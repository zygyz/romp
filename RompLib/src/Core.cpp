#include "Core.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>

#include "AccessControl.h"
#include "ParallelRegionData.h"
#include "TaskData.h"
#include "TaskInfoQuery.h"
#include "ThreadData.h"

extern PerformanceCounters gPerformanceCounters;

bool analyzeRaceCondition(const Record& histRecord, const Record& curRecord, bool& isHistBeforeCur, int& diffIndex, const uint64_t checkedAddress) {
  auto histTaskData = static_cast<TaskData*>(histRecord.getTaskPtr()); 
  auto curTaskData = static_cast<TaskData*>(curRecord.getTaskPtr());
//if (histTaskData == curTaskData) {
//both memory accesses are performed by the same task. 
//    return false;
// }
  
  auto histRecordMemoryOwner = histRecord.getMemoryAddressOwner();
  auto curRecordMemoryOwner = curRecord.getMemoryAddressOwner(); 
  if (histRecordMemoryOwner != curRecordMemoryOwner) {
    return false;
  }

  if (analyzeMutualExclusion(histRecord, curRecord)) {
    return false;
  }  
   
  auto histLabel = histRecord.getLabel(); 
  auto curLabel = curRecord.getLabel(); 
  isHistBeforeCur = happensBefore(histLabel, curLabel, diffIndex, histTaskData, curTaskData);
  RAW_DLOG(INFO, "analzye Race condition: mem addr: %lx hist: %s cur %s isHistBeforeCur: %d", checkedAddress, histLabel->toString().c_str(), curLabel->toString().c_str(), isHistBeforeCur);
  if (isHistBeforeCur) {
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

  if ((histRecord.isInReduction() || curRecord.isInReduction()) && histTaskData->parallelRegionDataPtr == curTaskData->parallelRegionDataPtr) {
    // both accesses are in reduction in the same work share region, no data race.
    RAW_DLOG(INFO, "mem addr: %lx, hist in reduction: %d cur in reduction: %d, hist laebl: %lx cur label: %lx", checkedAddress, histRecord.isInReduction(), curRecord.isInReduction(), histLabel->toString().c_str(), curLabel->toString().c_str());
    return false; 
  }
   
  auto currentDataSharingType = curRecord.getDataSharingType();
  auto historyDataSharingType = histRecord.getDataSharingType();
  auto bothAccessesAreTaskPrivate = ((currentDataSharingType == eThreadPrivateAccessCurrentTask || currentDataSharingType == eExplicitTaskPrivate) && (historyDataSharingType == eThreadPrivateAccessCurrentTask || historyDataSharingType == eExplicitTaskPrivate));
  auto hasDataRace = !isHistBeforeCur && (histRecord.isWrite() || curRecord.isWrite()) && !bothAccessesAreTaskPrivate;
  return hasDataRace;
}

bool analyzeMutualExclusion(const Record& histRecord, const Record& curRecord) {
  auto histLockSet = histRecord.getLockSet(); 
  auto curLockSet = curRecord.getLockSet();  
  return histRecord.hasHardwareLock() && curRecord.hasHardwareLock() || hasCommonLock(histLockSet, curLockSet);  
}

// assuming proper concurrency control for access history
void* setMemoryOwner(AccessHistory* accessHistory, int dataSharingType, void* taskData, void* memoryAddress) {
  if (dataSharingType == eThreadPrivateAccessCurrentTask || dataSharingType == eExplicitTaskPrivate) {
    accessHistory->setOwner(taskData);
    return taskData;
  }
  return accessHistory->getOwner();
}

bool happensBefore(Label* histLabel, Label* curLabel, int& diffIndex, TaskData* histTaskData, TaskData* curTaskData) {
  diffIndex = compareLabels(histLabel, curLabel);
  auto histHappensBeforeCur = false;
  if (diffIndex < 0) {
    switch(diffIndex) {
      case static_cast<int>(eSameLabel):
        return true;
      case static_cast<int>(eLeftIsPrefix):
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
    RAW_CHECK(histType == curType, "not expecting hist seg type\
            != cur seg type");
    switch(histType) {
      case eImplicit:
        return true;
      case eLogical:
        histHappensBeforeCur = analyzeOrderedSection(histLabel, curLabel,  diffIndex); // TODO: is it right?
        break;
      case eExplicit:
        histHappensBeforeCur = analyzeSameTask(histLabel, curLabel, diffIndex);
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
      histHappensBeforeCur = true;
    } else {
      histHappensBeforeCur = analyzeSiblingImplicitTask(histLabel, curLabel, diffIndex);
    }
  }  else {
    histHappensBeforeCur = analyzeSameTask(histLabel, curLabel, diffIndex); 
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
      mcs_node_t node;
#ifdef PERFORMANCE
      LockGuard guard(&(parallelRegionData->lock), &node, &gPerformanceCounters);
#else
      LockGuard guard(&(parallelRegionData->lock), &node, nullptr);
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

bool analyzeSiblingImplicitTask(Label* histLabel, Label* curLabel, int diffIndex) { 
  auto lenHistLabel = histLabel->getLabelLength();
  auto lenCurLabel = curLabel->getLabelLength();
  if (diffIndex == (lenHistLabel - 1) || diffIndex == (lenCurLabel - 1)) {
    // if any one if T(histLabel) and T(curLabel) is leaf implicit task, 
    // we are sure there is no happens-beofre relationship
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
      return false;
    } 
    auto histSeg = histLabel->getKthSegment(diffIndex);
    auto curSeg = curLabel->getKthSegment(diffIndex);
    auto histSegLoopCount = histSeg->getLoopCount();
    auto curSegLoopCount = curSeg->getLoopCount();
    if (histSegLoopCount == curSegLoopCount) {
      return analyzeOrderedSection(histLabel, curLabel, diffIndex + 1);
    } 
    return false;
  }
  return false;
}

// startIndex-1 points to the first pair of different index
bool analyzeOrderedSection(Label* histLabel, Label* curLabel, int startIndex) {
  auto histBaseSeg  = histLabel->getKthSegment(startIndex);
  auto curBaseSeg = curLabel->getKthSegment(startIndex);
  auto histSegment = static_cast<WorkShareSegment*>(histBaseSeg);
  auto curSegment = static_cast<WorkShareSegment*>(curBaseSeg);
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
      return analyzeOrderedDescendants(histLabel, startIndex, histPhase);
    }
  } else if (leftPhase > rightPhase) {
    return false;
  } else { // leftPhase < rightPhase
    return analyzeOrderedDescendants(histLabel, startIndex, histPhase);
  }
  return false;
}

bool analyzeOrderedDescendants(Label* histLabel, int startIndex, 
        uint64_t histPhase) {
  auto nextSeg = histLabel->getKthSegment(startIndex + 1);
  auto nextSegType = nextSeg->getType();
  if (nextSegType == eImplicit) {
    // we know that implicit task syncs with its parent task
    return true;
  } 
  if (nextSegType == eLogical) {
    RAW_LOG(FATAL, "does not expect next segment of workshare seg to be \
           workshare segment"); 
    return false;
  }
  if (nextSegType == eExplicit) {
    auto curSeg = histLabel->getKthSegment(startIndex);
    auto taskGroupLevel = curSeg->getTaskGroupLevel();
    if (taskGroupLevel > 0) {
      auto phase = curSeg->getPhase();  
      if (phase % 2 == 0 && nextSeg->isTaskGroupSync() && 
              nextSeg->getTaskGroupPhase() <= histPhase) {
        return true;
      } 
      if (phase % 2 == 1) {
        return true;
      } 
    }
    if (nextSeg->isTaskwaited() && nextSeg->getTaskwaitPhase() <= histPhase) {
      return analyzeSyncChain(histLabel, startIndex + 1);
    }
    return false;
  }
  return false;
}

bool analyzeSyncChain(Label* label, int startIndex) {
  auto lenLabel = label->getLabelLength(); 
  if (startIndex == lenLabel - 1) {
    // already the leaf task
    return true;
  }
  for (auto i = startIndex; i < lenLabel; ++i) {
    auto seg = label->getKthSegment(i);
    auto segType = seg->getType();
    if (segType == eImplicit) {
      return true;
    } else if (segType == eExplicit) {
      auto taskGroupLevel = seg->getTaskGroupLevel();    
      if (taskGroupLevel > 0) {
        // taskgroup guarantees completion of descendants
        return true;
      } else {
        if (!seg->isTaskwaited()) { 
          // if current explicit task T(label, i) is not waited 
          // by parent task, no sync.
          return false;
        }
      }
    }
  }
  return true;
}


// This function is called under the premise that offset field is the same.
// There exists fields in histLabel[diffIndex] and curLabel[diffIndex] that are different.
// Return true if there exists happens-before relationship. Return false otherwise.
bool analyzeSameTask(Label* histLabel, Label* curLabel, int diffIndex) {
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
      return histWorkShareID == curWorkShareID;
    } 
    // otherwise, should be same implicit task at different phases, has happens-before relationship.
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
        return true;  
      } else {
        return false;
      }
    } else {
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
          isHappensBefore = true; 
        } else {
          isHappensBefore = false;
        }
      } else {
        return analyzeSyncChain(histLabel, diffIndex + 1);
      }   
    } else if (histNextType == eLogical) {
      if (static_cast<WorkShareSegment*>(histNextSegment)->isWorkSharePlaceHolder()) {
        isHappensBefore = true;
      } else {
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
      return analyzeExplicitTask(histLabel, curLabel, diffIndex); 
    } 
    RAW_CHECK(!(histNextType == eImplicit && curNextType == eImplicit), "not expecting next level tasks are sibling implicit tasks");
    RAW_CHECK(!(histNextType == eLogical && curNextType == eLogical), "not expecting next level tasks are sibling logical tasks");
    RAW_CHECK(!(histNextType == eLogical && curNextType == eImplicit), "not expecting next level tasks are implicit task and logcial task combination");
    RAW_CHECK(!(histNextType == eImplicit && curNextType == eLogical), "not expecting next level tasks are implicit task and logcial task combination");
    // in the rest of the cases, there is no case of sibling explicit tasks. 
    isHappensBefore = false; 
  }  
  return isHappensBefore; 
}



