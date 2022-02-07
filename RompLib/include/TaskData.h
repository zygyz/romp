#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Label;
class LockSet;

/*
 * TaskData struct records information related to a task.
 * A pointer to this struct is stored in openmp runtime 
 * data structure and could be retrieved through ompt query 
 * functions.
 */
typedef struct TaskData {
  std::shared_ptr<Label> label;
  std::shared_ptr<LockSet> lockSet;
  bool inReduction;
  void* exitFrame;
  std::vector<void*> childrenExplicitTasksData;
  int expLocalId; // if the task is explicit, store its local id in par region
  bool isMutexTask;
  bool isExplicitTask; 
  std::unordered_map<std::string, bool> duplicateMap;
  TaskData() {
    label = nullptr;
    lockSet = nullptr;
    inReduction = false;
    exitFrame = nullptr;
    expLocalId = 0;
    isMutexTask = false;
    isExplicitTask = false;
  }
  
} TaskData;
