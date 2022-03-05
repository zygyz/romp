#include "Core.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>

#include "ParallelRegionData.h"
#include "RecordManagement.h"
#include "TaskInfoQuery.h"
#include "ThreadData.h"

#define REDUNDANT_RECORD_REMOVAL_THRESHOLD 32

extern PerformanceCounters gPerformanceCounters;

bool analyzeRaceCondition(const Record& histRecord, const Record& curRecord, const uint64_t checkedAddress, RecordManagementInfo& recordManagementInfo) {
  auto histLabel = histRecord.getLabel(); 
  auto curLabel = curRecord.getLabel(); 
  recordManagementInfo.nodeRelation = eUndefinedNodeRelation;
  recordManagementInfo.lockRelation = eUndefinedLockRelation;
  RAW_DLOG(INFO, "analyze race condition - address: %lx hist label: %s hist is write: %d cur label: %s cur is write: %d\n", checkedAddress, histLabel->toString().c_str(), histRecord.isWrite(), curLabel->toString().c_str(), curRecord.isWrite());
  // we want to set both lock set info and node relation info so we don't early return.
  auto hasCommonLock = analyzeMutualExclusion(histRecord, curRecord, recordManagementInfo);

  int diffIndex;
  auto isHistoryAccessBeforeCurrentAccess = happensBefore(histLabel, curLabel, diffIndex, recordManagementInfo);    
  if (diffIndex == eRightIsPrefix) {
    RAW_DLOG(FATAL, "current access is prefix: %lx", checkedAddress);
  }

  auto isMutexTask = false;  
  auto isInReduction = false;
  if (!isHistoryAccessBeforeCurrentAccess) {
    // if there is no happens before relation, we further determine the e
    auto currentAccessIsInReduction = curRecord.isInReduction(); 
    auto historyAccessIsInReduction = histRecord.isInReduction();
    RAW_DLOG(INFO, "current access is in reduction: %d  history access is in reduction: %d", currentAccessIsInReduction, historyAccessIsInReduction);
    if (currentAccessIsInReduction && historyAccessIsInReduction) {
      RAW_DLOG(INFO, "memory access is in reduction phase. memory address: %lx", checkedAddress);
      // only the variable being reduced to is data race free. 
      recordManagementInfo.otherSynchronizationInfo = eInReduction;
      isInReduction = true;
    } else {
      // further check explicit task dependence if current task and history task 
      // are both explicit tasks. If no task dependence, return true
      auto curTaskData = static_cast<TaskData*>(curRecord.getTaskPtr());
      auto histTaskData = static_cast<TaskData*>(histRecord.getTaskPtr()); 
      if (curTaskData->isExplicitTask && histTaskData->isExplicitTask) {
        // first check if the two tasks are mutex tasks
        if (curTaskData->isMutexTask && histTaskData->isMutexTask) { 
          RAW_DLOG(INFO, "current access and history access are mutex task memory address: %lx", checkedAddress);
          recordManagementInfo.otherSynchronizationInfo = eIsMutexTask;
          isMutexTask = true;
        }
        // have to get the associated parallel region
        ParallelRegionInfo parallelRegionInfo;
        if (!queryParallelRegionInfo(0, parallelRegionInfo)) {
          RAW_LOG(FATAL, "cannot get parallel region data");
        } 
        auto parallelRegionData= static_cast<ParallelRegionData*>(parallelRegionInfo.parallelData->ptr); 
        // have to lock the task dep graph before graph traversal
        pfq_rwlock_node_t node;
  #ifdef PERFORMANCE
        ReaderWriterLockGuard guard(&(parallelRegionData->lock), &node, &gPerformanceCounters);
  #else
        ReaderWriterLockGuard guard(&(parallelRegionData->lock), &node, nullptr);
  #endif
        if (parallelRegionData->taskDepGraph.hasPath((void*)histTaskData, (void*)curTaskData)) {
          isHistoryAccessBeforeCurrentAccess= true;
          recordManagementInfo.nodeRelation = eHappensBefore;
        }
      }
    }
  }
  auto hasDataRace = !isInReduction && !isMutexTask && !hasCommonLock && !isHistoryAccessBeforeCurrentAccess && (histRecord.isWrite() || curRecord.isWrite());
  RAW_DLOG(INFO, "has data race: %d memory address: %lx hist label: %s cur label: %s is in reduction: %d has common lock: %d happens before: %d", hasDataRace, checkedAddress, histLabel->toString().c_str(), curLabel->toString().c_str(), isInReduction, hasCommonLock, isHistoryAccessBeforeCurrentAccess); 
  return hasDataRace;
}

