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
    metaData &= ~eIsExplicitTask;         
  } else {
    metaData |= eIsExplicitTask; 
  }
}

void TaskData::setIsMutexTask(bool isMutexTask) {
  if (!isMutexTask) {
    metaData &= ~eIsMutexTask;
  } else {
    metaData |= eIsMutexTask; 
  }
}

void TaskData::setIsUndeferredTask(bool isUndeferredTask) {
  if (!isUndeferredTask) {
    metaData &= ~eIsUndeferredTask;
  } else {
    metaData |= eIsUndeferredTask; 
  }
}

void TaskData::setIsUntiedTask(bool isUntiedTask) {
  if (!isUntiedTask) {
    metaData &= ~eIsUntiedTask;
  } else {
    metaData |= eIsUntiedTask; 
  }
}

void TaskData::setIsFinalTask(bool isFinalTask) {
  if (!isFinalTask) {
    metaData &= ~eIsFinalTask;
  } else {
    metaData |= eIsFinalTask;
  }
}

void TaskData::setIsMergeableTask(bool isMergeableTask) {
  if (!isMergeableTask) {
    metaData &= ~eIsMergeableTask;
  } else {
    metaData |= eIsMergeableTask;
  }
}

void TaskData::setIsInReduction(bool isInReduction) {
  if (!isInReduction) {
    metaData &= ~eIsInReduction;
  } else {
    metaData |= eIsInReduction;
  }
}

void TaskData::setIsTaskwait(bool isTaskwait) {
  if (!isTaskwait) {
    metaData &= ~eIsTaskwait;
  } else {
    metaData |= eIsTaskwait;
  }
}

void TaskData::setIsMergedTask(bool isMergedTask) {
  if (!isMergedTask) {
    metaData &= ~eIsMergedTask;
  } else {
    metaData |= eIsMergedTask;
  }
}

void TaskData::setHasDependence(bool hasDependence) {
  if (!hasDependence) {
    metaData &= ~eHasDependence;
  } else {
    metaData |= eHasDependence;
  }
}

bool TaskData::getIsExplicitTask() const  {
  return (metaData & eIsExplicitTask) == eIsExplicitTask;
}
  
bool TaskData::getIsMutexTask() const {
  return (metaData & eIsMutexTask) == eIsMutexTask;
}

bool TaskData::getIsUndeferredTask() const {
  return (metaData & eIsUndeferredTask) == eIsUndeferredTask;
}

bool TaskData::getIsUntiedTask() const {
  return (metaData & eIsUntiedTask) == eIsUntiedTask;
}
  
bool TaskData::getIsFinalTask() const {
  return (metaData & eIsFinalTask) == eIsFinalTask;
}
  
bool TaskData::getIsMergeableTask() const {
  return (metaData & eIsMergeableTask) == eIsMergeableTask;
}
  
bool TaskData::getIsInReduction() const {
  return (metaData & eIsInReduction) == eIsInReduction;
}

bool TaskData::getIsTaskwait() const {
  return (metaData & eIsTaskwait) == eIsTaskwait;
}

bool TaskData::getIsMergedTask() const {
  return (metaData & eIsMergeableTask) == eIsMergeableTask;
}

bool TaskData::getHasDependence() const {
  return (metaData & eHasDependence) == eHasDependence;
}

