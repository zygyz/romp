#include "Core.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>

#include "ParRegionData.h"
#include "QueryFuncs.h"

namespace romp {
/*
 * Note: comments for functions contain some notation about task and its 
 * label. We describe the notation in this comment here to clarify and to ease
 * the description in each comment.
 * T(L, index): the task represented by the task label, L', which contains 
 * prefix of label segments in label `L` up to index: L' = L[0:index]
 *
 * T(L): the task represented by the task label `L`
 *
 * Descendant tasks: all tasks spawned in the current task's execution context
 *
 */

/*
 * This function is a driver function that analyzes race condition between two
 * memory accesses.  Return true if there is race condition between the two 
 * accesses. Return true if there is race condition
 */
bool analyzeRaceCondition(const Record& histRecord, const Record& curRecord, 
        bool& isHistBeforeCur, int& diffIndex) {
  auto histLabel = histRecord.getLabel(); 
  auto curLabel = curRecord.getLabel(); 
  if (analyzeMutualExclusion(histRecord, curRecord)) {
    return false;
  }  
  auto curTaskData = static_cast<TaskData*>(curRecord.getTaskPtr());
  if (curTaskData->inReduction) { 
    // current memory access is in reduction phase, we trust runtime library
    // that in this phase no data race is genereted by reduction method.
    return false;
  }
  isHistBeforeCur = happensBefore(histLabel, curLabel, diffIndex);
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
      auto teamSize = 0;
      void* parallelDataPtr = nullptr;
      if (!queryParallelInfo(0, teamSize, parallelDataPtr)) {
        RAW_LOG(WARNING, "cannot get parallel region data");
      } else {
        auto parallelData = static_cast<ParRegionData*>(parallelDataPtr); 
        // have to lock the task dep graph before graph traversal
	McsNode node;
	LockGuard guard(&(parallelData->lock), &node);
        if (parallelData->taskDepGraph.hasPath((void*)histTaskData, 
				 (void*)curTaskData)) {
          isHistBeforeCur = true;
	}
      }
    }
  }
  return !isHistBeforeCur && (histRecord.isWrite() || curRecord.isWrite());
}

/*
 * This function analyzes mutual exclusion between memory access recorded in 
 * histRecord and memory access recorded in curRecord.
 * Return true if there is mutual exclusion; Return false otherwise.
 */
bool analyzeMutualExclusion(const Record& histRecord, const Record& curRecord) {
  auto histLockSet = histRecord.getLockSet(); 
  auto curLockSet = curRecord.getLockSet();  
  if (histLockSet == nullptr || curLockSet == nullptr) {
    return false;
  }
  return histLockSet->hasCommonLock(*curLockSet) || 
           (histRecord.hasHwLock() && curRecord.hasHwLock());
}


/*
 * This function analyzes the happens-before relationship between two memory
 * accesses based on their associated task labels. The idea is that task label
 * encodes nodes relationship in openmp task graph. If there exists a directed
 * path from node A to node B, node A `happens-before` node B. Otherwise, node
 * A is logically concurrent with node B. Note that instead of doing explicit 
 * graph traversal, we traverse and compare label segments. 
 * `hsitLabel`: task label denoting a task recorded in the access history
 * `curLabel`: task label denoting the current task
 * `diffIndex`: the index of first pair of different segment, which is the 
 * result of `compareLabel` function.
 * Return true if hist task happens before current task 
 * Return false if hist task is logically concurrent with current task
 * Issue fatal warning if current task happens before hist task.
 */
