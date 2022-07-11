#include "Core.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>

#include "ParallelRegionData.h"
#include "RecordManagement.h"
#include "TaskData.h"
#include "TaskInfoQuery.h"
#include "ThreadData.h"

extern PerformanceCounters gPerformanceCounters;

bool analyzeRaceCondition(const Record& histRecord, const Record& curRecord, RecordManagementInfo& recordManagementInfo) {
  auto checkedAddress = curRecord.getCheckedMemoryAddress();
  auto histLabel = histRecord.getLabel(); 
  auto curLabel = curRecord.getLabel(); 
  // we want to set both lock set info and node relation info so we don't early return.
  recordManagementInfo.nodeRelation = eUndefinedNodeRelation;
  recordManagementInfo.lockRelation = eUndefinedLockRelation;
  int diffIndex;
  auto hasCommonLock = analyzeMutualExclusion(histRecord, curRecord, recordManagementInfo);
  auto histTaskData = static_cast<TaskData*>(histRecord.getTaskPtr()); 
  auto curTaskData = static_cast<TaskData*>(curRecord.getTaskPtr());
  auto isHistoryAccessBeforeCurrentAccess = happensBefore(histLabel, curLabel, diffIndex, histTaskData, curTaskData, recordManagementInfo);    

//  if (histTaskData == curTaskData) {
//    // both memory accesses are performed by the same task. 
//    // the record management info has been collected 
//    return false;
//  }

  auto histRecordMemoryOwner = histRecord.getMemoryAddressOwner();
  auto curRecordMemoryOwner = curRecord.getMemoryAddressOwner(); 
  if (histRecordMemoryOwner != curRecordMemoryOwner) {
    RAW_DLOG(INFO, "memory owner not match , memr addr: %lx cur instn: %lx cur owner: %lx hist instn: %lx hist owner: %lx", checkedAddress, curRecord.getInstructionAddress(), curRecordMemoryOwner, histRecord.getInstructionAddress(), histRecordMemoryOwner);
    return false;
  } else {
    RAW_DLOG(INFO, "memory owner is matching, memr addr: %lx cur instn: %lx cur owner: %lx hist instn: %lx hist owner: %lx", checkedAddress, curRecord.getInstructionAddress(), curRecordMemoryOwner, histRecord.getInstructionAddress(), histRecordMemoryOwner);
  }

  if (hasCommonLock) {
    return false;
  }

  if (isHistoryAccessBeforeCurrentAccess) {
    return false;
  }

  if ((curTaskData->getIsExplicitTask() && histTaskData->getIsExplicitTask() && curTaskData->getIsMutexTask() && histTaskData->getIsMutexTask())) {
    // two memory accesses are performed by two mutex tasks. In these cases, there is no data race.
    // there exists happens-before relationship between two memory accesses. No data race.
    return false; 
  } 

  if (histRecord.isTLSAccess() && curRecord.isTLSAccess()) {
    // both memory access are performed as thread local storage. 
    return false; 
  }
   
  if ((histRecord.isInReduction() || curRecord.isInReduction())&& histTaskData->parallelRegionDataPtr == curTaskData->parallelRegionDataPtr) {
    return false; 
  }
 
  auto currentDataSharingType = curRecord.getDataSharingType();
  auto historyDataSharingType = histRecord.getDataSharingType();
  auto bothAccessesAreTaskPrivate = ((currentDataSharingType == eThreadPrivateAccessCurrentTask || currentDataSharingType == eExplicitTaskPrivate) && (historyDataSharingType == eThreadPrivateAccessCurrentTask || historyDataSharingType == eExplicitTaskPrivate));
  auto hasDataRace = !isHistoryAccessBeforeCurrentAccess && (histRecord.isWrite() || curRecord.isWrite()) && !bothAccessesAreTaskPrivate;
  if (hasDataRace) {
    RAW_DLOG(INFO, "data race found! hist is write: %d cur is write: %d , memr addr: %lx cur instn: %lx hist instn: %lx hist label: %s cur label: %s hist fields breakd down: %s cur fields break down: %s hist owner: %lx cur owner: %lx", histRecord.isWrite(), curRecord.isWrite(), checkedAddress, curRecord.getInstructionAddress(), histRecord.getInstructionAddress(), histRecord.getLabel()->toString().c_str(), curRecord.getLabel()->toString().c_str(), histRecord.getLabel()->toFieldsBreakdown().c_str(), curRecord.getLabel()->toFieldsBreakdown().c_str(), histRecord.getMemoryAddressOwner(), curRecord.getMemoryAddressOwner());
  }
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
    recordManagementInfo.lockRelation = eCurrentLockSetContainsHistoryLockSetNonEmpty;
    return true;
  } 
  if (isSubSet(curLockSet, histLockSet)) {
    recordManagementInfo.lockRelation = eHistoryLockSetContainsCurrentLockSetNonEmpty;
    return true;
  }
  if (hasCommonLock(curLockSet, histLockSet)) {
    recordManagementInfo.lockRelation = eHasCommonLock;
    return true;
  }
  recordManagementInfo.lockRelation = eNoCommonLock;
  return false;
}