// return true if has mutual exclusion
bool analyzeMutualExclusion(const Record& histRecord, const Record& curRecord, RecordManagementInfo& recordManagementInfo) {
  if (histRecord.hasHardwareLock() && curRecord.hasHardwareLock()) {
    recordManagementInfo.lockRelation = eHasCommonLock;
    return true;
  }
  //now at least one of the access does not have hardware lock. 
  auto histLockSet = histRecord.getLockSet(); 
  auto curLockSet = curRecord.getLockSet();  
  auto historyLockSetIsEmpty = histLockSet == nullptr || histLockSet->isEmpty();
  auto currentLockSetIsEmpty = curLockSet == nullptr || curLockSet->isEmpty();
  if (historyLockSetIsEmpty && currentLockSetIsEmpty) {
    recordManagementInfo.lockRelation = eBothEmptyLock; 
    return false;
  }
  if (!historyLockSetIsEmpty && currentLockSetIsEmpty) {
    recordManagementInfo.lockRelation = eCurrentNoLockHistoryHasLock; 
    return false;
  } 
  if (historyLockSetIsEmpty && !currentLockSetIsEmpty) {
    recordManagementInfo.lockRelation = eHistoryNoLockCurrentHasLock;
    return false;
  }
  if (isSubSet(histLockSet, curLockSet)) {
    recordManagementInfo.lockRelation = eCurrentLockSetContainsHistoryLockSet;
    return true;
  } 
  if (isSubSet(curLockSet, histLockSet)) {
    recordManagementInfo.lockRelation = eHistoryLockSetContainsCurrentLockSet;
    return true;
  }
  if (hasCommonLock(curLockSet, histLockSet)) {
    recordManagementInfo.lockRelation = eHasCommonLock;
    return true;
  }
  recordManagementInfo.lockRelation = eNoCommonLock;
  return false;
}

bool happensBefore(Label* histLabel, Label* curLabel, int& diffIndex, RecordManagementInfo& recordManagementInfo) {
  diffIndex = compareLabels(histLabel, curLabel);
  if (diffIndex < 0) {
    switch(diffIndex) {
      case static_cast<int>(eSameLabel):
        recordManagementInfo.nodeRelation = eSameNode; 
        return true;
      case static_cast<int>(eLeftIsPrefix):
        recordManagementInfo.nodeRelation = eHappensBefore;
        return true;
      case static_cast<int>(eRightIsPrefix):
	// current record -> hist record
	RAW_LOG(FATAL, "cur -> hist, cur: %s hist: %s", curLabel->toString().c_str(), histLabel->toString().c_str());
        return false;
      default:
        RAW_LOG(FATAL, "unknown label compare result");
        return false;
    }
  }
  auto histSegment = histLabel->getKthSegment(diffIndex); 
  auto curSegment = curLabel->getKthSegment(diffIndex);
  uint64_t histOffset, curOffset, histSpan, curSpan; 
  histSegment->getOffsetSpan(histOffset, histSpan);
  curSegment->getOffsetSpan(curOffset, curSpan);
  if (histSpan != curSpan) {
    RAW_LOG(FATAL, "left span: %lu != right span: %lu", histSpan, curSpan);
  }
  if (histSpan == 1) { // explicit task or work share task or initial task
    auto histType = histSegment->getType();
    auto curType = curSegment->getType();
    RAW_CHECK(histType == curType, "not expecting hist seg type\
            != cur seg type");
    switch(histType) {
      case eImplicit:
        recordManagementInfo.nodeRelation = eHappensBefore;
        return true;
      case eLogical:
        return analyzeOrderedSection(histLabel, curLabel,  diffIndex, recordManagementInfo);
      case eExplicit:
        // same explciit task for T(histLabel[diffIndex]) and T(curLabel[diffIndex])
        return analyzeSameTask(histLabel, curLabel, diffIndex, recordManagementInfo);
      default:
        RAW_LOG(FATAL, "unexpected segment type: %d", histType);
        return false;
    }
  } 
  if (histOffset != curOffset) { 
    auto span = histSpan;
    if (histOffset % span == curOffset % span) {
      RAW_CHECK(histOffset < curOffset, "not expecting history access joined \
               before current access");
      recordManagementInfo.nodeRelation = eHappensBefore;
      return true; 
    } 
    return analyzeSiblingImplicitTask(histLabel, curLabel, diffIndex, recordManagementInfo);
  } 
  return analyzeSameTask(histLabel, curLabel, diffIndex, recordManagementInfo); 
}

