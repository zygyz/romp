#include "Callbacks.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>

#include "AccessControl.h"
#include "AccessHistory.h"
#include "CoreUtil.h"
#include "DataSharing.h"
#include "Label.h"
#include "ParallelRegionData.h"
#include "PerformanceCounters.h"
#include "TaskInfoQuery.h"
#include "ShadowMemory.h"
#include "TaskData.h"
#include "ThreadData.h"

extern ShadowMemory<AccessHistory> shadowMemory;
extern PerformanceCounters gPerformanceCounters;
   
void on_ompt_callback_implicit_task(
       ompt_scope_endpoint_t endPoint,
       ompt_data_t* parallelData,
       ompt_data_t* taskData,
       unsigned int actualParallelism,
       unsigned int index,
       int flags) {
  if (flags == ompt_task_initial) {
    auto initTaskData = new TaskData();
    auto newTaskLabel = generateInitialTaskLabel();
    initTaskData->label = std::move(newTaskLabel);
    initTaskData->parallelRegionDataPtr = parallelData->ptr;
    taskData->ptr = static_cast<void*>(initTaskData);
    return;
  } 
  auto taskDataPtr = static_cast<TaskData*>(taskData->ptr);
  if (actualParallelism == 0 && index != 0) {
    // Parallelism is 0 means that it is end of task, index != 0 means
    // that it is not the master thread, simply release the memory and
    // return. implicit-task-end and initial-task-end events.
    // We have to do it here before getting parent task because somehow 
    // the runtime library won't be able to get parent task for this case.
    if (!taskDataPtr) {
      RAW_LOG(FATAL, "task data pointer is null");
    }
    delete taskDataPtr; 
    taskData->ptr = nullptr;
    return;
  }
  TaskInfo parentTaskInfo;
  if (!queryTaskInfo(1, parentTaskInfo)) {
    RAW_LOG(FATAL, "cannot get parent task info");     
    return;
  }   
  auto parentTaskData = static_cast<TaskData*>(parentTaskInfo.taskData->ptr);
  switch(endPoint) {
    case ompt_scope_begin:
    {
      // begin of implcit task, create the label for this new task
      auto newTaskLabel = generateImplicitTaskLabel((parentTaskData->label).get(), index, actualParallelism); 
      // return value optimization should avoid the ref count mod
      auto newTaskDataPtr = new TaskData();
      // cast to rvalue and avoid atomic ref count modification
      newTaskDataPtr->label = std::move(newTaskLabel); 
      newTaskDataPtr->mutateCount++;
      newTaskDataPtr->parallelRegionDataPtr = parallelData->ptr;
      taskData->ptr = static_cast<void*>(newTaskDataPtr);
      return;
    }
    case ompt_scope_end:
    {
      // End of the current implicit task, modify parent task's label
      // only one worker thread with index 0 is responsible for mutating
      // the parent task label. The mutated label should be created separately
      // because access history referred to labels by pointer.
      // At this point, only one implicit task should reach here.
      if (!taskDataPtr) { 
        RAW_LOG(FATAL, "task data pointer is null");
      }
      auto mutatedLabel = mutateParentImpEnd(taskDataPtr->label.get());
      parentTaskData->label = std::move(mutatedLabel);
      parentTaskData->mutateCount++;
      delete taskDataPtr; 
      taskData->ptr = nullptr;
      return;
    }
  }
}

inline BaseSegment* getLastSegment(Label* label) {
  auto lenLabel = label->getLabelLength();
  return label->getKthSegment(lenLabel - 1);
}

/*
 * Once a task encounters a taskwait, mark the task's explicit children to 
 * be taskwaited, and record the ordered section phase value 
 */
