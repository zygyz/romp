#include "Callbacks.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>

#include "AccessControl.h"
#include "AccessHistory.h"
#include "CoreUtil.h"
#include "DataSharing.h"
#include "Label.h"
#include "ParallelRegionData.h"
#include "TaskInfoQuery.h"
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
  if (flags == ompt_task_initial) {
    auto initTaskData = new TaskData();
    auto newTaskLabel = generateInitialTaskLabel();
    initTaskData->label = std::move(newTaskLabel);
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
      auto newTaskLabel = generateImplicitTaskLabel((parentTaskData->label).get(), index, 
            actualParallelism); 
      // return value optimization should avoid the ref count mod
      auto newTaskDataPtr = new TaskData();
      // cast to rvalue and avoid atomic ref count modification
      newTaskDataPtr->label = std::move(newTaskLabel); 
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
      delete taskDataPtr; 
      taskData->ptr = nullptr;
      return;
    }
  }
}

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
  for (const auto& child : taskData->childrenExplicitTasksData) {
    auto childTaskData = static_cast<const TaskData*>(child); 
    auto lenLabel = childTaskData->label->getLabelLength(); 
    auto lastSeg = childTaskData->label->getKthSegment(lenLabel - 1);
    lastSeg->setTaskwaited();
    lastSeg->setTaskwaitPhase(phase);
  }
  taskData->childrenExplicitTasksData.clear(); // clear the children after taskwait
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
  auto it = taskData->childrenExplicitTasksData.begin();
  auto lenParentLabel = curLabel->getLabelLength();
  while (it != taskData->childrenExplicitTasksData.end()) {
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
        it = taskData->childrenExplicitTasksData.erase(it);
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
      if (endPoint == ompt_scope_begin) { 
        taskDataPtr->inReduction = true;
      } else if (endPoint == ompt_scope_end) {
        taskDataPtr->inReduction = false;
      } 
    }
    default:
      RAW_LOG(FATAL, "unknown ompt_sync_region_t type: %d", kind);
      break;
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
      RAW_LOG(FATAL, "ompt_work_taskloop is not supported yet :(");
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
  switch(flags) {
    case ompt_task_target:
      RAW_LOG(FATAL, "ompt_task_target is not supported yet");
      return;
    case ompt_task_explicit:
      // create label for explicit task
      auto parentTaskData = static_cast<TaskData*>(encounteringTaskData->ptr);
      if (!parentTaskData || !parentTaskData->label) {
        RAW_LOG(FATAL, "cannot get parent task label");
        return;
      }
      auto taskData = new TaskData();
      auto parentLabel = (parentTaskData->label).get();
      taskData->label = generateExplicitTaskLabel(parentLabel);
      taskData->isExplicitTask = true; // mark current task as explicit task
      auto mutatedParentLabel = mutateParentTaskCreate(parentLabel); 
      parentTaskData->label = std::move(mutatedParentLabel);
      parentTaskData->childrenExplicitTasksData.push_back(static_cast<void*>(taskData));
      // get parallel region info, atomic fetch and add the explicit task id
      ParallelRegionInfo parallelRegionInfo;
      if (!queryParallelRegionInfo(0, parallelRegionInfo)) {
        RAW_LOG(FATAL, "cannot get parallel region data");
        return;
      }
      auto parallelRegionData  = static_cast<ParallelRegionData*>(parallelRegionInfo.parallelData->ptr);
      auto taskId = parallelRegionData->expTaskCount.fetch_add(1,std::memory_order_relaxed);
      taskData->expLocalId = taskId;        
      newTaskData->ptr = static_cast<void*>(taskData);
   }
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
}

void on_ompt_callback_task_schedule(
        ompt_data_t *priorTaskData,
        ompt_task_status_t priorTaskStatus,
        ompt_data_t *nextTaskData) {
  auto priorTaskPtr = priorTaskData->ptr;
  switch(priorTaskStatus) {
    case ompt_task_complete:
      handleTaskComplete(priorTaskPtr);
      //recycleTaskThreadStackMemory(priorTaskPtr);
      //recycleTaskPrivateMemory();
      break;
    case ompt_task_switch:
     // recycleTaskThreadStackMemory(priorTaskPtr);
     // recycleTaskPrivateMemory();
      break;
    case ompt_task_yield:
    case ompt_task_cancel:
    case ompt_task_detach:
    case ompt_task_early_fulfill:
    case ompt_task_late_fulfill:
      break;
    default:
      RAW_LOG(WARNING, "unknown prior task status: %d", priorTaskStatus); 
      break;
  } 
}

void on_ompt_callback_dependences(
        ompt_data_t *taskData,
        const ompt_dependence_t *deps,
        int ndeps) {
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
  mcs_node_t node;      
  LockGuard guard(&(parallelRegionData->lock), &node);
  // while in mutual exculsion, maintain explicit task dependencies
  for (int i = 0; i < ndeps; ++i) {
    auto variable = deps[i].variable; 
    auto depType = deps[i].dependence_type;
    maintainTaskDeps(deps[i], taskPtr, parallelRegionData);
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
  taskDataPtr->label = std::move(mutatedLabel);
 }
}

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
  auto taskDataPtr = static_cast<TaskData*>(taskData->ptr);
  switch(endPoint) {
    case ompt_scope_begin:
      taskDataPtr->inReduction = true;
      break;
    case ompt_scope_end:
      taskDataPtr->inReduction = false;
  }
}
}