bool happensBefore(Label* histLabel, Label* curLabel, int& diffIndex) {
  diffIndex = compareLabels(histLabel, curLabel);
  if (diffIndex < 0) {
    switch(diffIndex) {
      case static_cast<int>(eSameLabel):
        return true;
      case static_cast<int>(eLeftIsPrefix):
        return true;
      case static_cast<int>(eRightIsPrefix):
        RAW_LOG(FATAL, "cur Label: %s -> hist label: %s", 
                curLabel->toString().c_str(), histLabel->toString().c_str());
        return false;
      default:
        RAW_LOG(FATAL, "unknown label compare result");
        return false;
    }
  }
  // `diffIndex` points to the first pair of different label segment
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
        /* 
         * T(histLabel, diffIndex) and T(curLabel, diffIndex) are both the root
         * task, T(curLabel) should have encountered a barrier counstruct
         */
        RAW_CHECK(histOffset < curOffset, "not expecting hist offset >=\
                cur offset");
        return true;
      case eWorkShare:
        if (static_cast<WorkShareSegment*>(histSegment)->isSingleExecutor() && 
            static_cast<WorkShareSegment*>(curSegment)->isSingleExecutor()) { 
          return analyzeSameTask(histLabel, curLabel, diffIndex);
        } else {
          return analyzeOrderedSection(histLabel, curLabel,  diffIndex);
        }
      case eExplicit:
        /*
         * T(histLabel, diffIndex) and T(curLabel, diffIndex) are both explicit
         * tasks. In this case, they are the same explicit task. If the segment
         * is different, 
         */
        return analyzeSameTask(histLabel, curLabel, diffIndex);
      default:
        break;
    }
    RAW_LOG(FATAL, "unexpected segment type: %d", histType);
    return false;
  } else { // left span == right span and span > 1, implicit task
    if (histOffset != curOffset) { 
      auto span = histSpan;
      if (histOffset % span == curOffset % span) {
        RAW_CHECK(histOffset < curOffset, "not expecting history access joined \
                before current access");
        /* 
         * Any possible descendent tasks of T(histLabel, diffIndex) 
         * should have joined with T(histLabel, diffIndex). And they 
         * should happen before any descendent tasks of T(curLabel, diffIndex)
         */
        return true; 
      } else {
        return analyzeSiblingImpTask(histLabel, curLabel, diffIndex);
      }
    } else { 
      return analyzeSameTask(histLabel, curLabel, diffIndex); 
    }
  }
}

/*
 * This function analyzes happens-before relation when first pair of different 
 * segments are implicit segment, where offset are different and offset%span 
 * are different. This means the T(histLabel, diffIndex) and 
 * T(curLabel, diffIndex) are two sibling implicit tasks in the same parallel 
 * region. T(histLabel) and T(curLabel) are descendent tasks of 
 * T(histLabel, diffIndex), T(curLabel, diffIndex) respectively.
 *
 * Return true if T(histLabel) -> T(curLabel)
 * Return false if T(histLabel) || T(curLabel)
 * Issue fatal warning if T(curLabel) -> T(histLabel)
 *
 */
