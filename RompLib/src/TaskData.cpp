#include "TaskData.h"

TaskData::TaskData() {
  label = nullptr;
  lockSet = nullptr;
  exitFrame = nullptr;
  metaData = 0;
}

void TaskData::recordExplicitTaskData(TaskData* taskData) {
  childrenExplicitTasks.push_back(static_cast<void*>(taskData));
}

void TaskData::recordUndeferredTaskData(TaskData* taskData) {
  undeferredTasks.push_back(static_cast<void*>(taskData));
}

void TaskData::setIsExplicitTask(bool isExplicitTask) {
  if (!isExplicitTask) {
    metaData &= ~is_explicit_task;         
  } else {
    metaData |= is_explicit_task; 
  }
}

void TaskData::setIsMutexTask(bool isMutexTask) {
  if (!isMutexTask) {
    metaData &= ~is_mutex_task;
  } else {
    metaData |= is_mutex_task; 
  }
}

void TaskData::setIsUndeferredTask(bool isUndeferredTask) {
  if (!isUndeferredTask) {
    metaData &= ~is_undeferred_task;
  } else {
    metaData |= is_undeferred_task; 
  }
}

void TaskData::setIsUntiedTask(bool isUntiedTask) {
  if (!isUntiedTask) {
    metaData &= ~is_untied_task;
  } else {
    metaData |= is_untied_task; 
  }
}

void TaskData::setIsFinalTask(bool isFinalTask) {
  if (!isFinalTask) {
    metaData &= ~is_final_task;
  } else {
    metaData |= is_final_task;
  }
}
  
void TaskData::setIsMergeableTask(bool isMergeableTask) {
  if (!isMergeableTask) {
    metaData &= ~is_mergeable_task;
  } else {
    metaData |= is_mergeable_task;
  }
}

void TaskData::setIsInReduction(bool isInReduction) {
  if (!isInReduction) {
    metaData &= ~is_in_reduction;
  } else {
    metaData |= is_in_reduction;
  }
}

void TaskData::setIsTaskwait(bool isTaskwait) {
  if (!isTaskwait) {
    metaData &= ~is_taskwait;
  } else {
    metaData |= is_taskwait;
  }
}

void TaskData::setIsMergedTask(bool isMergedTask) {
  if (!isMergedTask) {
    metaData &= ~is_merged_task;
  } else {
    metaData |= is_merged_task;
  }
}

void TaskData::setHasDependence(bool hasDependence) {
  if (!hasDependence) {
    metaData &= ~has_dependence;
  } else {
    metaData |= has_dependence;
  }
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

bool TaskData::getHasDependence() const {
  return (metaData & has_dependence) == has_dependence;
}
