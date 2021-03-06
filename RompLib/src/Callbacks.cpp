#include "Callbacks.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>

#include "AccessHistory.h"
#include "CoreUtil.h"
#include "DataSharing.h"
#include "Label.h"
#include "ParRegionData.h"
#include "QueryFuncs.h"
#include "ShadowMemory.h"
#include "TaskData.h"
#include "ThreadData.h"

namespace romp {   

extern ShadowMemory<AccessHistory> shadowMemory;
   
void on_ompt_callback_implicit_task(
       ompt_scope_endpoint_t endPoint,
       ompt_data_t* parallelData,
       ompt_data_t* taskData,
       unsigned int actualParallelism,
       unsigned int index,
       int flags) {
  RAW_DLOG(INFO, "on_ompt_callback_implicit_task called:%u p:%lx t:%lx %u %u %d",
          endPoint, parallelData, taskData, actualParallelism, index, flags);
  incrementLabelId();
  if (flags == ompt_task_initial) {
    RAW_DLOG(INFO, "generating initial task: %lx", taskData);
    auto initTaskData = new TaskData();
    auto newTaskLabel = genInitTaskLabel();
    initTaskData->label = std::move(newTaskLabel);
    taskData->ptr = static_cast<void*>(initTaskData);
    return;
  } 
  auto taskDataPtr = static_cast<TaskData*>(taskData->ptr);
  if (actualParallelism == 0 && index != 0) {
    /* 
     * Parallelism is 0 means that it is end of task, index != 0 means
     * that it is not the master thread, simply release the memory and
     * return. implicit-task-end and initial-task-end events.
     * We have to do it here before getting parent task because somehow 
     * the runtime library won't be able to get parent task for this case.
    */
    if (!taskDataPtr) {
      RAW_LOG(FATAL, "task data pointer is null");
    }
    delete taskDataPtr; 
    taskData->ptr = nullptr;
    return;
  }
  int parentTaskType, parentThreadNum;
  void* parentDataPtr = nullptr;
  if (!queryTaskInfo(1, parentTaskType, parentThreadNum, parentDataPtr)) {
    RAW_LOG(FATAL, "cannot get parent task info");     
    return;
  }   
  auto parentTaskData = static_cast<TaskData*>(parentDataPtr);
  if (endPoint == ompt_scope_begin) {
    // begin of implcit task, create the label for this new task
    auto newTaskLabel = genImpTaskLabel((parentTaskData->label).get(), index, 
            actualParallelism); 
    // return value optimization should avoid the ref count mod
    auto newTaskDataPtr = new TaskData();
    RAW_DLOG(INFO, "created task data ptr: %p stored at %p",
            newTaskDataPtr, taskData);
    // cast to rvalue and avoid atomic ref count modification
    newTaskDataPtr->label = std::move(newTaskLabel); 
    RAW_DLOG(INFO, "%p label is: %p", newTaskDataPtr, newTaskDataPtr->label.get());
    taskData->ptr = static_cast<void*>(newTaskDataPtr);
  } else if (endPoint == ompt_scope_end) {
    /* 
     * End of the current implicit task, modify parent task's label
     * only one worker thread with index 0 is responsible for mutating
     * the parent task label. The mutated label should be created separately
     * because access history referred to labels by pointer.
     * At this point, only one implicit task should reach here.
     */
    if (!taskDataPtr) { 
      RAW_LOG(FATAL, "task data pointer is null");
    }
    if (taskDataPtr->label == nullptr) {
      RAW_LOG(FATAL, "%p, %p label is nullptr", taskData, taskDataPtr);; 
    }
    auto mutatedLabel = mutateParentImpEnd(taskDataPtr->label.get());
    parentTaskData->label = std::move(mutatedLabel);
    RAW_DLOG(INFO, "modifying parent label: %p %p", parentTaskData);
    delete taskDataPtr; 
    taskData->ptr = nullptr;
  }
}


/* 
 * Helper function for getting the phase value of the last label segment.
 */
inline Segment* getLastSegment(Label* label) {
  auto lenLabel = label->getLabelLength();
  return label->getKthSegment(lenLabel - 1);
}

/*
 * Once a task encounters a taskwait, mark the task's explicit children to 
 * be taskwaited, and record the ordered section phase value 
 */
void markExpChildSyncTaskwait(TaskData* taskData, Label* curLabel) {
  auto seg = getLastSegment(curLabel);
  auto phase = seg->getPhase();
  for (const auto& child : taskData->childExpTaskData) {
    auto childTaskData = static_cast<const TaskData*>(child); 
    auto lenLabel = childTaskData->label->getLabelLength(); 
    auto lastSeg = childTaskData->label->getKthSegment(lenLabel - 1);
    lastSeg->setTaskwaited();
    lastSeg->setTaskwaitPhase(phase);
  }
  taskData->childExpTaskData.clear(); // clear the children after taskwait
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
  auto it = taskData->childExpTaskData.begin();
  auto lenParentLabel = curLabel->getLabelLength();
  while (it != taskData->childExpTaskData.end()) {
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
        it = taskData->childExpTaskData.erase(it);
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
       const void* codePtrRa) {
  RAW_DLOG(INFO,  "on_ompt_callback_sync_region called %p %d %d", 
          taskData, kind, endPoint);
  incrementLabelId();
  if (!taskData || !taskData->ptr) {
    RAW_LOG(FATAL, "task data pointer is null");  
    return;
  }
  auto taskDataPtr = static_cast<TaskData*>(taskData->ptr);
  auto labelPtr = (taskDataPtr->label).get();  // never std::move here!
  std::shared_ptr<Label> mutatedLabel = nullptr;
  if (endPoint == ompt_scope_begin) {
    switch(kind) {
      case ompt_sync_region_reduction:
        taskDataPtr->inReduction = true;
        break;
      case ompt_sync_region_taskgroup:
        mutatedLabel = mutateTaskGroupBegin(labelPtr);
        break;
      default:
        RAW_DLOG(WARNING, "ignoring endpoint type %d", kind);
        break;
    } 
  } else if (endPoint == ompt_scope_end) {
    switch(kind) {
      case ompt_sync_region_taskwait:
        mutatedLabel = mutateTaskWait(labelPtr);
        markExpChildSyncTaskwait(taskDataPtr, labelPtr);
        break;
      case ompt_sync_region_taskgroup:
        mutatedLabel = mutateTaskGroupEnd(labelPtr);
        markExpChildSyncTaskGroupEnd(taskDataPtr, labelPtr);
        break;
      case ompt_sync_region_barrier:
      case ompt_sync_region_barrier_explicit:
      case ompt_sync_region_barrier_implementation:
      case ompt_sync_region_barrier_implicit:
        mutatedLabel = mutateBarrierEnd(labelPtr);
        break;
      case ompt_sync_region_reduction:
        taskDataPtr->inReduction = false;
        break;
      default:
        RAW_DLOG(WARNING, "ignoring endpoint type %d", kind);
        break;
    }
  }
  if (mutatedLabel != nullptr) { // for default case, don't modify
    taskDataPtr->label = std::move(mutatedLabel);
  }
  return;
}

void on_ompt_callback_mutex_acquired(
        ompt_mutex_t kind,
        ompt_wait_id_t waitId,
        const void *codePtrRa) {
  RAW_DLOG(INFO, "on_ompt_callback_mutex_acquired called");
  incrementLabelId();
  int taskType, threadNum;
  void* dataPtr;
  if (!queryTaskInfo(0, taskType, threadNum, dataPtr)) {
    RAW_LOG(FATAL, "task data pointer is null");
    return;
  }
  auto taskDataPtr = static_cast<TaskData*>(dataPtr);
  auto label = taskDataPtr->label;
  std::shared_ptr<Label> mutatedLabel = nullptr;
  if (kind == ompt_mutex_ordered) {
    mutatedLabel = mutateOrderSection(label.get()); 
  } else {
    RAW_DLOG(INFO, "mutex acquired on wait id: %lu", waitId);
    if (taskDataPtr->lockSet == nullptr) {
      auto lockSet = std::make_shared<SmallLockSet>();
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
  }
}

void on_ompt_callback_mutex_released(
        ompt_mutex_t kind,
        ompt_wait_id_t waitId,
        const void *codePtrRa) {
  RAW_DLOG(INFO, "on_ompt_callback_mutex_released called");
  incrementLabelId();
  int taskType, threadNum;
  void* dataPtr;
  if (!queryTaskInfo(0, taskType, threadNum, dataPtr)) {
    RAW_LOG(FATAL, "task data pointer is null");
    return;
  } 
  auto taskDataPtr = static_cast<TaskData*>(dataPtr);
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
  }
}

/*
 * Create a mutated label upon entering/exiting workshare loop construct
 */
inline std::shared_ptr<Label> handleOmpWorkLoop(
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

/*
 * Create a mutated label label upon entering/exiting workshare 
 * section construct
 */
inline std::shared_ptr<Label> handleOmpWorkSections(
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

inline std::shared_ptr<Label> handleOmpWorkSingleExecutor(
        ompt_scope_endpoint_t endPoint, 
        const std::shared_ptr<Label>& label) {
  std::shared_ptr<Label> mutatedLabel = nullptr;
  auto labelPtr = label.get();
  if (endPoint == ompt_scope_begin) {
    mutatedLabel = mutateSingleExecBegin(labelPtr);
  } else if (endPoint == ompt_scope_end) {
    mutatedLabel = mutateSingleEnd(labelPtr);  
  }
  return mutatedLabel;
}

inline std::shared_ptr<Label> handleOmpWorkSingleOther(
        ompt_scope_endpoint_t endPoint, 
        const std::shared_ptr<Label>& label) {
  std::shared_ptr<Label> mutatedLabel = nullptr;
  auto labelPtr = label.get();
  if (endPoint == ompt_scope_begin) {
    mutatedLabel = mutateSingleOtherBegin(labelPtr);
  } else if (endPoint == ompt_scope_end) {
    mutatedLabel = mutateSingleEnd(labelPtr);
  }
  return mutatedLabel;
}
    
inline std::shared_ptr<Label> handleOmpWorkWorkShare(
        ompt_scope_endpoint_t endPoint, 
        const std::shared_ptr<Label>& label, 
        uint64_t count) {
  RAW_LOG(FATAL, "c++ openmp does not support workshare construct");
  return nullptr;
}

inline std::shared_ptr<Label> handleOmpWorkDistribute(
        ompt_scope_endpoint_t endPoint, 
        const std::shared_ptr<Label>& label, 
        uint64_t count) {
  //TODO: This is assoicated with target and team construct
  RAW_LOG(FATAL, "not implemented yet");
  return nullptr;
}

/*
 * Taskloop is another worksharing construct that is like the worksharing
 * for-loop. The difference is that taskloop construct creates explicit 
 * tasks to execute the logical iterations in the loops.  
 */
inline std::shared_ptr<Label> handleOmpWorkTaskLoop(
        ompt_scope_endpoint_t endPoint, 
        const std::shared_ptr<Label>& label, 
        uint64_t count) {
  // TODO: determine label mutation rule for taskloop begin
  RAW_LOG(INFO, "task loop %lu", count);
  /*
  std::shared_ptr<Label> mutatedLabel = nullptr;
  if (endPoint == ompt_scope_begin) {
    mutatedLabel = mutateTaskLoopBegin(label);
  } else if (endPoint == ompt_scope_end) {
    mutatedLabel = mutateTaskLoopEnd(label);
  }
  return mutatedLabel;
  */
  return nullptr;
}

void on_ompt_callback_work(
      ompt_work_t wsType,
      ompt_scope_endpoint_t endPoint,
      ompt_data_t *parallelData,
      ompt_data_t *taskData,
      uint64_t count,
      const void *codePtrRa) {
  RAW_DLOG(INFO, "on_ompt_callback_work called");
  incrementLabelId();
  if (!taskData || !taskData->ptr) {
    RAW_LOG(FATAL, "task data pointer is null");
  }
  auto taskDataPtr = static_cast<TaskData*>(taskData->ptr);
  auto label = taskDataPtr->label;
  std::shared_ptr<Label> mutatedLabel = nullptr;
  switch(wsType) {
    case ompt_work_loop: 
      RAW_DLOG(INFO, "ompt_work_loop");
      mutatedLabel = handleOmpWorkLoop(endPoint, label);
      break;
    case ompt_work_sections:
      RAW_DLOG(INFO, "ompt_work_sections");
      mutatedLabel = handleOmpWorkSections(endPoint, label, count);
      break;
    case ompt_work_single_executor:
      RAW_DLOG(INFO, "ompt_work_single_executor");
      mutatedLabel = handleOmpWorkSingleExecutor(endPoint, label);
      break;
    case ompt_work_single_other:
      RAW_DLOG(INFO, "ompt_work_single_other");
      mutatedLabel = handleOmpWorkSingleOther(endPoint, label);
      break;
    case ompt_work_workshare:
      RAW_DLOG(INFO, "ompt_work_workshare");
      mutatedLabel = handleOmpWorkWorkShare(endPoint, label, count);
      break;
    case ompt_work_distribute:
      RAW_DLOG(INFO, "ompt_work_distribute");
      mutatedLabel = handleOmpWorkDistribute(endPoint, label, count);
      break;
    case ompt_work_taskloop:
      RAW_DLOG(INFO, "ompt_work_taskloop");
      mutatedLabel = handleOmpWorkTaskLoop(endPoint, label, count);
      break;
    default:
      break;
  }
  taskDataPtr->label = std::move(mutatedLabel);
}

void on_ompt_callback_parallel_begin(
       ompt_data_t *encounteringTaskData,
       const ompt_frame_t *encounteringTaskFrame,
       ompt_data_t *parallelData,
       unsigned int requestedParallelism,
       int flags,
       const void *codePtrRa) {
  RAW_DLOG(INFO, "parallel begin et:%lx p:%lx %u %d", encounteringTaskData, 
           parallelData, requestedParallelism, flags);
  incrementLabelId();
  auto parRegionData = new ParRegionData(requestedParallelism, flags);
  parallelData->ptr = static_cast<void*>(parRegionData);  
}

void on_ompt_callback_parallel_end( 
       ompt_data_t *parallelData,
       ompt_data_t *encounteringTaskData,
       int flags,
       const void *codePtrRa) {
  RAW_DLOG(INFO, "parallel end et:%lx p:%lx par data: %lx flag:%lx", 
		  encounteringTaskData, 
		  parallelData,
		  parallelData->ptr,
                  flags);
  incrementLabelId();
  auto parRegionData = parallelData->ptr;
  delete static_cast<ParRegionData*>(parRegionData);
}  

void on_ompt_callback_task_create(
        ompt_data_t *encounteringTaskData,
        const ompt_frame_t *encounteringTaskFrame,
        ompt_data_t *newTaskData,
        int flags,
        int hasDependences,
        const void *codePtrRa) {
  auto taskData = new TaskData();
  incrementLabelId();
  if (flags == ompt_task_initial) {
    /*
     * In recent diff (merged from https://reviews.llvm.org/D68615),initial task
     * creation ompt callback is moved to ompt_callback_implicit_task. The code 
     * here is not executed. We leave the code here for backward compatibility.
     */
    RAW_DLOG(INFO, "generating initial task: %lx", taskData);
    auto newTaskLabel = genInitTaskLabel();
    taskData->label = std::move(newTaskLabel);
  } else if (flags == ompt_task_explicit) {
    // create label for explicit task
    auto parentTaskData = static_cast<TaskData*>(encounteringTaskData->ptr);
    if (!parentTaskData || !parentTaskData->label) {
      RAW_LOG(FATAL, "cannot get parent task label");
      return;
    }
    auto parentLabel = (parentTaskData->label).get();
    auto newTaskLabel = genExpTaskLabel(parentLabel);
    taskData->label = std::move(newTaskLabel);
    taskData->isExplicitTask = true; // mark current task as explicit task
    auto mutatedParentLabel = mutateParentTaskCreate(parentLabel); 
    parentTaskData->label = std::move(mutatedParentLabel);
    parentTaskData->childExpTaskData.push_back(static_cast<void*>(taskData));
    // get parallel region info, atomic fetch and add the explicit task id
    auto teamSize = 0;
    void* parallelDataPtr = nullptr;   
    if (!queryParallelInfo(0, teamSize, parallelDataPtr)) {
      RAW_LOG(WARNING, "cannot get parallel region data");
    } else {
      auto parallelData = static_cast<ParRegionData*>(parallelDataPtr);
      auto taskId = parallelData->expTaskCount.fetch_add(1, 
		      std::memory_order_relaxed);
      RAW_DLOG(INFO, "explicit task create, local id: %d", taskId);
      taskData->expLocalId = taskId;        
    }	    
  } else if (flags == ompt_task_target) {
    // TODO: prepare the task data pointer for target 
    RAW_LOG(FATAL, "ompt_task_target not implemented yet");
  }
  newTaskData->ptr = static_cast<void*>(taskData);
}

/*
 * This helper function is called when task status is ompt_task_complete.
 * It mutates the label of the encountering task and set the stored label
 * to the mutated one.
 */
void handleTaskComplete(void* ptr) {
  auto taskDataPtr = static_cast<TaskData*>(ptr);
  auto label = (taskDataPtr->label).get();
  auto mutatedLabel = mutateTaskComplete(label);
  taskDataPtr->label = std::move(mutatedLabel);
}

void on_ompt_callback_task_schedule(
        ompt_data_t *priorTaskData,
        ompt_task_status_t priorTaskStatus,
        ompt_data_t *nextTaskData) {
  RAW_DLOG(INFO, "ompt_callback_task_schedule"); 
  auto taskPtr = priorTaskData->ptr;
  incrementLabelId();
  if (!taskPtr) {
    RAW_LOG(FATAL, "prior task data pointer is null"); 
  }
  switch(priorTaskStatus) {
    case ompt_task_complete:
      RAW_DLOG(INFO, "task complete encountered");
      handleTaskComplete(taskPtr);
      recycleTaskThreadStackMemory(taskPtr);
      recycleTaskPrivateMemory();
      break;
    case ompt_task_yield:
      RAW_DLOG(INFO, "taskyield construct encountered");
      break;
    case ompt_task_cancel:
      RAW_LOG(INFO, "task cancel encountered");
      break;
    case ompt_task_detach:
      RAW_LOG(INFO, "task detach encountered"); 
      break;
    case ompt_task_early_fulfill:
      RAW_LOG(INFO, "task early fulfill encountered");
      break;
    case ompt_task_late_fulfill:
      RAW_LOG(INFO, "task late fulfill encountered");
      break;
    case ompt_task_switch:
      RAW_DLOG(INFO, "task switch encountered");
      recycleTaskThreadStackMemory(taskPtr);
      recycleTaskPrivateMemory();
      break;
  } 
}

void on_ompt_callback_dependences(
        ompt_data_t *taskData,
        const ompt_dependence_t *deps,
        int ndeps) {
  RAW_DLOG(INFO, "callback dependencies -- num deps: %lu", ndeps);
  incrementLabelId();
  auto teamSize = 0;
  void* parallelDataPtr = nullptr;
  if (!queryParallelInfo(0, teamSize, parallelDataPtr)) {
    RAW_LOG(WARNING, "cannot get parallel region data");
    return;
  }	  
  // get pointer parallel region data structure
  auto parallelData = static_cast<ParRegionData*>(parallelDataPtr);
  if (!parallelData) {
    RAW_LOG(WARNING, "callback dependences: current parallel data ptr is null");
    return;
  }
  auto taskPtr = taskData->ptr;
  if (!taskPtr) {
    RAW_LOG(WARNING, "callback dependences: current task data ptr is null");
    return;
  }
  McsNode node;      
  LockGuard guard(&(parallelData->lock), &node);
  // while in mutual exculsion, maintain explicit task dependencies
  for (int i = 0; i < ndeps; ++i) {
    auto variable = deps[i].variable; 
    auto depType = deps[i].dependence_type;
    maintainTaskDeps(deps[i], taskPtr, parallelData);
  }
}


void on_ompt_callback_thread_begin(
       ompt_thread_t threadType,
       ompt_data_t *threadData) {
  if (!threadData) {
    RAW_LOG(WARNING, "thread data is null");
    return;
  }
  auto newThreadData = new ThreadData();
  if (!newThreadData) {
    RAW_LOG(FATAL, "failed to create thread data");
    return;
  }
  threadData->ptr = static_cast<void*>(newThreadData);
  incrementLabelId();
  void* stackAddr = nullptr;
  uint64_t stackSize = 0;
  if (!queryThreadStackInfo(stackAddr, stackSize)) {
    RAW_LOG(WARNING, "failed to get thread stack info");
    return;
  }
  newThreadData->stackBaseAddr = stackAddr;
  auto stackTopAddr = reinterpret_cast<void*>(
           reinterpret_cast<uint64_t>(stackAddr) +
           static_cast<uint64_t>(stackSize));             
  newThreadData->stackTopAddr = stackTopAddr;    
}

void on_ompt_callback_thread_end(
       ompt_data_t *threadData) {
  if (!threadData) {
    return;
  }
  incrementLabelId();
  auto dataPtr = threadData->ptr;
  if (!dataPtr) {
    delete static_cast<ThreadData*>(dataPtr);
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
  incrementLabelId();
  auto taskDataPtr = static_cast<TaskData*>(taskData->ptr);
  auto parentLabel = (taskDataPtr->label).get();
  std::shared_ptr<Label> mutatedLabel = nullptr;
  if (kind == ompt_dispatch_iteration) {
    mutatedLabel = mutateIterDispatch(parentLabel, instance.value);
  } else if (kind == ompt_dispatch_section) {
    mutatedLabel = mutateSectionDispatch(parentLabel, instance.ptr);
  }
  taskDataPtr->label = std::move(mutatedLabel);
}

/*
 * Note: this callback is merged into on_ompt_callback_sync_region in 
 * latest version of openmp spec. We keep this for backward compatibility.
 */
void on_ompt_callback_reduction(
       ompt_sync_region_t kind,
       ompt_scope_endpoint_t endPoint,
       ompt_data_t *parallelData,
       ompt_data_t *taskData,
       const void *codePtrRa) {
  if (!taskData || !taskData->ptr) {
    RAW_LOG(FATAL, "task data pointer is null");
    return;
  }  
  incrementLabelId();
  auto taskDataPtr = static_cast<TaskData*>(taskData->ptr);
  if (endPoint == ompt_scope_begin) {
    taskDataPtr->inReduction = true;
  } else if (endPoint == ompt_scope_end) {
    taskDataPtr->inReduction = false;
  }
}


}