bool analyzeSiblingImpTask(Label* histLabel, Label* curLabel, int diffIndex) { 
  auto lenHistLabel = histLabel->getLabelLength();
  auto lenCurLabel = curLabel->getLabelLength();
  if (diffIndex == (lenHistLabel - 1) || diffIndex == (lenCurLabel - 1)) {
    // if any one if T(histLabel) and T(curLabel) is leaf implicit task, 
    // we are sure there is happens-beofre relationship
    return false;
  }
  // now diffIndex + 1 must not be out of boundary
  auto histNextSeg = histLabel->getKthSegment(diffIndex + 1);  
  auto curNextSeg = curLabel->getKthSegment(diffIndex + 1);
  auto histNextSegType = histNextSeg->getType();
  auto curNextSegType = curNextSeg->getType();
  /* Task dependency applies to sibling tasks, which are child tasks in a 
   * task region. We already know that T(histLabel) and T(curLabel) can
   * not be sibling tasks.
   */
  if (histNextSegType == eWorkShare && curNextSegType == eWorkShare) {
    // in this case, it is possible to be ordered with ordered section
    if (static_cast<WorkShareSegment*>(histNextSeg)->isSection() || 
            static_cast<WorkShareSegment*>(curNextSeg)->isSection()) {
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

/*
 * This function analyzes if Task(histLabel) is ordered by ordered section 
 * with Task(curLabel). T(histLabel, startIndex) and T(curLabel, startIndex) 
 * are workshare task.
 */
bool analyzeOrderedSection(Label* histLabel, Label* curLabel, int startIndex) {
  auto histBaseSeg  = histLabel->getKthSegment(startIndex);
  auto curBaseSeg = curLabel->getKthSegment(startIndex);
  auto histSegment = static_cast<WorkShareSegment*>(histBaseSeg);
  auto curSegment = static_cast<WorkShareSegment*>(curBaseSeg);
  if (histSegment->isPlaceHolder() || curSegment->isPlaceHolder()) {
    // have not entered the workshare construct yet.
    return false;
  } 
  auto histWorkShareId = histSegment->getWorkShareId();
  auto curWorkShareId = curSegment->getWorkShareId(); 
  auto histPhase = histBaseSeg->getPhase();
  auto curPhase = curBaseSeg->getPhase();
  auto histExitRank = computeExitRank(histPhase);
  auto curEnterRank = computeEnterRank(curPhase);
  if (histExitRank < curEnterRank) {
    auto histLen = histLabel->getLabelLength();
    auto curLen = curLabel->getLabelLength();
    if (startIndex == histLen - 1) {
      /* T(histLabel, startIndex) is leaf task, while T(curLabel) is descendent
       * task of T(curLabel. startIndex), ordered section has imposed happens
       * before relation in this case
       */
      return true;
    } else {
      /*
       * T(histLabel) is the descendent task of T(histLabel, startIndex)
       */
      return analyzeOrderedDescendents(histLabel, startIndex, histPhase);
    } 
  }
  return false;
}

/*
 * This function analyzes if T(histLabel) sync with T(histLabel, startIndex) 
 * and thus is ordered by ordered section. 
 *
 * Return true if T(histLabel) sync with T(histLabel, startIndex) within the 
 * effect of ordered section
 * Return false if T(histLabel) does not sync with ordered section
 */
bool analyzeOrderedDescendents(Label* histLabel, int startIndex, 
        uint64_t histPhase) {
  auto nextSeg = histLabel->getKthSegment(startIndex + 1);
  auto nextSegType = nextSeg->getType();
  if (nextSegType == eImplicit) {
    // we know that implicit task syncs with its parent task
    return true;
  } else if (nextSegType == eWorkShare) {
    RAW_LOG(FATAL, "does not expect next segment of workshare seg to be \
           workshare segment"); 
    return false;
  } else if (nextSegType == eExplicit) {
    /*
     * Explicit task does not interact with ordered section. i.e., if an
     * explicit task is created inside an ordered section, the finish 
     * of ordered section does not sync the explicit task. Here the idea 
     * is that since we have already moved to next iteration of ordered 
     * section, T(histLabel, startIndex+1) is an explicit task, and 
     * T(histLabel) is its descendent task. If T(histLabel) does sync by
     * the ordered section, there must be some explicit tasking sync 
     * applied (e.g., taskwait, taskgroup).
     */ 
    auto curSeg = histLabel->getKthSegment(startIndex);
    auto taskGroupLevel = curSeg->getTaskGroupLevel();
    if (taskGroupLevel > 0) {
      auto phase = curSeg->getPhase();  
      if (phase % 2 == 0 && nextSeg->isTaskGroupSync() && 
              nextSeg->getTaskGroupPhase() <= histPhase) {
      /* 
       * If phase is even, it is out of the ordered section scope.
       * T(histLabel, startIndex + 1) is out of ordered section scope
       * In this case, taskgroup construct's scope could be wrapping the 
       * ordered section's lexical scope. Thus taskgroup construct does
       * not necessarily guarantees T(histLabel) is in sync.
       */
        return true;
      } else if (phase % 2 == 1) {
      /* T(histLabel, startIndex + 1) is in ordered section scope
       * In this case, taskgroup construct would be confined in the ordered 
       * section's lexical scope. So if T(histLabel, startIndex) has the 
       * taskgroup construct, T(histLabel) should be in sync.
       */
        return true;
      } 
    }
    if (nextSeg->isTaskwaited() && nextSeg->getTaskwaitPhase() <= histPhase) {
      /* explicit task T(histLabel, startIndex+1) is sync with taskwait clause, 
       * we need to check the phase to make sure taskwait is not after the ordered
       * section. If taskwait is not after the ordered section, check further 
       * down the task creation chain
       */
      return analyzeSyncChain(histLabel, startIndex + 1);
    }
    return false;
  }
  return false;
}

/* 
 * This function analyzes if T(label) is in sync with T(label, startIndex).
 * i.e., T(label, startIndex)'s completion guarantees the completion of 
 * T(label)
 *
 * Return ture if T(label) is in sync with T(label, startIndex)
 * Return false otherwise.
 */
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
        // taskgroup guarantees completion of descendents
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


/*
 * This function analyzes happens-before relation when first pair of different 
 * segments are implicit segment, explicit task, or single workshare task, where
 * offset are the same. This means that T(histLabel, diffIndex) and 
 * T(curLabel, diffIndex) are the same implicit task, or explicit task, or 
 * single task, denote it as T'. T(histLabel), T(curLabel) are descendent tasks 
 * of T'.
 *
 * Return true if T(histLabel) -> T(curLabel)
 * Return false if T(histLabel) || T(curLabel)
 * Issue fatal warning if T(curLabel) -> T(histLabel)
 */
bool analyzeSameTask(Label* histLabel, Label* curLabel, int diffIndex) {
  auto lenHistLabel = histLabel->getLabelLength(); 
  auto lenCurLabel = curLabel->getLabelLength();
  if (diffIndex == (lenHistLabel - 1)) {
    /*
     * T(histLabel, diffIndex) == T(histLabel), which is leaf task. 
     * In this case, it is only possible to have T(histLabel) happens before
     * T(curLabel)
     */
    return true;
  }      
  // T(histLabel, diffIndex) is not leaf task
  if (diffIndex == (lenCurLabel - 1)) {
    /*
     * T(curLabel, diffIndex) is leaf task, while T(histLabel) is descendent
     * task of T(histLabel, diffIndex). We assert that T(histLabel, diffIndex+1)
     * is not implicit task. Because otherwise, T(curLabel, diffIndex) must be
     * the implicit task after join of the parallel region.
     */
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
        } else {
          return false;
        }
      } else {
        // curTaskwait > histTaskwait
        return analyzeSyncChain(histLabel, diffIndex + 1); 
      }
    } else if (histNextType == eWorkShare) {
      /*
       * T(histLabel, diffIndex + 1) is workshare task. As descendent task, 
       * T(histLabel) is logically concurrent with T(curLabel) even with 
       * ordered section depending on the scheduling of the workshare work.
       * Be careful when histLabel[diffIndex+1] is place holder segment, 
       * in this case, happens-before relation hold
       */ 
      if (static_cast<WorkShareSegment*>(histNextSeg)->isPlaceHolder()) {
        return true;
      }
      return false; 
    }
  } else {
    // both T(histLabel, diffIndex) and T(curLabel, diffIndex) are not leaf task 
    auto histNextSeg = histLabel->getKthSegment(diffIndex + 1);
    auto curNextSeg = curLabel->getKthSegment(diffIndex + 1);
    auto histNextType = histNextSeg->getType();
    auto curNextType = curNextSeg->getType();
    RAW_CHECK(!(histNextType == eImplicit && curNextType == eImplicit),
            "not expecting next level tasks are sibling implicit tasks");
    // invoke different checking depending on next segment's type 
    auto checkCase = buildCheckCase(histNextType, curNextType);
    return dispatchAnalysis(checkCase, histLabel, curLabel, diffIndex);
  }
  return false;
}

