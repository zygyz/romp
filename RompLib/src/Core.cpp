#include "Core.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>

#include "AccessControl.h"
#include "ParallelRegionData.h"
#include "TaskInfoQuery.h"
#include "ThreadData.h"

extern PerformanceCounters gPerformanceCounters;

bool analyzeRaceCondition(const Record& histRecord, const Record& curRecord, bool& isHistBeforeCur, int& diffIndex, const uint64_t checkedAddress) {
  auto histTaskData = static_cast<TaskData*>(histRecord.getTaskPtr()); 
  auto curTaskData = static_cast<TaskData*>(curRecord.getTaskPtr());
//  RAW_DLOG(INFO, "checking data race on address: %lx, history task: %lx isExplicit: %d current task: %lx isExplicit: %d\n", (void*)checkedAddress, histTaskData, histTaskData->getIsExplicitTask(), curTaskData, curTaskData->getIsExplicitTask());
  if (histTaskData == curTaskData) {
    // both memory accesses are performed by the same task. 
    return false;
  }

  if (analyzeMutualExclusion(histRecord, curRecord)) {
    return false;
  }  
  if (curTaskData->getIsInReduction()) { 
    // current memory access is in reduction phase, we trust runtime library
    // that in this phase no data race is genereted by reduction method.
    // TODO: review this decision
    return false;
  }

  auto histLabel = histRecord.getLabel(); 
  auto curLabel = curRecord.getLabel(); 
  isHistBeforeCur = happensBefore(histLabel, curLabel, diffIndex, histTaskData, curTaskData);

  if (isHistBeforeCur || 
     curTaskData->getIsExplicitTask() && histTaskData->getIsExplicitTask() && curTaskData->getIsMutexTask() && histTaskData->getIsMutexTask()) {
    // either comparing the task label infers existence of happens-before relation, 
    // or two memory accesses are performed by two mutex tasks. In these cases, there is no data race.
    // there exists happens-before relationship between two memory accesses. No data race.
    return false; 
  } 
  auto hasDataRace = !isHistBeforeCur && (histRecord.isWrite() || curRecord.isWrite());
  if (hasDataRace) {
    RAW_DLOG(INFO, "data race found: hist is explicit: (taskPtr:%lx) %d cur is explicit: (taskPtr:%lx) %d", histTaskData, histTaskData->getIsExplicitTask(), curTaskData, curTaskData->getIsExplicitTask());
  }
  return hasDataRace;
}

bool analyzeMutualExclusion(const Record& histRecord, const Record& curRecord) {
  auto histLockSet = histRecord.getLockSet(); 
  auto curLockSet = curRecord.getLockSet();  
  return histRecord.hasHardwareLock() && curRecord.hasHardwareLock() || hasCommonLock(histLockSet, curLockSet);  
}


bool happensBefore(Label* histLabel, Label* curLabel, int& diffIndex, TaskData* histTaskData, TaskData* curTaskData) {
  diffIndex = compareLabels(histLabel, curLabel);
  auto histHappensBeforeCur = false;
  if (diffIndex < 0) {
    switch(diffIndex) {
      case static_cast<int>(eSameLabel):
        histHappensBeforeCur = true;
        break;
      case static_cast<int>(eLeftIsPrefix):
        histHappensBeforeCur = true;
        break;
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
        histHappensBeforeCur = true;
        break;
      case eLogical:
        histHappensBeforeCur = analyzeOrderedSection(histLabel, curLabel,  diffIndex);
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
          histHappensBeforeCur = true;
        }
      } else {
        // current task is implicit task, history task is explicit task, check if there exists order by undeferred task 
        // i.e., if current thread executing the implicit task has encountered some undeferred task before. And there exists
        // explicit task dependence between history explicit task and the undeferred task. In this case, there exists happens-before  relationship between history task and current task.
        for (auto undeferredTask : curTaskData->undeferredTasks) {
          RAW_DLOG(INFO, "undeferred task: %lx", undeferredTask);
          if (parallelRegionData->taskDependenceGraph.hasPath(static_cast<void*>(histTaskData), undeferredTask)) {
            histHappensBeforeCur = true;
            break; 
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

bool analyzeSameTask(Label* histLabel, Label* curLabel, int diffIndex) {
  auto lenHistLabel = histLabel->getLabelLength(); 
  auto lenCurLabel = curLabel->getLabelLength();
  if (diffIndex == (lenHistLabel - 1)) {
    return true;
  }      
  // T(histLabel, diffIndex) is not leaf task
  if (diffIndex == (lenCurLabel - 1)) {
    auto histNextSeg = histLabel->getKthSegment(diffIndex + 1);
    auto histNextType = histNextSeg->getType();
    RAW_CHECK(histNextType != eImplicit, 
            "not expecting next level task to be implicit task");
    if (histNextType == eExplicit) {
      // check if T(histLabel) happens before T(curLabel) because of explicit 
      // task synchronization
      auto histSeg = histLabel->getKthSegment(diffIndex);
      auto histTaskwait = histSeg->getTaskwait();
      auto curSeg = curLabel->getKthSegment(diffIndex);
      auto curTaskwait = curSeg->getTaskwait();
      RAW_CHECK(curTaskwait >= histTaskwait, "not expecting hist taskwait\
              to be larger than cur taskwait");
      if (curTaskwait == histTaskwait) {
        // futher check task group sync 
        auto histTaskGroupLevel = histSeg->getTaskGroupLevel();      
        if (histTaskGroupLevel > 0 && histNextSeg->isTaskGroupSync()) {
          // T(histLabel) happens before T(curLabel) only when the taskgroup 
          // construct wrapping T(histLabel,diffIndex + 1) finishes before 
          // T(curLabel, diffIndex)
          return true;
        }
        return false;
      } 
      return analyzeSyncChain(histLabel, diffIndex + 1); 
    } 
    if (histNextType == eLogical) {
      if (static_cast<WorkShareSegment*>(histNextSeg)->isWorkSharePlaceHolder()) {
        return true;
      }
      return false; 
    }
  } 
  // both T(histLabel, diffIndex) and T(curLabel, diffIndex) are not leaf task 
  auto histNextSeg = histLabel->getKthSegment(diffIndex + 1);
  auto curNextSeg = curLabel->getKthSegment(diffIndex + 1);
  auto histNextType = histNextSeg->getType();
  auto curNextType = curNextSeg->getType();
  RAW_CHECK(!(histNextType == eImplicit && curNextType == eImplicit),
            "not expecting next level tasks are sibling implicit tasks");
    // invoke different checking depending on next segment's type 
  if (histNextType == eExplicit && curNextType == eExplicit || histNextType == eExplicit && curNextType == eLogical) {
    return analyzeExplicitTask(histLabel, curLabel, diffIndex);
  }
  return false;
}


bool analyzeExplicitTask(Label* histLabel, Label* curLabel, int diffIndex) {
  // First check if ordered by task group construct   
  if (analyzeTaskGroupSync(histLabel, curLabel, diffIndex)) {
    return true;
  }
  auto histSeg = histLabel->getKthSegment(diffIndex);      
  auto curSeg = curLabel->getKthSegment(diffIndex);
  auto histTaskwait = histSeg->getTaskwait();
  auto curTaskwait = curSeg->getTaskwait();
  if (histTaskwait == curTaskwait) {
    return false;
  } else if (histTaskwait < curTaskwait) {
    // there is taskwait between creation of T(histLabel, diffIndex + 1) and 
    // T(curLabel, diffIndex + 1)  
    return analyzeSyncChain(histLabel, diffIndex + 1); 
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
