#include "Core.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>

#include "AccessControl.h"
#include "ParallelRegionData.h"
#include "TaskInfoQuery.h"
#include "ThreadData.h"

extern PerformanceCounters gPerformanceCounters;

bool analyzeRaceCondition(const Record& histRecord, const Record& curRecord, 
        bool& isHistBeforeCur, int& diffIndex, const uint64_t checkedAddress) {
  auto histLabel = histRecord.getLabel(); 
  auto curLabel = curRecord.getLabel(); 
  RAW_DLOG(INFO, "analyze race condition - address: %lx hist label: %s hist is write: %d cur label: %s cur is write: %d\n", checkedAddress, histLabel->toString().c_str(), histRecord.isWrite(), curLabel->toString().c_str(), curRecord.isWrite());
  if (analyzeMutualExclusion(histRecord, curRecord)) {
    return false;
  }  
  auto curTaskData = static_cast<TaskData*>(curRecord.getTaskPtr());
  if (curTaskData->inReduction) { 
    // current memory access is in reduction phase, we trust runtime library
    // that in this phase no data race is genereted by reduction method.
    RAW_DLOG(INFO, "current memory access is in reduction phase. memory address: %lx", checkedAddress);
    return false;
  }
  isHistBeforeCur = happensBefore(histLabel, curLabel, diffIndex);
  if (diffIndex == eRightIsPrefix) {
    return false;
  }
  if (!isHistBeforeCur) {
    // further check explicit task dependence if current task and history task 
    // are both explicit tasks. If no task dependence, return true
    auto histTaskData = static_cast<TaskData*>(histRecord.getTaskPtr()); 
    if (curTaskData->isExplicitTask && histTaskData->isExplicitTask) {
      // first check if the two tasks are mutex tasks
      if (curTaskData->isMutexTask && histTaskData->isMutexTask) { 
        return false; // mutex task does not form race condition
      }
      // have to get the associated parallel region
      ParallelRegionInfo parallelRegionInfo;
      if (!queryParallelRegionInfo(0, parallelRegionInfo)) {
        RAW_LOG(FATAL, "cannot get parallel region data");
      } 
      auto parallelRegionData= static_cast<ParallelRegionData*>(parallelRegionInfo.parallelData->ptr); 
      // have to lock the task dep graph before graph traversal
      mcs_node_t node;
#ifdef PERFORMANCE
      LockGuard guard(&(parallelRegionData->lock), &node, &gPerformanceCounters);
#else
      LockGuard guard(&(parallelRegionData->lock), &node);
#endif
      if (parallelRegionData->taskDepGraph.hasPath((void*)histTaskData, 
				 (void*)curTaskData)) {
         isHistBeforeCur = true;
	}
    }
  }
  return !isHistBeforeCur && (histRecord.isWrite() || curRecord.isWrite());
}

bool analyzeMutualExclusion(const Record& histRecord, const Record& curRecord) {
  auto histLockSet = histRecord.getLockSet(); 
  auto curLockSet = curRecord.getLockSet();  
  return histRecord.hasHardwareLock() && curRecord.hasHardwareLock() || 
         (histLockSet && curLockSet && histLockSet->hasCommonLock(*curLockSet));  
}


bool happensBefore(Label* histLabel, Label* curLabel, int& diffIndex) {
  diffIndex = compareLabels(histLabel, curLabel);
  if (diffIndex < 0) {
    switch(diffIndex) {
      case static_cast<int>(eSameLabel):
        return true;
      case static_cast<int>(eLeftIsPrefix):
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
        return true;
      case eLogical:
        return analyzeOrderedSection(histLabel, curLabel,  diffIndex);
      case eExplicit:
        return analyzeSameTask(histLabel, curLabel, diffIndex);
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
      return true; 
    } 
    return analyzeSiblingImplicitTask(histLabel, curLabel, diffIndex);
  } 
  return analyzeSameTask(histLabel, curLabel, diffIndex); 
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
  auto histExitRank = computeExitRank(histPhase);
  auto curEnterRank = computeEnterRank(curPhase);
  if (histExitRank < curEnterRank) {
    auto histLen = histLabel->getLabelLength();
    auto curLen = curLabel->getLabelLength();
    if (startIndex == histLen - 1) {
      return true;
    } 
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
          isHistBeforeCurrent && isSubset(curLockSet, histLockSet)) {
    return eDeleteHistoryRecord;  
  } else if (diffIndex == static_cast<int>(eSameLabel) && 
            ((!histIsWrite && !curIsWrite) || histIsWrite) && 
            isSubset(histLockSet, curLockSet)) {
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
