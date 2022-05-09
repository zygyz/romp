#include "TaskData.h"

TaskData::TaskData() {
  label = nullptr;
  lockSet = nullptr;
  exitFrame = nullptr;
  metaData = 0;
}

void TaskData::setIsExplicitTask(bool isExplicitTask) {
  if (!isExplicitTask) {
    return;
  }
  metaData |= is_explicit_task; 
}

void TaskData::setIsMutexTask(bool isMutexTask) {
  if (!isMutexTask) {
    return;
  }
  metaData |= is_mutex_task; 
}

void TaskData::setIsUndeferredTask(bool isUndeferredTask) {
  if (!isUndeferredTask) {
    return;
  }
  metaData |= is_undeferred_task; 
}

void TaskData::setIsUntiedTask(bool isUntiedTask) {
  if (!isUntiedTask) {
    return;
  }
  metaData |= is_untied_task; 
}

void TaskData::setIsFinalTask(bool isFinalTask) {
  if (!isFinalTask) {
    return;
  }
  metaData |= is_final_task;
}
  
void TaskData::setIsMergeableTask(bool isMergeableTask) {
  if (!isMergeableTask) {
    return;
  }
  metaData |= is_mergeable_task;
}

void TaskData::setIsInReduction(bool isInReduction) {
  if (!isInReduction) {
    return;
  }
  metaData |= is_in_reduction;
}

void TaskData::setIsTaskwait(bool isTaskwait) {
  if (!isTaskwait) {
    return;
  }
  metaData |= is_taskwait;
}

void TaskData::setIsMergedTask(bool isMergedTask) {
  metaData |= is_merged_task;
}

bool TaskData::getIsExplicitTask() const  {
  return (metaData & is_explicit_task) == is_explicit_task;
}
  
bool TaskData::getIsMutexTask() const {
  return (metaData & is_mutex_task) == is_mutex_task;
}

bool TaskData::getIsUndeferredTask() const {
  return (metaData & is_undeferred_task) == is_undeferred_task;
}

bool TaskData::getIsUntiedTask() const {
  return (metaData & is_untied_task) == is_untied_task;
}
  
bool TaskData::getIsFinalTask() const {
  return (metaData & is_final_task) == is_final_task;
}
  
bool TaskData::getIsMergeableTask() const {
  return (metaData & is_mergeable_task) == is_mergeable_task;
}
  
bool TaskData::getIsInReduction() const {
  return (metaData & is_in_reduction) == is_in_reduction;
}

bool TaskData::getIsTaskwait() const {
  return (metaData & is_taskwait) == is_taskwait;
}

bool TaskData::getIsMergedTask() const {
  return (metaData & is_mergeable_task) == is_mergeable_task;
}