/* 
 * This function analyzes case when T(histLabel, diffIndex) and 
 * T(curLabel, diffIndex) are the same implicit task, T'. T(histLabel) and 
 * T(curLabel) are descendent tasks of T'. T(histLabel, diffIndex+1) is 
 * implicit task, T(curLabel, diffIndex + 1) is explicit task. 
 * In this case, we assume that task create count at histLabel[diffIndex] 
 * is smaller than task create count at curLabel[diffIndex]. And 
 * T(histLabel) must be in parallel with T(curLabel). Because if implicit
 * task T(histLabel, diffIndex + 1) is created first, the explicit task
 * T(curLabel, diffIndex + 1) can only be created after the end of parallel
 * region with T(histLabel, diffIndex+1). Then the offset field in
 * histLabel[diffIndex] and curLabel[diffIndex] should have been different.
 */
bool analyzeNextImpExp(Label* histLabel, Label* curLabel, int diffIndex) {
#ifdef DEBUG_CORE
  // checking like this is to make sure label segments meet our expectation
  // it could be eliminated in production 
  auto histSeg = histLabel->getKthSegment(diffIndex);
  auto curSeg = curLabel->getKthSegment(diffIndex);
  auto histTaskcreate = histSeg->getTaskcreate();
  auto curTaskcreate = curSeg->getTaskcreate();
  RAW_CHECK(histTaskcreate > curTaskcreate, "unexpecting hist task create \
         count <= cur task create count");
#endif
  return false;
}

/*
 * This function analyzes case when T(histLabel, diffIndex) and 
 * T(curLabel, diffIndex) are the same implicit task, T'. T(histLabel) and
 * T(curLabel) are descendent tasks of T'. T(histLabel, diffIndex+1) is
 * implicit task, T(curLabel, diffIndex+1) is workshare task. The workshare
 * task must be created first (If the implicit task is created first, it has
 * to be joined before creating the workshare task, then the offset field in 
 * histLabel[diffIndex] and curLabel[diffIndex] would be different) and does 
 * not encounter the implicit barrier because of the nowait clause (If there 
 * is no nowait clause, the implicit barrier would have made the offset field 
 * in histLabel[diffIndex], curLabel[diffIndex] different). 
 */