bool analyzeSiblingImplicitTask(Label* histLabel, Label* curLabel, int diffIndex, RecordManagementInfo& recordManagementInfo) { 
  auto lenHistLabel = histLabel->getLabelLength();
  auto lenCurLabel = curLabel->getLabelLength();
  auto isSibling = lenHistLabel == lenCurLabel && diffIndex == lenCurLabel - 1;
  if (diffIndex == (lenHistLabel - 1) || diffIndex == (lenCurLabel - 1)) {
    // if any one if T(histLabel) and T(curLabel) is leaf implicit task, 
    // we are sure there is no happens-beofre relationship
    if (isSibling) {
      recordManagementInfo.nodeRelation = eSiblingParallel;
    } else {
      recordManagementInfo.nodeRelation = eNonSiblingParallel;  
    }
    return false;
  }
  // now diffIndex + 1 must not be out of boundary
  auto histNextSeg = histLabel->getKthSegment(diffIndex + 1);  
  auto curNextSeg = curLabel->getKthSegment(diffIndex + 1);
  auto histNextSegType = histNextSeg->getType();
  auto curNextSegType = curNextSeg->getType();
  if (histNextSegType == eLogical && curNextSegType == eLogical) {
    // in this case, it is possible to be ordered with ordered section
    auto histNextWorkShareType = static_cast<WorkShareSegment*>(histNextSeg)->getWorkShareType(); 
    auto curNextWorkShareType = static_cast<WorkShareSegment*>(curNextSeg)->getWorkShareType();
    if (histNextWorkShareType == eSection || curNextWorkShareType == eSection) {   
      // section construct does not have ordered section 
      recordManagementInfo.nodeRelation = eNonSiblingParallel;
      return false;
    } 
    auto histSeg = histLabel->getKthSegment(diffIndex);
    auto curSeg = curLabel->getKthSegment(diffIndex);
    auto histSegLoopCount = histSeg->getLoopCount();
    auto curSegLoopCount = curSeg->getLoopCount();
    if (histSegLoopCount == curSegLoopCount) {
      return analyzeOrderedSection(histLabel, curLabel, diffIndex + 1, recordManagementInfo);
    } 
    recordManagementInfo.nodeRelation = eNonSiblingParallel;
    return false;
  }
  recordManagementInfo.nodeRelation = eNonSiblingParallel;
  return false;
}

// T(histLabel[startIndex]) and T(curLabel[startIndex]) are logical tasks
bool analyzeOrderedSection(Label* histLabel, Label* curLabel, int startIndex, RecordManagementInfo& recordManagementInfo) {
  auto histBaseSeg  = histLabel->getKthSegment(startIndex);
  auto curBaseSeg = curLabel->getKthSegment(startIndex);
  auto histSegment = static_cast<WorkShareSegment*>(histBaseSeg);
  auto curSegment = static_cast<WorkShareSegment*>(curBaseSeg);
  auto histLabelLength = histLabel->getLabelLength();
  auto curLabelLength = curLabel->getLabelLength();
  auto isSibling =  histLabelLength == curLabelLength && startIndex == histLabelLength - 1; 
  
  if (histSegment->isWorkSharePlaceHolder() || curSegment->isWorkSharePlaceHolder()) {
    // have not entered the workshare construct yet.
    if (isSibling) {
      recordManagementInfo.nodeRelation = eSiblingParallel;  // two sibling implicit tasks
    } else {
      recordManagementInfo.nodeRelation = eNonSiblingParallel; 
    }
    return false;
  } 
  auto histPhase = histBaseSeg->getPhase();
  auto curPhase = curBaseSeg->getPhase();
  auto histExitRank = computeExitRank(histPhase);
  auto curEnterRank = computeEnterRank(curPhase);
  if (histExitRank < curEnterRank) {
    auto histLen = histLabel->getLabelLength();
    auto curLen = curLabel->getLabelLength();
    if (startIndex == histLen - 1) {
      recordManagementInfo.nodeRelation = eHappensBefore;
      return true;
    } 
    return analyzeOrderedDescendants(histLabel, startIndex, histPhase, recordManagementInfo);
  }
  if (isSibling) {
    recordManagementInfo.nodeRelation = eSiblingParallel;
  } else {
    recordManagementInfo.nodeRelation = eNonSiblingParallel;
  }
  return false;
}