void markExpChildSyncTaskwait(TaskData* taskData, Label* curLabel) {
  auto segment = getLastSegment(curLabel);
  auto phase = segment->getPhase();
  for (auto& child : taskData->childrenExplicitTasks) {
    auto childTaskData = static_cast<TaskData*>(child); 
    childTaskData->setIsTaskwait(true); 
    auto lastSegment = getLastSegment(childTaskData->label.get());
    uint64_t offset, span;
    lastSegment->getOffsetSpan(offset, span);
    if (span == 1) { // if the last label segment is still explicit label segment, set the taskwaited flag in segment
      lastSegment->setTaskwaited();
      lastSegment->setTaskwaitPhase(phase); 
    }
  }
  taskData->childrenExplicitTasks.clear(); // clear the children after taskwait
}

/*
 * Once a task encounters the end of taskgroup, mark all explicit task 
 * children which are inside the ending taskgroup. 
 */
void markExpChildSyncTaskGroupEnd(TaskData* taskData, Label* curLabel) {
  auto seg = getLastSegment(curLabel);
  auto phase = seg->getPhase();
  auto taskGroupLevel = seg->getTaskGroupLevel();
  auto taskGroupId = seg->getTaskGroupId(); 
  auto it = taskData->childrenExplicitTasks.begin();
  auto lenParentLabel = curLabel->getLabelLength();
  while (it != taskData->childrenExplicitTasks.end()) {
    auto childTaskData = static_cast<TaskData*>(*it);
    auto childLabel = childTaskData->label;
    auto lenChildLabel = childLabel->getLabelLength();
    if (lenChildLabel > lenParentLabel) {
      // This child task is not inside current task group
      it++; 
    } else {
      auto lastSeg = childLabel->getLastKthSegment(1);    
      // check if the task group id matches  
      auto childTaskGroupId = lastSeg->getTaskGroupId();
      if (childTaskGroupId == taskGroupId) {
        auto mutatedChildLabel = mutateTaskGroupSyncChild(childLabel.get());
        childTaskData->label = std::move(mutatedChildLabel);
        it = taskData->childrenExplicitTasks.erase(it);
      } else {
        it++;
      }
    }
  }
}

void on_ompt_callback_sync_region(
       ompt_sync_region_t kind,
       ompt_scope_endpoint_t endPoint,
       ompt_data_t *parallelData,
       ompt_data_t *taskData,
       const void* codePtrReturnAddress) {
  if (!taskData || !taskData->ptr) {
    RAW_LOG(FATAL, "task data pointer is null");  
    return;
  }
  auto taskDataPtr = static_cast<TaskData*>(taskData->ptr);
  auto labelPtr = (taskDataPtr->label).get();  // never std::move here!
  std::shared_ptr<Label> mutatedLabel = nullptr;
  switch(kind) {
    case ompt_sync_region_barrier:
    case ompt_sync_region_barrier_implicit:
    case ompt_sync_region_barrier_explicit:
    case ompt_sync_region_barrier_implementation:
    {
      if (endPoint == ompt_scope_end) {
        mutatedLabel = mutateBarrierEnd(labelPtr);
      }   
      break;
    }
    case ompt_sync_region_taskwait:
    {
      if (endPoint == ompt_scope_begin) {
        mutatedLabel = mutateTaskWait(labelPtr);
        markExpChildSyncTaskwait(taskDataPtr, labelPtr);
      }
      break;
    }
    case ompt_sync_region_taskgroup:
    {
      if (endPoint == ompt_scope_begin) {
        mutatedLabel = mutateTaskGroupBegin(labelPtr);
      } else if (endPoint == ompt_scope_end) {
        mutatedLabel = mutateTaskGroupEnd(labelPtr);
        markExpChildSyncTaskGroupEnd(taskDataPtr, labelPtr);
      }
      break;
    }
    case ompt_sync_region_reduction:
    {
      // Note: callback for reduction is replaced by ompt_callback_reduction
      if (endPoint == ompt_scope_begin) { 
        taskDataPtr->setIsInReduction(true);
      } else if (endPoint == ompt_scope_end) {
        taskDataPtr->setIsInReduction(false);
      } 
    }
    default:
      RAW_LOG(FATAL, "unknown ompt_sync_region_t type: %d", kind);
      break;
  }
  if (mutatedLabel != nullptr) { // for default case, don't modify
    taskDataPtr->label = std::move(mutatedLabel);
    taskDataPtr->mutateCount++;
  }
  return;
}