// assuming proper concurrency control for access history
void  setMemoryOwner(AccessHistory* accessHistory, int dataSharingType, void* taskData, void* memoryAddress) {
  if (dataSharingType == eThreadPrivateAccessCurrentTask || dataSharingType == eExplicitTaskPrivate) {
    pfq_rwlock_node_t me;
    ReaderWriterLockGuard guard(&(accessHistory->getLock()), &me, &gPerformanceCounters);
    if (accessHistory->getOwner() != taskData) {
      guard.upgradeFromReaderToWriter();
      accessHistory->setOwner(taskData);
    }
  } 
}

bool happensBefore(Label* histLabel, Label* curLabel, int& diffIndex, TaskData* histTaskData, TaskData* curTaskData, RecordManagementInfo& recordManagementInfo) {
  diffIndex = compareLabels(histLabel, curLabel);
  auto histHappensBeforeCur = false;
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
        break;
      default:
        RAW_LOG(FATAL, "unknown label compare result");
        break;
    }
  }
  auto histSegment = histLabel->getKthSegment(diffIndex); 
  auto curSegment = curLabel->getKthSegment(diffIndex);
  uint64_t histOffset, curOffset, histSpan, curSpan; 
  histSegment->getOffsetSpan(histOffset, histSpan);
  curSegment->getOffsetSpan(curOffset, curSpan);

  RAW_CHECK(histSpan == curSpan, "left span != right span");

  if (histSpan == 1) { // explicit task or work share task or initial task
    auto histType = histSegment->getType();
    auto curType = curSegment->getType();
    RAW_CHECK(histType == curType, "not expecting hist seg type != cur seg type");
    switch(histType) {
      case eImplicit:
        recordManagementInfo.nodeRelation = eHappensBefore;
        return true;
      case eLogical:
        histHappensBeforeCur = analyzeOrderedSection(histLabel, curLabel,  diffIndex, false /*isFromSiblingImplicitTasks*/, recordManagementInfo);
        break;
      case eExplicit:
        // same explciit task for T(histLabel[diffIndex]) and T(curLabel[diffIndex])
        histHappensBeforeCur = analyzeSameTask(histLabel, curLabel, diffIndex, recordManagementInfo);
        break;
      default:
        RAW_LOG(FATAL, "unexpected segment type: %d", histType);
        break;
    }
  } 
  if (histOffset != curOffset) { 
    auto span = histSpan;
    if (histOffset % span == curOffset % span) {
      RAW_CHECK(histOffset < curOffset, "not expecting history access joined \
               before current access");
      recordManagementInfo.nodeRelation = eHappensBefore;
      histHappensBeforeCur = true;
    } else {
      histHappensBeforeCur = analyzeSiblingImplicitTask(histLabel, curLabel, diffIndex, recordManagementInfo);
    } 
  } else {
    histHappensBeforeCur = analyzeSameTask(histLabel, curLabel, diffIndex, recordManagementInfo); 
  } 
  // comparing task label does not infer happens-before relation. Addtionally we check other situations
  // that are not recorded in task label to make final decision of happens-before relation.
  if (histHappensBeforeCur == false) {
    // further check explicit task dependence if current task and history task 
    // are both explicit tasks. If there exists task dependence, no data race.
    if (histTaskData->getIsExplicitTask()) {
      ParallelRegionInfo parallelRegionInfo;
      RAW_CHECK(queryParallelRegionInfo(0, parallelRegionInfo) == true, "cannot get parallel region data");
      auto parallelRegionData = static_cast<ParallelRegionData*>(parallelRegionInfo.parallelData->ptr); 
      pfq_rwlock_node_t node;
#ifdef PERFORMANCE
        ReaderWriterLockGuard guard(&(parallelRegionData->lock), &node, &gPerformanceCounters); // needed in happens before analys
#else
        ReaderWriterLockGuard guard(&(parallelRegionData->lock), &node, nullptr);
#endif
      if (curTaskData->getIsExplicitTask()) {
        // both history task and current task are explicit task. Check if there exists explicit task dependence between them. 
        if (parallelRegionData->taskDependenceGraph.hasPath(static_cast<void*>(histTaskData), static_cast<void*>(curTaskData))) {
          return true;
        }
      } else {
        // current task is implicit task, history task is explicit task, check if there exists order by undeferred task 
        // i.e., if current thread executing the implicit task has encountered some undeferred task before. And there exists
        // explicit task dependence between history explicit task and the undeferred task. In this case, there exists happens-before  relationship between history task and current task.
        for (auto undeferredTask : curTaskData->undeferredTasks) {
          if (parallelRegionData->taskDependenceGraph.hasPath(static_cast<void*>(histTaskData), undeferredTask)) {
            return true;
          }
        }
      }
    }
  }
  return histHappensBeforeCur;  
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
      return analyzeOrderedSection(histLabel, curLabel, diffIndex + 1, true/*isFromSiblingImplicitTasks*/, recordManagementInfo);
    } 
    recordManagementInfo.nodeRelation = eNonSiblingParallel;
    return false;
  }
  recordManagementInfo.nodeRelation = eNonSiblingParallel;
  return false;
}