bool analyzeOrderedDescendants(Label* histLabel, int startIndex, uint64_t histPhase, RecordManagementInfo& recordManagementInfo) {
  auto nextSegment = histLabel->getKthSegment(startIndex + 1);
  auto nextSegmentType = nextSegment->getType();
  if (nextSegmentType == eLogical) {
    RAW_LOG(FATAL, "does not expect next segment of workshare seg to be workshare segment"); 
    return false;
  }
  if (nextSegmentType == eImplicit) {
    // we know that implicit task syncs with its parent task
    recordManagementInfo.nodeRelation = eHappensBefore;
    return true;
  } 
  if (nextSegmentType == eExplicit) {
    auto curSegment = histLabel->getKthSegment(startIndex);
    auto taskGroupLevel = curSegment->getTaskGroupLevel();
    if (taskGroupLevel > 0) {
      auto phase = curSegment->getPhase();  
      if (phase % 2 == 1 || (phase % 2 == 0 && nextSegment->isTaskGroupSync() && nextSegment->getTaskGroupPhase() <= histPhase)) {
        recordManagementInfo.nodeRelation = eHappensBefore; 
        return true;
      } 
    }
    if (nextSegment->isTaskwaited() && nextSegment->getTaskwaitPhase() <= histPhase) {
      return analyzeExplicitTaskSynchronizationWithTaskWait(histLabel, startIndex + 1, recordManagementInfo);
    }
  }
  recordManagementInfo.nodeRelation = eNonSiblingParallel;
  return false;
}

bool analyzeExplicitTaskSynchronizationWithTaskWait(Label* label, int startIndex, RecordManagementInfo& recordManagementInfo) {
  auto lenLabel = label->getLabelLength(); 
  if (startIndex == lenLabel - 1) {
    // already the leaf task
    recordManagementInfo.nodeRelation = eHappensBefore;    
    return true;
  }
  for (auto i = startIndex; i < lenLabel; ++i) {
    auto segment = label->getKthSegment(i);
    auto segmentType = segment->getType();
    if (segmentType == eImplicit) {
      recordManagementInfo.nodeRelation = eHappensBefore;    
      return true;
    } else if (segmentType == eExplicit) {
      auto taskGroupLevel = segment->getTaskGroupLevel();    
      if (taskGroupLevel > 0) {
        // taskgroup guarantees completion of descendants
        recordManagementInfo.nodeRelation = eHappensBefore;    
        return true;
      } else {
        if (!segment->isTaskwaited()) { 
          // if current explicit task T(label, i) is not waited 
          // by parent task, no sync.
          recordManagementInfo.nodeRelation = eNonSiblingParallel;
          return false;
        }
      }
    }
  }
  recordManagementInfo.nodeRelation = eHappensBefore;
  return true;
}