bool analyzeNextImpWork(Label* histLabel, Label* curLabel, int diffIndex) {
#ifdef DEBUG_CORE
  auto histSeg = histLabel->getKthSegment(diffIndex);
  auto curSeg = curLabel->getKthSegment(diffIndex);
  uint64_t histLoopCnt, curLoopCnt;
  auto histLoopCnt = histSeg->getLoopCount();
  auto curLoopCnt = curSeg->getLoopCount();
  RAW_CHECK(histLoopCnt > curLoopCnt, "unexpecting hist loop count <= cur loop\
          count");
#endif
  return false;
}

/*
 * This function analyes case when T(histLabel, diffIndex) and 
 * T(curLabel, diffIndex) are the same implicit task, T'. T(histLabel) and 
 * T(curLabel) are descendent tasks of T'. T(histLabel, diffIndex+1) is
 * explicit task, T(curLabel, diffIndex+1) is implicit task. For the same
 * reason described in comment of `analyeNextImpWork`, implicit task 
 * T(curLabel, diffIndex+1) must not be created before explicit task
 * T(histLabel, diffIndex+1). 
 */
bool analyzeNextExpImp(Label* histLabel, Label* curLabel, int diffIndex) {
#ifdef DBEUG_CORE
  auto histSeg = histLabel->getKthSegment(diffIndex);
  auto curSeg = curLabel->getKthSegment(diffIndex);
  auto histTaskcreate = histSeg->getTaskcreate();
  auto curTaskcreate = curSeg->getTaskcreate();
  RAW_CHECK(curTaskcreate > histTaskcreate, "unexpecting cur task create \
          count <= hist task create count");
#endif
  return false;
}

/*
 * This function analyzes case when T(histLabel, diffIndex) and 
 * T(curLabel, diffIndex) are the same implicit task or single workshare
 * task, T'. T(histLabel) and T(curLabel) are descendent tasks of T'. 
 * T(histLabel, diffIndex+1) is explicit task, T(curLabel, diffIndex+1) is 
 * also explicit task. Check syncrhonization that could affect the 
 * happens-before relation. e.g., taskwait, taskgroup. Note that we treat 
 * explicit task dependency as an extra syncrhonization imposed on tasks and 
 * is checked separately
 *
 * Return true if T(histLabel) happens before T(curLabel) 
 * Return false otherwise.
 */