void on_ompt_callback_mutex_acquired(
        ompt_mutex_t kind,
        ompt_wait_id_t waitId,
        const void *codePtrRa) {
  TaskInfo taskInfo;
  if (!queryTaskInfo(0, taskInfo)) {
    RAW_LOG(FATAL, "task data pointer is null");
    return;
  }
  auto taskDataPtr = static_cast<TaskData*>(taskInfo.taskData->ptr);
  auto label = taskDataPtr->label;
  std::shared_ptr<Label> mutatedLabel = nullptr;
  if (kind == ompt_mutex_ordered) {
    mutatedLabel = mutateOrderSection(label.get()); 
  } else {
    if (taskDataPtr->lockSet == nullptr) {
      auto lockSet = std::make_shared<LockSet>();
      taskDataPtr->lockSet = std::move(lockSet);
      taskDataPtr->lockSet->addLock(static_cast<uint64_t>(waitId));
    } else {
      // we need to clone the old lockset before modifying it
      auto clonedLockSet = taskDataPtr->lockSet->clone();
      clonedLockSet->addLock(static_cast<uint64_t>(waitId));
      taskDataPtr->lockSet = std::move(clonedLockSet);
    }
  }
  if (mutatedLabel) {
    taskDataPtr->label = std::move(mutatedLabel);
    taskDataPtr->mutateCount++;
  }
}

void on_ompt_callback_mutex_released(
        ompt_mutex_t kind,
        ompt_wait_id_t waitId,
        const void *codePtrRa) {
  void* dataPtr;
  TaskInfo taskInfo;
  if (!queryTaskInfo(0, taskInfo)) {
    RAW_LOG(FATAL, "failed to fetch task info");
    return;
  } 
  auto taskDataPtr = static_cast<TaskData*>(taskInfo.taskData->ptr);
  auto label = taskDataPtr->label;
  std::shared_ptr<Label> mutatedLabel = nullptr; 
  if (kind == ompt_mutex_ordered) {
    mutatedLabel = mutateOrderSection(label.get());
  } else {
    // we need to clone the old lockset before modifying it
    auto clonedLockSet = taskDataPtr->lockSet->clone();
    clonedLockSet->removeLock(waitId);
    taskDataPtr->lockSet = std::move(clonedLockSet);
  }
  if (mutatedLabel) {
    taskDataPtr->label = std::move(mutatedLabel);
    taskDataPtr->mutateCount++;
  }
}

inline std::shared_ptr<Label> mutateTaskLabelOnWorkLoopCallback(
                             ompt_scope_endpoint_t endPoint, 
                             const std::shared_ptr<Label>& label) {
  std::shared_ptr<Label> mutatedLabel = nullptr;
  auto labelPtr = label.get();
  if (endPoint == ompt_scope_begin) {
    mutatedLabel = mutateLoopBegin(labelPtr);
  } else if (endPoint == ompt_scope_end) {
    mutatedLabel = mutateLoopEnd(labelPtr);
  }  
  return mutatedLabel;
}

inline std::shared_ptr<Label> mutateTaskLabelOnWorkSectionsCallback(
        ompt_scope_endpoint_t endPoint, 
        const std::shared_ptr<Label>& label,
        uint64_t count) {
  std::shared_ptr<Label> mutatedLabel = nullptr;
  auto labelPtr = label.get();
  if (endPoint == ompt_scope_begin) {
    mutatedLabel = mutateSectionBegin(labelPtr);
  } else if (endPoint == ompt_scope_end) {
    mutatedLabel = mutateSectionEnd(labelPtr);
  }
  return mutatedLabel;
}

inline std::shared_ptr<Label> mutateTaskLabelOnTaskLoopCallback(
    ompt_scope_endpoint_t endPoint,
    const std::shared_ptr<Label>& label, 
    uint64_t count) {
  std::shared_ptr<Label> mutatedLabel = nullptr;
  auto labelPtr = label.get();
  RAW_DLOG(INFO, "mutate on task loop, count: %d", count);
  if (endPoint == ompt_scope_begin) {
    mutatedLabel = mutateLoopBegin(labelPtr);
  } else if (endPoint == ompt_scope_end) {
    mutatedLabel = mutateLoopEnd(labelPtr);
  }  
  return mutatedLabel;  
}
   