// T(histLabel[diffIndex]) and T(curLabel[diffIndex]) are the same implicit or explicit task. If they are the same implicit task, they
// have the same offset value. 
bool analyzeSameTask(Label* histLabel, Label* curLabel, int diffIndex, RecordManagementInfo& recordManagementInfo) {
  auto lenHistLabel = histLabel->getLabelLength(); 
  auto lenCurLabel = curLabel->getLabelLength();
  // T(histLabel[diffIndex]) is leaf task
  if (diffIndex == (lenHistLabel - 1)) {
    recordManagementInfo.nodeRelation = eHappensBefore;
    return true;
  }      
  // T(curLabel[diffIndex]) is leaf task 
  if (diffIndex == (lenCurLabel - 1)) {
    auto histNextSeg = histLabel->getKthSegment(diffIndex + 1);
    auto histNextType = histNextSeg->getType();
    RAW_CHECK(histNextType != eImplicit, "not expecting next level task to be implicit task");
    if (histNextType == eExplicit) {
      // check if T(histLabel) happens before T(curLabel) because of explicit task synchronization
      auto histSeg = histLabel->getKthSegment(diffIndex);
      auto histTaskwait = histSeg->getTaskwait();
      auto curSeg = curLabel->getKthSegment(diffIndex);
      auto curTaskwait = curSeg->getTaskwait();
      RAW_CHECK(curTaskwait >= histTaskwait, "not expecting hist taskwait to be larger than cur taskwait");
      if (curTaskwait == histTaskwait) {
        // futher check task group sync 
        auto histTaskGroupLevel = histSeg->getTaskGroupLevel();      
        if (histTaskGroupLevel > 0 && histNextSeg->isTaskGroupSync()) {
          // T(histLabel) happens before T(curLabel) only when the taskgroup 
          // construct wrapping T(histLabel,diffIndex + 1) finishes before 
          // T(curLabel, diffIndex)
          recordManagementInfo.nodeRelation = eHappensBefore;
          return true;
        }
        recordManagementInfo.nodeRelation = eNonSiblingParallel; 
        return false;
      } 
      return analyzeExplicitTaskSynchronizationWithTaskWait(histLabel, diffIndex + 1, recordManagementInfo); 
    } 
    if (histNextType == eLogical) {
      if (static_cast<WorkShareSegment*>(histNextSeg)->isWorkSharePlaceHolder()) {
        recordManagementInfo.nodeRelation = eHappensBefore; 
        return true;
      }
      recordManagementInfo.nodeRelation = eNonSiblingParallel;
      return false; 
    }
  } 
  // both T(histLabel, diffIndex) and T(curLabel, diffIndex) have descendant tasks
  // Precondition: If T(histLabel, diffIndex) and T(curLabel, diffIndex) are the same implicit task, with offset value being the same. they could be at different phase, but 
  // there should not be any barrier between the two phases, otherwise, their parent segment should have already been different.
  auto histSegment = histLabel->getKthSegment(diffIndex); 
  auto curSegment = curLabel->getKthSegment(diffIndex); 
  auto histNextSegment = histLabel->getKthSegment(diffIndex + 1);
  auto curNextSegment = curLabel->getKthSegment(diffIndex + 1);
  auto histSegmentType = histSegment->getType();
  auto curSegmentType = curSegment->getType(); 
  auto histNextSegmentType = histNextSegment->getType();
  auto curNextSegmentType = curNextSegment->getType();
  RAW_CHECK(histSegmentType == curSegmentType, "not expecting segment type to be different");
  RAW_CHECK(histSegmentType != eLogical && curSegmentType != eLogical, "not expecting segment type to be logical task");
  // T(histLabel, diffIndex) and T(curLabel, diffIndex) are the same implicit task or the same explicit task.
  // In terms of possible combinations of T_c = T(curLabel, diffIndex+1) and T_h = T(histLabel, diffIndex + 1), 
  //x 1) T_h is implicit task, T_c is implicit task. This is not possible. If T_h and T_c are sibling implicit tasks, histLabel[diffIndex] and curLabel[diffIndex] would have been the same. If T_h and T_c belong to two consecutive parallel regions, 
  //  the offset field in histLabel[diffIndex] and curLabel[diffIndex] would be different, this violates the precondition  
  //x 2) T_h is implicit task, T_c is explicit task. This is not possible. Because T_c can only be created after the parallel region that encloses T_h finishes. This would make curLabel[diffIndex-1] and histLabel[diffIndex-1] different already.
  //x 3) T_h is implicit task, T_c is logical task. This is not possible. Because T_c can only be created after the parallel region that encloses T_h finishes. This would make curLabel[diffIndex-1] and histLabel[diffIndex-1] different already.
  //  4) T_h is explicit task, T_c is implicit task. This is possible.
  //  5) T_h is explicit task, T_c is logical task. This is possible
  //  6) T_h is explicit task, T_c is explicit task. This is possible
  //  7) T_h is logical task, T_c is implicit task. This is only possible when the worksharing construct specifies nowait clause, and T(curLabel,diffIndex) = T(histLabel, diffIndex) being implicit task
  //  8) T_h is logical task, T_c is logical task. This is only possible when the worksharing construct specifies nowait clause. and T(curLabel,diffIndex) = T(histLabel, diffIndex) being implicit task
  //  9) T_h is logical task, T_c is explicit task. This is only possible when the worksharing construct specifies nowait clause. and T(curLabel,diffIndex) = T(histLabel, diffIndex) being implicit task
  if (histNextSegmentType == eExplicit) {
    if (analyzeTaskGroupSync(histLabel, curLabel, diffIndex)) {
      recordManagementInfo.nodeRelation = eHappensBefore;
      return true;
    }
    auto histTaskwait = histSegment->getTaskwait();
    auto curTaskwait = curSegment->getTaskwait();
    if (histTaskwait == curTaskwait) {
      // no taskwait clause   
      recordManagementInfo.nodeRelation = eNonSiblingParallel;
      return false;
    } else if (histTaskwait < curTaskwait) {
      // there is taskwait between creation of T(histLabel, diffIndex + 1) and T(curLabel, diffIndex + 1)  
      return analyzeExplicitTaskSynchronizationWithTaskWait(histLabel, diffIndex + 1, recordManagementInfo); 
    } else {
      RAW_LOG(FATAL, "not expecting hist taskwait to be larger than cur taskwait");
      return false;
    }
  }
  if (histNextSegmentType == eLogical) {
    // this corresponds to case 7, 8, 9. There is no wait clause implied. 
    recordManagementInfo.nodeRelation = eNonSiblingParallel;
    return false;
  } 
  RAW_CHECK(histNextSegmentType != eImplicit, "not expecting hist next segment type to be implicit.");
  return false;
}