//  histLabel[diffIndex + 1] and curLabel[diffIndex + 1] are explicit task segment
bool analyzeExplicitTask(Label* histLabel, Label* curLabel, int diffIndex) {
  // First check if ordered by task group construct   
  if (analyzeTaskGroupSync(histLabel, curLabel, diffIndex)) {
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
  mcs_node_t node;
#ifdef PERFORMANCE
  LockGuard guard(&(parallelRegionData->lock), &node, &gPerformanceCounters);
#else
  LockGuard guard(&(parallelRegionData->lock), &node, nullptr);
#endif
  if (parallelRegionData->taskDependenceGraph.hasPath(histNextTaskPtr, curNextTaskPtr)) {
    return analyzeSyncChain(histLabel, diffIndex + 2) && analyzeSyncChain(curLabel, diffIndex + 2); 
  } else {
    // There is no explicit task dependence between T1 and T2, we further check the synchronization enforced by taskwait
    auto histSegment = histLabel->getKthSegment(diffIndex);
    auto curSegment = curLabel->getKthSegment(diffIndex);
    auto histTaskwait = histSegment->getTaskwait();
    auto curTaskwait = curSegment->getTaskwait();
    if (histTaskwait == curTaskwait) { 
      return false; 
    } else if (histTaskwait < curTaskwait) {
      return analyzeSyncChain(histLabel, diffIndex + 1); 
    } else {
      RAW_LOG(FATAL, "not expecting hist taskwait to be larger than cur taskwait");
      return false;
    }
  } 
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
  return (histTaskGroupId < curTaskGroupId) && (histTaskGroupLevel >= 
          curTaskGroupLevel);
}

uint64_t computeExitRank(uint64_t phase) {
  return phase - (phase % 2); 
}

uint64_t computeEnterRank(uint64_t phase) {
  return phase + (phase % 2);
}

AccessHistoryManagementDecision manageAccessRecord(const Record& histRecord, 
                                    const Record& curRecord,
                                    bool isHistBeforeCurrent,
                                    int diffIndex) {
  auto histIsWrite = histRecord.isWrite();  
  auto curIsWrite = curRecord.isWrite();
  auto histLockSet = histRecord.getLockSet();
  auto curLockSet = curRecord.getLockSet();
  if (((histIsWrite && curIsWrite) || !histIsWrite) && 
          isHistBeforeCurrent && isSubSet(curLockSet, histLockSet)) {
    return eDeleteHistoryRecord;  
  } else if (diffIndex == static_cast<int>(eSameLabel) && 
            ((!histIsWrite && !curIsWrite) || histIsWrite) && 
            isSubSet(histLockSet, curLockSet)) {
    return eSkipAddCurrentRecord; 
  } 
  return eNoOperation;
} 

void modifyAccessHistory(AccessHistoryManagementDecision decision, 
                         std::vector<Record>* records,
                         std::vector<Record>::iterator& it) {
  if (decision == eDeleteHistoryRecord) {
    it = records->erase(it);
  } else {
    it++;
  }
}