inline std::shared_ptr<Label> mutateTaskLabelOnWorkSingleExecutorCallback(
        ompt_scope_endpoint_t endPoint, 
        const std::shared_ptr<Label>& label) {
  auto labelPtr = label.get();
  std::shared_ptr<Label> mutatedLabel = mutateSingleExecutor(labelPtr);
  return mutatedLabel;
}

inline std::shared_ptr<Label> mutateTaskLabelOnWorkSingleOtherCallback(
        ompt_scope_endpoint_t endPoint, 
        const std::shared_ptr<Label>& label) {
  auto labelPtr = label.get();
  std::shared_ptr<Label> mutatedLabel =  mutateSingleOther(labelPtr);
  return mutatedLabel;
}

void on_ompt_callback_work(
      ompt_work_t workType,
      ompt_scope_endpoint_t endPoint,
      ompt_data_t *parallelData,
      ompt_data_t *taskData,
      uint64_t count,
      const void *codePtrRa) {
  if (!taskData || !taskData->ptr) {
    RAW_LOG(FATAL, "task data pointer is null");
  }
  auto taskDataPtr = static_cast<TaskData*>(taskData->ptr);
  auto label = taskDataPtr->label;
  std::shared_ptr<Label> mutatedLabel = nullptr;
  switch(workType) {
    case ompt_work_loop: 
      mutatedLabel = mutateTaskLabelOnWorkLoopCallback(endPoint, label);
      break;
    case ompt_work_sections:
      mutatedLabel = mutateTaskLabelOnWorkSectionsCallback(endPoint, label, count);
      break;
    case ompt_work_single_executor:
      mutatedLabel = mutateTaskLabelOnWorkSingleExecutorCallback(endPoint, label);
      break;
    case ompt_work_single_other:
      mutatedLabel = mutateTaskLabelOnWorkSingleOtherCallback(endPoint, label);
      break;
    case ompt_work_workshare:
      RAW_LOG(FATAL, "ompt_work_workshare is not supported yet");
      break;
    case ompt_work_distribute:
      RAW_LOG(FATAL, "ompt_work_distribute is not supported yet");
      break;
    case ompt_work_taskloop:
      mutatedLabel = mutateTaskLabelOnTaskLoopCallback(endPoint, label, count); 
      break;
    default:
      break;
  }
  taskDataPtr->label = std::move(mutatedLabel);
  taskDataPtr->mutateCount++;
}

void on_ompt_callback_parallel_begin(
       ompt_data_t *encounteringTaskData,
       const ompt_frame_t *encounteringTaskFrame,
       ompt_data_t *parallelData,
       unsigned int requestedParallelism,
       int flags,
       const void *codePtrRa) {
  auto parallelRegionData = new ParallelRegionData(requestedParallelism, flags);
  parallelData->ptr = static_cast<void*>(parallelRegionData);  
}

void on_ompt_callback_parallel_end( 
       ompt_data_t *parallelData,
       ompt_data_t *encounteringTaskData,
       int flags,
       const void *codePtrRa) {
  auto parRegionData = parallelData->ptr;
  delete static_cast<ParallelRegionData*>(parRegionData);
}  