// analyze happens before relationship when T(histLabel, diffIndex + 1) is explicit task
bool analyzeHistNextExplicitTask(Label* histLabel, Label* curLabel, int diffIndex, RecordManagementInfo& recordManagementInfo) {
  // First check if ordered by task group construct   
  if (analyzeTaskGroupSync(histLabel, curLabel, diffIndex)) {
    recordManagementInfo.nodeRelation = eHappensBefore;
    return true;
  }
  auto histSegment = histLabel->getKthSegment(diffIndex);      
  auto curSegment = curLabel->getKthSegment(diffIndex);
  auto histTaskwait = histSegment->getTaskwait();
  auto curTaskwait = curSegment->getTaskwait();
  if (histTaskwait == curTaskwait) {
    // no taskwait clause   
     
    return false;
  } else if (histTaskwait < curTaskwait) {
    // there is taskwait between creation of T(histLabel, diffIndex + 1) and 
    // T(curLabel, diffIndex + 1)  
    return analyzeExplicitTaskSynchronizationWithTaskWait(histLabel, diffIndex + 1, recordManagementInfo); 
  } else {
    RAW_LOG(FATAL, "not expecting hist taskwait to be larger than \
            cur taskwait");
    return false;
  }
  return true;
}

bool analyzeTaskGroupSync(Label* histLabel, Label* curLabel, int diffIndex) {
  auto histSeg = histLabel->getKthSegment(diffIndex);
  auto curSeg = curLabel->getKthSegment(diffIndex);
  auto histTaskGroupId = histSeg->getTaskGroupId(); 
  auto histTaskGroupLevel = histSeg->getTaskGroupLevel();
  auto curTaskGroupId = curSeg->getTaskGroupId();
  auto curTaskGroupLevel = curSeg->getTaskGroupLevel();
  return (histTaskGroupId < curTaskGroupId) && (histTaskGroupLevel >= curTaskGroupLevel);
}

uint64_t computeExitRank(uint64_t phase) {
  return phase - (phase % 2); 
}

