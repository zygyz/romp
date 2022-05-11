#pragma once
#include <memory>
#include <vector>

class Label;
class LockSet;

typedef enum TaskFlag { 
  is_explicit_task = 0x0001,
  is_mutex_task = 0x0002,
  is_undeferred_task = 0x0004,
  is_untied_task = 0x0008,
  is_final_task = 0x0010,
  is_mergeable_task = 0x0020,
  is_in_reduction = 0x0040,
  is_taskwait = 0x0080,
  is_merged_task = 0x0100, 
  has_dependence = 0x0200,
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
  std::vector<void*> childrenExplicitTasksData;
  uint16_t metaData;
  TaskData();

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