void on_ompt_callback_task_create(
        ompt_data_t *encounteringTaskData,
        const ompt_frame_t *encounteringTaskFrame,
        ompt_data_t *newTaskData,
        int flags,
        int hasDependences,
        const void *codePtrRa) {
  // In current llvm-openmp implementation:
  // https://github.com/llvm/llvm-project/blob/83914ee96fc2d828e1cfb8913f5d156d39150e2c/openmp/runtime/src/kmp_tasking.cpp#L795
  // flags is a variable where multiple bits can be set
  // e.g., flags = ompt_task_explicit | ompt_task_undeferred | ompt_task_untied 
  RAW_DLOG(INFO, "ompt_callback_task_create called");
  auto isExplicitTask = (flags & ompt_task_explicit) == ompt_task_explicit;
  auto isUndeferred = (flags & ompt_task_undeferred) == ompt_task_undeferred;
  auto isUntied = (flags & ompt_task_untied) == ompt_task_untied; 
  auto isFinal = (flags & ompt_task_final) == ompt_task_final;
  auto isMergeable = (flags & ompt_task_mergeable) == ompt_task_mergeable;
  auto isTaskwait = (flags & ompt_task_taskwait) == ompt_task_taskwait;
  auto parentTaskData = static_cast<TaskData*>(encounteringTaskData->ptr); 
  if (!parentTaskData || !parentTaskData->label) {
    RAW_LOG(FATAL, "cannot get parent task label");
    return;
  }  
  auto taskData = new TaskData();
  // the explicit task being created share the same parallel region with its parent task.
  taskData->parallelRegionDataPtr = parentTaskData->parallelRegionDataPtr; 
  taskData->setIsExplicitTask(isExplicitTask);
  taskData->setIsUndeferredTask(isUndeferred);
  taskData->setIsUntiedTask(isUntied);
  taskData->setIsFinalTask(isFinal);
  taskData->setIsMergeableTask(isMergeable);
  taskData->setIsTaskwait(isTaskwait);
  taskData->setHasDependence(hasDependences > 0);
  // there is one case where the flags == ompt_task_taskwait | ompt_task_undeferred | ompt_task_mergeable
  // one example is #pragma omp task deps(in:x) if(0) we still treat this as explicit task.
  auto parentLabel = (parentTaskData->label).get();
  taskData->label = generateExplicitTaskLabel(parentLabel, static_cast<void*>(taskData));
  auto mutatedParentLabel = mutateParentTaskCreate(parentLabel); 
  parentTaskData->label = std::move(mutatedParentLabel);
  parentTaskData->mutateCount++; 
  if (isExplicitTask) {
    parentTaskData->recordExplicitTaskData(taskData); 
  }
  if (isUndeferred) {
    parentTaskData->recordUndeferredTaskData(taskData);
  } 
  newTaskData->ptr = static_cast<void*>(taskData);
  RAW_DLOG(INFO, "task create new task ptr: %lx", newTaskData->ptr);
}

void handleTaskComplete(void* taskPtr) {
  if (taskPtr == nullptr) {
    RAW_LOG(WARNING, "handleTaskComplete: task ptr is null");
    return;
  }
  auto taskDataPtr = static_cast<TaskData*>(taskPtr);
  auto label = (taskDataPtr->label).get();
  auto mutatedLabel = mutateTaskComplete(label);
  taskDataPtr->label = std::move(mutatedLabel);
  taskDataPtr->mutateCount++;
}

void on_ompt_callback_task_schedule(
        ompt_data_t *priorTaskData,
        ompt_task_status_t priorTaskStatus,
        ompt_data_t *nextTaskData) {
  auto priorTaskPtr = priorTaskData->ptr;
  switch(priorTaskStatus) {
    case ompt_task_complete:
      handleTaskComplete(priorTaskPtr);
      break;
    case ompt_task_switch:
    case ompt_task_yield:
    case ompt_task_cancel:
    case ompt_task_detach:
      break;
    case ompt_task_early_fulfill:
    case ompt_task_late_fulfill:
      break;
    default:
      RAW_LOG(WARNING, "unknown prior task status: %d", priorTaskStatus); 
      break;
  } 
}