bool analyzeNextExpExp(Label* histLabel, Label* curLabel, int diffIndex) {
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

/*
 * This function analyzes if T(histLabel) and T(curLabel) are synchronized by 
 * task group construct. In this case, T(histLabel, diffIndex) and 
 * T(curLabel, diffIndex) are the same implicit task or worksahre single task.
 * While T(histLabel, diffIndex + 1) are T(curLabel, diffIndex + 1) are explicit
 * tasks. 
 */
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
/*
 * This function analyzes case when T(histLabel, diffIndex) and 
 * T(curLabel, diffIndex) are the same implicit task, T'. T(histLabel) and 
 * T(curLabel) are descendent tasks of T'. T(histLabel, diffIndex + 1) is 
 * explicit task, T(curLabel, diffIndex + 1) is workshare task.
 * The intereseting part is that since T(histLabel, diffIndex+1) is explicit
 * task, T(histLabel) happens before T(curLabel) only when T(histLabel) 
 * finishes before T(histLabel, diffIndex + 1), while T(histLabel, diffIndex+1)
 * finishes before T(curLabel, diffIndex + 1). This means that the analysis
 * is the same as the one for analyzeNextExpExp()
 */
bool analyzeNextExpWork(Label* histLabel, Label* curLabel, int diffIndex) {
  return analyzeNextExpExp(histLabel, curLabel, diffIndex);
}

/*
 * This function analyzes case when T(histLabel, diffIndex) and 
 * T(curLabel, diffIndex) are the same implicit task, T'. T(histLabel) and
 * T(curLabel) are descendent tasks of T'. T(histLabel, diffIndex+1) is
 * workshare task, T(curLabel, diffIndex+1) is implicit task. The workshare
 * task must be created first (If the implicit task is created first, it has
 * to be joined before creating the workshare task, then the offset field in 
 * histLabel[diffIndex] and curLabel[diffIndex] would be different) and does 
 * not encounter the implicit barrier because of the nowait clause (If there 
 * is no nowait clause, the implicit barrier would have made the offset field 
 * in histLabel[diffIndex], curLabel[diffIndex] different). 
 * Since T(histLabel) is descendent task of T(histLabel, diffIndex + 1),
 * there is no happens before relationship.
 */
bool analyzeNextWorkImp(Label* histLabel, Label* curLabel, int diffIndex) {
  return false; 
}


/*
 * This function analyzes case when T(histLabel, diffIndex) and 
 * T(curLabel, diffIndex) are the same implicit task, T'. T(histLabel) and 
 * T(curLabel) are descendent tasks of T'. T(histLabel, diffIndex+1) is 
 * workshare task, T(curLabel, diffIndex + 1) is explicit task. 
 * The reasoning is similar to the one in `analyzeNextWorkImp`
 */
bool analyzeNextWorkExp(Label* histLabel, Label* curLabel, int diffIndex) {
  return false;  
}

/*
 * This function analyzes case when T(histLabel, diffIndex) and 
 * T(curLabel, diffIndex) are the same implicit task, T'. T(histLabel) and
 * T(curLabel) are descendent tasks of T'. T(histLabel, diffIndex+1) is
 * workshare task, T(curLabel, diffIndex+1) is also workshare task. 
 * If T(histLabel, diffIndex+1) and T(curLabel, diffIndex+1) are in the same
 * workshare construct, curLabel[diffIndex] and histLabel[diffIndex] would
 * not be different. So they must be in two different workshare construct
 * with nowait specified. 
 */
bool analyzeNextWorkWork(Label* histLabel, Label* curLabel, int diffIndex) {
  return false;
}

uint64_t computeExitRank(uint64_t phase) {
  return phase - (phase % 2); 
}

uint64_t computeEnterRank(uint64_t phase) {
  return phase + (phase % 2);
}

/*
 * Build the check case. Avoid conditional instructions. Concatenate 
 * bit level representation of two segment type to form a case code and 
 * directly  
 */
inline CheckCase buildCheckCase(SegmentType histType, SegmentType curType) {
  return static_cast<CheckCase>(histType | (curType << CASE_SHIFT));
}

/*
 * Helper function to dispatch different checking procedures based 
 * on the type of label segments of hist[diffIndex+1], cur[diffIndex+1]
 */
bool dispatchAnalysis(CheckCase checkCase, Label* hist, Label* cur, 
        int diffIndex) {
  switch(checkCase) {
    case eImpImp:
      RAW_LOG(FATAL, "not expected case: imp-imp");
    case eImpExp:
      return analyzeNextImpExp(hist, cur, diffIndex);
    case eImpWork:
      return analyzeNextImpWork(hist, cur, diffIndex);
    case eExpImp:
      return analyzeNextExpImp(hist, cur, diffIndex);
    case eExpExp:
      return analyzeNextExpExp(hist, cur, diffIndex);
    case eExpWork:
      return analyzeNextExpWork(hist, cur, diffIndex);
    case eWorkImp:
      return analyzeNextWorkImp(hist, cur, diffIndex);
    case eWorkExp:
      return analyzeNextWorkExp(hist, cur, diffIndex);
    case eWorkWork:
      return analyzeNextWorkWork(hist, cur, diffIndex);
  }
  return false;
}

/*
 * This function determines the action on access history depending on 
 * various conditions between hist record and cur record. This is where
 * access history maintenence decision is made.
 * Here we implement the baseline pruning algorithm
 */
RecordManagement manageAccessRecord(const Record& histRecord, 
                                    const Record& curRecord,
                                    bool isHistBeforeCurrent,
                                    int diffIndex) {
  auto histIsWrite = histRecord.isWrite();  
  auto curIsWrite = curRecord.isWrite();
  auto histLockSet = histRecord.getLockSet();
  auto curLockSet = curRecord.getLockSet();
  if (((histIsWrite && curIsWrite) || !histIsWrite) && 
          isHistBeforeCurrent && isSubset(curLockSet, histLockSet)) {
    return eDelHist;  
  } else if (diffIndex == static_cast<int>(eSameLabel) && 
            ((!histIsWrite && !curIsWrite) || histIsWrite) && 
            isSubset(histLockSet, curLockSet)) {
      return eSkipAddCur; 
  }
  return eNoOp;
} 

                         
/*
 * This function modifies the access record associated with a memory address
 * based on the management decision. It advances the iterator to the container
 * that holds access records 
 */
void modifyAccessHistory(RecordManagement decision, 
                         std::vector<Record>* records,
                         std::vector<Record>::iterator& it) {
  if (decision == eDelHist) {
    it = records->erase(it);
  } else {
    it++;
  }
}

}