uint64_t computeEnterRank(uint64_t phase) {
  return phase + (phase % 2);
}

// iterate over access records in accessHistory, make access history managemnet decision 
// This function is called with read lock held. ALso, this function being callled implies that 
// there is no race condition between current access record and all existing history records. 
// In this function, we determine what could be pruned and update record state.
void manageAccessRecords(AccessHistory* accessHistory, const Record& currentRecord, ReaderWriterLockGuard& lockGuard, std::vector<RecordManagementInfo>& info) {
  auto records = accessHistory->getRecords();
  auto infoSize = info.size(); 
  auto recordsNum = records->size();
  RAW_DLOG(INFO, "num access records: %d", recordsNum);
  RAW_CHECK(infoSize == recordsNum, "access records size is not equal to records number");
  auto recordState = accessHistory->getRecordState();
  std::vector<int> recordRemovalCandidates;
  auto canSkipAddingCurrentRecord = false;
  for (int i = 0; i < infoSize; ++i) {
    auto recordManagementInfo = info.at(i);
    auto historyRecord = records->at(i); 
    RAW_DLOG(INFO, "i: %d , size: %d,  node relation: %d, lock relation: %d", i, info.size(),  recordManagementInfo.nodeRelation, recordManagementInfo.lockRelation); 
    auto historyAccessIsWrite = historyRecord.isWrite();
    auto currentAccessIsWrite = currentRecord.isWrite();
//    if (((historyAccessIsWrite && currentAccessIsWrite) || historyAccessIsWrite == false) && 
//          recordManagementInfo.nodeRelation == eHappensBefore && 
//          recordManagementInfo.lockRelation == eHistoryLockSetContainsCurrentLockSet) {
//      recordRemovalCandidates.push_back(i);
//    } 
//else if (canSkipAddingCurrentRecord == false && historyAccessIsWrite == false && currentAccessIsWrite == false && 
//              (recordManagementInfo.lockRelation == eCurrentLockSetContainsHistoryLockSet || 
//               recordManagementInfo.lockRelation == eBothEmptyLock || 
//               recordManagementInfo.lockRelation == eHistoryNoLockCurrentHasLock) && 
//               recordManagementInfo.nodeRelation == eSiblingParallel) {
//      RAW_DLOG(INFO, "sibling node, skip adding current to the record");
      // if we determine current record can be skipped, this is valid throughout the iteration. Because this state is mutual exclusive with records removal candidates case.
//      canSkipAddingCurrentRecord = true; 
//    }
  }
  if (recordRemovalCandidates.size() > 0) {
    RAW_DLOG(INFO, "records removal candidate size: %d", recordRemovalCandidates.size());
    auto didSkipRemovingRecords = false;
    if (recordsNum < REDUNDANT_RECORD_REMOVAL_THRESHOLD) {
      didSkipRemovingRecords = true; 
    } else {
      auto hasWriteWriteContention = lockGuard.upgradeFromReaderToWriter();
      if (!hasWriteWriteContention) {
        accessHistory->removeRecords(recordRemovalCandidates);
      } else {
        didSkipRemovingRecords = true; 
      }
    } 
#ifdef PERFORMANCE
    if (didSkipRemovingRecords) {
      gPerformanceCounters.bumpNumAccessHistorySkipRemoveRecords(); 
    }
#endif
  }
  if (!canSkipAddingCurrentRecord) {
    lockGuard.upgradeFromReaderToWriter();  
    accessHistory->addRecordToAccessHistory(currentRecord);     
  }
} 

// return true if there is data race. 
bool checkDataRaceForMemoryAddress(uint64_t checkedAddress, AccessHistory* accessHistory, const Record& currentRecord, std::vector<RecordManagementInfo>& info) {
  auto records = accessHistory->getRecords();
  for (int i = 0; i < records->size(); ++i) { 
    auto histRecord = records->at(i);
    RecordManagementInfo recordManagementInfo;      
    if (analyzeRaceCondition(histRecord, currentRecord, checkedAddress, recordManagementInfo)) {
      accessHistory->setFlag(eDataRaceFound); 
      return true;
    }
    info.push_back(recordManagementInfo); 
  }
  return false;
}