void on_ompt_callback_dependences(ompt_data_t *taskData, const ompt_dependence_t *deps, int ndeps) {
  RAW_DLOG(INFO, "ompt_callback_dependences");
  auto taskPtr = taskData->ptr;
  if (!taskPtr) {
    RAW_LOG(WARNING, "callback dependences: current task data ptr is null");
    return;
  }
  ParallelRegionInfo parallelRegionInfo;
  if (!queryParallelRegionInfo(0, parallelRegionInfo)) {
    RAW_LOG(WARNING, "cannot get parallel region data");
    return;
  }	  
  // get pointer parallel region data structure
  auto parallelRegionData = static_cast<ParallelRegionData*>(parallelRegionInfo.parallelData->ptr);
  if (!parallelRegionData) {
    RAW_LOG(FATAL, "callback dependences: current parallel data ptr is null");
    return;
  }
  pfq_rwlock_node_t node;      
#ifdef PERFORMANCE
  ReaderWriterLockGuard guard(&(parallelRegionData->lock), &node, &gPerformanceCounters);
#else
  ReaderWriterLockGuard guard(&(parallelRegionData->lock), &node, nullptr);
#endif
  guard.upgradeFromReaderToWriter();   
  // while in mutual exculsion, maintain explicit task dependences
  for (int i = 0; i < ndeps; ++i) {
    RAW_DLOG(INFO, "maintain task dependence: %lx", taskPtr);
    parallelRegionData->maintainTaskDependence(taskPtr, deps[i]);
  }
}

void on_ompt_callback_thread_begin(
       ompt_thread_t threadType,
       ompt_data_t *threadData) {
  if (!threadData) {
    RAW_LOG(FATAL, "thread data is null");
    return;
  }
  threadData->value = (uint64_t)threadType;
  switch(threadType) {
    case ompt_thread_initial:
    case ompt_thread_worker:
    {
      threadData->ptr = static_cast<void*>(new ThreadData());
      void* stackBaseAddress = nullptr;
      uint64_t stackSize = 0;
      if (!queryThreadStackInfo(stackBaseAddress, stackSize)) {
        RAW_LOG(FATAL, "failed to get thread stack info");
        return;
      }
      static_cast<ThreadData*>(threadData->ptr)->stackBaseAddress = stackBaseAddress;
      static_cast<ThreadData*>(threadData->ptr)->stackTopAddress = reinterpret_cast<void*>(reinterpret_cast<uint64_t>(stackBaseAddress) + stackSize); 
      return;
    }
    case ompt_thread_other:
    case ompt_thread_unknown:
      threadData->ptr = nullptr;
      return;
  }
}

void on_ompt_callback_thread_end(
       ompt_data_t *threadData) {
  if (!threadData) {
    return;
  }
  if (!threadData->ptr) {
    delete static_cast<ThreadData*>(threadData->ptr);
  }
  threadData->ptr = nullptr;
}

void on_ompt_callback_dispatch(
       ompt_data_t *parallelData,
       ompt_data_t *taskData,
       ompt_dispatch_t kind,
       ompt_data_t instance) {
  if (!taskData || !taskData->ptr) {
    RAW_LOG(FATAL, "cannot get task data info");
    return;
  }
  auto taskDataPtr = static_cast<TaskData*>(taskData->ptr);
  auto parentLabel = (taskDataPtr->label).get();
  std::shared_ptr<Label> mutatedLabel = nullptr;
  switch(kind) {
    case ompt_dispatch_iteration:
      mutatedLabel = mutateWorkShareIterationDispatch(parentLabel, instance.value);
      break;
   case ompt_dispatch_section:
      mutatedLabel = mutateSectionDispatch(parentLabel, instance.ptr);
      break; 
   default:
     RAW_LOG(FATAL, "unexpected case %d", kind);
 }
 taskDataPtr->label = std::move(mutatedLabel);
 taskDataPtr->mutateCount++;
}

void on_ompt_callback_reduction(
       ompt_sync_region_t kind,
       ompt_scope_endpoint_t endPoint,
       ompt_data_t *parallelData,
       ompt_data_t *taskData,
       const void *codePtrReturnAddress) {
  if (!taskData || !taskData->ptr) {
    RAW_LOG(FATAL, "task data pointer is null");
    return;
  }  
  auto taskDataPtr = static_cast<TaskData*>(taskData->ptr);
  switch(endPoint) {
    case ompt_scope_begin:
      taskDataPtr->setIsInReduction(true);
      break;
    case ompt_scope_end:
      taskDataPtr->setIsInReduction(false);
      break;
    default:
      break;
  }
}