// T(histLabel[startIndex]) and T(curLabel[startIndex]) are logical tasks
bool analyzeOrderedSection(Label* histLabel, Label* curLabel, int startIndex, bool isFromSiblingImplicitTasks, RecordManagementInfo& recordManagementInfo) {
  auto histBaseSeg  = histLabel->getKthSegment(startIndex);
  auto curBaseSeg = curLabel->getKthSegment(startIndex);
  auto histSegment = static_cast<WorkShareSegment*>(histBaseSeg);
  auto curSegment = static_cast<WorkShareSegment*>(curBaseSeg);
  auto histLabelLength = histLabel->getLabelLength();
  auto curLabelLength = curLabel->getLabelLength();
  auto isSibling = isFromSiblingImplicitTasks ? false :  histLabelLength == curLabelLength && startIndex == histLabelLength - 1; 
  if (isSibling) {
    recordManagementInfo.nodeRelation = eSiblingParallel;  // two sibling implicit tasks
  } else {
    recordManagementInfo.nodeRelation = eNonSiblingParallel; 
  }
  if (histSegment->isWorkSharePlaceHolder() || curSegment->isWorkSharePlaceHolder()) {
    // have not entered the workshare construct yet.
    return false;
  } 
  auto histPhase = histBaseSeg->getPhase();
  auto curPhase = curBaseSeg->getPhase();
  auto histWorkShareId = histSegment->getWorkShareId();
  auto curWorkShareId = curSegment->getWorkShareId();
   
  auto leftPhase = histWorkShareId < curWorkShareId ? histPhase : curPhase; 
  auto rightPhase = histWorkShareId < curWorkShareId ? curPhase : histPhase;

  if (leftPhase == rightPhase) {
    if (leftPhase % 2 == 0) {
      return false;  
    } else {
      // recordManagementInfo will be overwritten 
      return analyzeOrderedDescendants(histLabel, startIndex, histPhase, recordManagementInfo);
    }
  } else if (leftPhase > rightPhase) {
    return false;
  } else { // leftPhase < rightPhase
    return analyzeOrderedDescendants(histLabel, startIndex, histPhase, recordManagementInfo);
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

// This function is called under the premise that offset field is the same.
// There exists fields in histLabel[diffIndex] and curLabel[diffIndex] that are different.
// Return true if there exists happens-before relationship. Return false otherwise.
bool analyzeSameTask(Label* histLabel, Label* curLabel, int diffIndex, RecordManagementInfo& recordManagementInfo) {
  auto lenHistLabel = histLabel->getLabelLength(); 
  auto lenCurLabel = curLabel->getLabelLength();
  auto histDiffSegmentIsLeaf = diffIndex == (lenHistLabel - 1);
  auto curDiffSegmentIsLeaf = diffIndex == (lenCurLabel - 1);
  auto isHappensBefore = false; 
  if (histDiffSegmentIsLeaf && curDiffSegmentIsLeaf) {
    auto histDiffSegment = histLabel->getKthSegment(diffIndex);
    auto curDiffSegment = curLabel->getKthSegment(diffIndex);  
    auto histDiffSegmentType = histDiffSegment->getType();
    auto curDiffSegmentType = curDiffSegment->getType();          
    if (histDiffSegmentType == eLogical && curDiffSegmentType == eLogical) {
      auto histWorkShareID = static_cast<WorkShareSegment*>(histDiffSegment)->getWorkShareId();
      auto curWorkShareID = static_cast<WorkShareSegment*>(curDiffSegment)->getWorkShareId();
      recordManagementInfo.nodeRelation = eSiblingParallel; 
      return histWorkShareID == curWorkShareID;
    } 
    // otherwise, should be same implicit task at different phases, has happens-before relationship.
    recordManagementInfo.nodeRelation = eHappensBefore;
    return true; 
  } else if (histDiffSegmentIsLeaf) {  
    auto histDiffSegment = histLabel->getKthSegment(diffIndex);
    auto curDiffSegment = curLabel->getKthSegment(diffIndex);  
    auto curNextSegment = curLabel->getKthSegment(diffIndex + 1);
    auto curNextSegmentType = curNextSegment->getType();
    if (curNextSegmentType == eExplicit) {
      auto histTaskCreateCount = histDiffSegment->getTaskcreate(); 
      auto curTaskCreateCount = curDiffSegment->getTaskcreate();
      if (histTaskCreateCount <= curTaskCreateCount) {
        // if task create count if of history segment is not larger than task create count of current segment
        // then the explicit task T(curLabel, diffIndex+1) is created after T(curLabel)
        recordManagementInfo.nodeRelation = eHappensBefore;
        return true;  
      } else {
        recordManagementInfo.nodeRelation = eNonSiblingParallel;
        return false;
      }
    } else {
      recordManagementInfo.nodeRelation = eHappensBefore;
      return true;
    }
  } else if (curDiffSegmentIsLeaf) {
    // hist diff segment is not leaf, cur diff segment is leaf.
    auto histNextSegment = histLabel->getKthSegment(diffIndex + 1);
    auto histNextType = histNextSegment->getType();
    if (histNextType == eExplicit) {
      auto histSegment = histLabel->getKthSegment(diffIndex); 
      auto histTaskwait = histSegment->getTaskwait();
      auto curSegment = curLabel->getKthSegment(diffIndex);
      auto curTaskwait = curSegment->getTaskwait();
      RAW_CHECK(curTaskwait >= histTaskwait, "not expecting history taskwait be larger than current taskwait");
      if (curTaskwait == histTaskwait) {
        // further check task group sync. 
        // TODO: revisit task group handling.
        auto histTaskGroupLevel = histSegment->getTaskGroupLevel();
        if (histTaskGroupLevel > 0 && histNextSegment->isTaskGroupSync()) {
          recordManagementInfo.nodeRelation = eHappensBefore;
          isHappensBefore = true; 
        } else {
          recordManagementInfo.nodeRelation = eNonSiblingParallel;
          isHappensBefore = false;
        }
      } else {
        return analyzeExplicitTaskSynchronizationWithTaskWait(histLabel, diffIndex + 1, recordManagementInfo);
      }   
    } else if (histNextType == eLogical) {
      if (static_cast<WorkShareSegment*>(histNextSegment)->isWorkSharePlaceHolder()) {
        recordManagementInfo.nodeRelation = eHappensBefore;
        isHappensBefore = true;
      } else {
        recordManagementInfo.nodeRelation = eNonSiblingParallel;
        isHappensBefore = false;  
      }
    } else {
      // we claim that it is not possible for next segment of hist diff segment to be implicit task. 
      // because if so, T(curLabel[diffIndex]) should be the implicit task after finishing a parallel region.
      // This cause the offset field to be different.
      RAW_CHECK(histNextType != eImplicit, "not expecting next level task to be implicit task");
    }
  } else {
    // both hist diff segment and cur diff segment are not leaf.
    auto histNextSegment = histLabel->getKthSegment(diffIndex + 1);   
    auto curNextSegment = curLabel->getKthSegment(diffIndex + 1);
    auto histNextType = histNextSegment->getType();
    auto curNextType = curNextSegment->getType();
    if (histNextType == eExplicit && curNextType == eExplicit) {
      // curLabel[diffIndex + 1] and histLabel[diffIndex + 1] are explicit task label segments.
      return analyzeExplicitTask(histLabel, curLabel, diffIndex, recordManagementInfo); 
    } 
    RAW_CHECK(!(histNextType == eImplicit && curNextType == eImplicit), "not expecting next level tasks are sibling implicit tasks");
    RAW_CHECK(!(histNextType == eLogical && curNextType == eLogical), "not expecting next level tasks are sibling logical tasks");
    RAW_CHECK(!(histNextType == eLogical && curNextType == eImplicit), "not expecting next level tasks are implicit task and logcial task combination");
    RAW_CHECK(!(histNextType == eImplicit && curNextType == eLogical), "not expecting next level tasks are implicit task and logcial task combination");
    // in the rest of the cases, there is no case of sibling explicit tasks. 
    recordManagementInfo.nodeRelation = eNonSiblingParallel;
    isHappensBefore = false; 
  }  
  return isHappensBefore; 
}



//  histLabel[diffIndex + 1] and curLabel[diffIndex + 1] are explicit task segment
bool analyzeExplicitTask(Label* histLabel, Label* curLabel, int diffIndex, RecordManagementInfo& recordManagementInfo) {
  // First check if ordered by task group construct   
  if (analyzeTaskGroupSync(histLabel, curLabel, diffIndex)) {
    recordManagementInfo.nodeRelation = eHappensBefore;
    return true;
  }
  // T1 = T(histLabel, diffIndex + 1) and T2 = T(curLabel, diffIndex + 1) are explicit tasks. Check if there exists
  // explicit task dependence specified between T1 and T2. If so, we further analyze whether T(histLabel) syncs with T1 and 
  // T(curLabel) syncs with T2. This checking should preempt other conditions.
  auto histNextSegment = static_cast<ExplicitTaskSegment*>(histLabel->getKthSegment(diffIndex + 1));
  auto curNextSegment = static_cast<ExplicitTaskSegment*>(curLabel->getKthSegment(diffIndex + 1));
  auto histNextTaskPtr = histNextSegment->getTaskPtr();
  auto curNextTaskPtr = curNextSegment->getTaskPtr();   
  ParallelRegionInfo parallelRegionInfo;
  RAW_CHECK(queryParallelRegionInfo(0, parallelRegionInfo) == true, "cannot get parallel region data");
  auto parallelRegionData = static_cast<ParallelRegionData*>(parallelRegionInfo.parallelData->ptr); 
  pfq_rwlock_node_t node;
#ifdef PERFORMANCE
  ReaderWriterLockGuard guard(&(parallelRegionData->lock), &node, &gPerformanceCounters); // needed in happens before analys
#else
  ReaderWriterLockGuard guard(&(parallelRegionData->lock), &node, nullptr);
#endif
  if (parallelRegionData->taskDependenceGraph.hasPath(histNextTaskPtr, curNextTaskPtr)) {
    return analyzeExplicitTaskSynchronizationWithTaskWait(histLabel, diffIndex + 2, recordManagementInfo) && analyzeExplicitTaskSynchronizationWithTaskWait(curLabel, diffIndex + 2, recordManagementInfo);
  } else {
    // There is no explicit task dependence between T1 and T2, we further check the synchronization enforced by taskwait
    auto histSegment = histLabel->getKthSegment(diffIndex);
    auto curSegment = curLabel->getKthSegment(diffIndex);
    auto histTaskwait = histSegment->getTaskwait();
    auto curTaskwait = curSegment->getTaskwait();
    if (histTaskwait == curTaskwait) { 
      auto histNextSegmentIsLeaf = (diffIndex + 1) == histLabel->getLabelLength();
      auto curNextSegmentIsLeaf = (diffIndex + 1) == curLabel->getLabelLength();  
      recordManagementInfo.nodeRelation = (histNextSegmentIsLeaf && curNextSegmentIsLeaf) ? eSiblingParallel : eNonSiblingParallel;
      return false; 
    } else if (histTaskwait < curTaskwait) {
      return analyzeExplicitTaskSynchronizationWithTaskWait(histLabel, diffIndex + 2, recordManagementInfo);
    } else {
      RAW_LOG(FATAL, "not expecting hist taskwait to be larger than cur taskwait");
      return false;
    }
  } 
  RAW_LOG(FATAL, "not expected to be executed");
  return false;
}

// TODO: revisit task group sync
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
// Return true if we need to rollback the calculation
bool manageAccessRecords(AccessHistory* accessHistory, const Record& currentRecord, ReaderWriterLockGuard& lockGuard, std::vector<RecordManagementInfo>& info) {
  auto records = accessHistory->getRecords();
  auto infoSize = info.size(); 
  auto recordsNum = records->size();
  RAW_CHECK(infoSize == recordsNum, "access records size is not equal to records number");
  std::vector<int> recordRemovalCandidates;
  recordRemovalCandidates.clear(); 
  // we define 4 combinations. Then we iterate over the record management info vector to count these values
  // the values will be used to determine if we could skip adding current record to the access history.
  auto histReadCurReadSiblingCurLockSetContainsHistLockSetCount = 0;
  auto histWriteCurReadSiblingCurLockSetContainsHistLockSetCount = 0;
  auto histWriteCurWriteSiblingCurLockSetContainsHistLockSetCount = 0;
  auto histReadCurWriteSiblingCurLockSetContainsHistLockSetCount = 0;
  auto canSkipAddingCurrentRecord = false;

  for (int i = 0; i < infoSize; ++i) {
    auto recordManagementInfo = info.at(i);
    auto historyRecord = records->at(i); 
    auto historyAccessIsWrite = historyRecord.isWrite();
    auto currentAccessIsWrite = currentRecord.isWrite();
    auto lockRelation = recordManagementInfo.lockRelation;
    auto historyLockSetContainsCurrentLockSet = lockRelation == eHistoryLockSetContainsCurrentLockSetNonEmpty || lockRelation == eBothEmptyLock || lockRelation == eCurrentNoLockHistoryHasLock;
    auto currentLockSetContainsHistoryLockSet = lockRelation == eCurrentLockSetContainsHistoryLockSetNonEmpty || lockRelation == eBothEmptyLock || lockRelation == eHistoryNoLockCurrentHasLock; 

    if (((historyAccessIsWrite && currentAccessIsWrite) || historyAccessIsWrite == false) && recordManagementInfo.nodeRelation == eHappensBefore && historyLockSetContainsCurrentLockSet) {
      recordRemovalCandidates.push_back(i); 
    } else {
      if (recordManagementInfo.nodeRelation == eSiblingParallel && currentLockSetContainsHistoryLockSet) {
        if (!historyAccessIsWrite && !currentAccessIsWrite) {
          histReadCurReadSiblingCurLockSetContainsHistLockSetCount += 1;
        } else if (historyAccessIsWrite && !currentAccessIsWrite) {
          // note that in this case at least a common lock is held 
          histWriteCurReadSiblingCurLockSetContainsHistLockSetCount += 1; 
        } else if (historyAccessIsWrite && currentAccessIsWrite) {
          histWriteCurWriteSiblingCurLockSetContainsHistLockSetCount += 1;
        } else if (!historyAccessIsWrite && currentAccessIsWrite) {
          histReadCurWriteSiblingCurLockSetContainsHistLockSetCount += 1;
        }
      }  
    }
    if ((histReadCurReadSiblingCurLockSetContainsHistLockSetCount >= 2 || 
        (histReadCurReadSiblingCurLockSetContainsHistLockSetCount > 0 && histWriteCurReadSiblingCurLockSetContainsHistLockSetCount > 0)) || 
        histWriteCurReadSiblingCurLockSetContainsHistLockSetCount >= 2 || 
        (histWriteCurReadSiblingCurLockSetContainsHistLockSetCount > 0 && histReadCurReadSiblingCurLockSetContainsHistLockSetCount > 0) || 
        histWriteCurWriteSiblingCurLockSetContainsHistLockSetCount >= 2 ||
        (histReadCurWriteSiblingCurLockSetContainsHistLockSetCount > 0 && histWriteCurWriteSiblingCurLockSetContainsHistLockSetCount > 0)) {
      canSkipAddingCurrentRecord = true; 
    } 
  }
  if (recordRemovalCandidates.size() > 0) {
    auto hasWriteWriteContention = lockGuard.upgradeFromReaderToWriter();
    if (!hasWriteWriteContention) {
      accessHistory->removeRecords(recordRemovalCandidates);
    } else {
      return true; // rolling back 
    }
#ifdef PERFORMANCE
    gPerformanceCounters.bumpNumAccessHistoryRemoveRecords();
#endif
  }
#ifdef PERFORMANCE
  if (canSkipAddingCurrentRecord) {
    gPerformanceCounters.bumpNumSkipAddingCurrentRecord(); 
  } 
#endif
  if (!canSkipAddingCurrentRecord) {
  // if we should not skip adding current record to the access history, we need to add it to the record no matter 
  // if there is write write contention or not. 
    auto hasWriteWriteContention = lockGuard.upgradeFromReaderToWriter();  
    if (!hasWriteWriteContention) {
      accessHistory->addRecordToAccessHistory(currentRecord);
      return false;
    } else {
      return true;
    }
  }
  return false;
}

// return true if there is data race. 
bool checkDataRaceForMemoryAddress(uint64_t checkedAddress, AccessHistory* accessHistory, const Record& currentRecord, std::vector<RecordManagementInfo>& info) {
  auto records = accessHistory->getRecords();
  auto dataRaceFound = false;
#ifdef PERFORMANCE
  uint64_t numAccessRecordsTraversed = 0;
#endif
  for (int i = 0; i < records->size(); ++i) { 
    auto histRecord = records->at(i);
    RecordManagementInfo recordManagementInfo;      
#ifdef PERFORMANCE
    numAccessRecordsTraversed += 1;
#endif
    if (analyzeRaceCondition(histRecord, currentRecord, recordManagementInfo)) {
      accessHistory->setFlag(eDataRaceFound); 
      dataRaceFound = true;
      break;
    }
    info.push_back(recordManagementInfo); 
  }
#ifdef PERFORMANCE
  gPerformanceCounters.bumpNumTotalAccessRecordsTraversed(numAccessRecordsTraversed); 
#endif
  return dataRaceFound;
}
