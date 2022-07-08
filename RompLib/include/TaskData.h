#pragma once
#include <memory>
#include <vector>

class Label;
class LockSet;

typedef enum TaskFlag { 
  eIsExplicitTask = 0x0001,
  eIsMutexTask = 0x0002,
  eIsUndeferredTask = 0x0004,
  eIsUntiedTask = 0x0008,
  eIsFinalTask = 0x0010,
  eIsMergeableTask = 0x0020,
  eIsInReduction = 0x0040,
  eIsTaskwait = 0x0080,
  eIsMergedTask = 0x0100, 
  eHasDependence = 0x0200,
  eIsComplete = 0x0400,
} TaskFlag;

/*
 * TaskData struct records information related to a task.
 * A pointer to this struct is stored in openmp runtime 
 * data structure and could be retrieved through ompt query 
 * functions. Because each OpenMP task will get allocated 
 * one TaskData structure, compact data structure is preferred.
 */
typedef struct TaskData {
  std::shared_ptr<Label> label;
  std::shared_ptr<LockSet> lockSet;
  void* exitFrame;
  void* parallelRegionDataPtr;
  void* parentTaskDataPtr;  
  std::vector<void*> childrenExplicitTasks;
  std::vector<void*> undeferredTasks; // record the TaskData pointers for undeferred task encountered.
  uint16_t metaData;
  uint8_t workShareRegionId;
  TaskData();
  //TaskData(void* parentTaskData);

  void recordExplicitTaskData(TaskData*);
  void recordUndeferredTaskData(TaskData*);
  void setIsExplicitTask(bool);
  void setIsMutexTask(bool);
  void setIsUndeferredTask(bool);
  void setIsUntiedTask(bool);
  void setIsFinalTask(bool);
  void setIsMergeableTask(bool);
  void setIsInReduction(bool);
  void setIsTaskwait(bool);  
  void setIsMergedTask(bool);
  void setHasDependence(bool);
  void setIsComplete(bool);

  bool getIsExplicitTask() const;
  bool getIsMutexTask() const;
  bool getIsUndeferredTask() const;
  bool getIsUntiedTask() const;
  bool getIsFinalTask() const;
  bool getIsMergeableTask() const;
  bool getIsInReduction() const;
  bool getIsTaskwait() const;
  bool getIsMergedTask() const;
  bool getHasDependence() const;
} TaskData;
