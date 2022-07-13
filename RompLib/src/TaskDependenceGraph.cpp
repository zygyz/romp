#include "TaskDependenceGraph.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>
#include <stack>

#include "TaskData.h"
/*
 * Given a task referred to by taskPtr, register its dependence in mDependences
 * mDependences is a map that takes the variable address as the key. The value is 
 * a vector of (taskPtr, dependence type) pair. Each time a new dependence
 * is added to the mDependences, it looks up all pairs in the corresponding vector 
 * using the variable as a key, find tasks that are generated before current
 * task (sibling tasks) and update the task graph edge depending on
 * the dependence type: 
 *  a. 'in': for any task with dependence type 'mutexinoutset', 'out', 'inout', 
 *      add an adge from that task to current task
 *  b. 'out' and 'inout': for any task with dependence type 'in', 'out', 
 *      'inout', 'mutexinoutset', add an edge from that task to current task
 *  c.  'mutexinoutset': for any task with dependence type 'in', 'out', 
 *       'inout', add an edge from that task to current task; for any task 
 *       dependence type 'mutexinoutset', form a pair of mutual exclusion task
 */
void TaskDependenceGraph::addDependence(void* taskPtr, const ompt_dependence_t& dependence) {
  // dependence variable is stored in ptr field
  auto variable = dependence.variable.ptr; 
  auto dependenceType = dependence.dependence_type;
  RAW_DLOG(INFO, "add task dependence, var: %lx, dep type: %d", variable, dependenceType);
  if (dependenceType == ompt_dependence_type_source || dependenceType == ompt_dependence_type_sink) {
    // ompt_dependence_type_source and ompt_dependence_type_sink are for ordered section. 
    return;
  }
  if (dependenceType == ompt_dependence_type_mutexinoutset) {
    auto taskData = static_cast<TaskData*>(taskPtr);
    taskData->setIsMutexTask(true);
  }
  // look up tasks that share the same dependence variable
  if (mDependences.find(variable) == mDependences.end()) {
    // no variable is found, it could be the first encountered task
    RAW_DLOG(INFO, "no variable is found %lx", variable);
    std::vector<std::pair<void*, ompt_dependence_type_t>> depVec;
    depVec.push_back(std::make_pair(taskPtr, dependenceType));
    mDependences[variable] = depVec;
  } else {
    // found the dependence variable, iterate over all pairs in the vector
    auto curTaskData = static_cast<TaskData*>(taskPtr);
    for (const auto& pair : mDependences[variable]) {
      auto otherTaskData = static_cast<TaskData*>(pair.first);
      auto otherDependenceType = pair.second;          
      if (dependenceType == ompt_dependence_type_in) {
        if (otherDependenceType == ompt_dependence_type_out || 
            otherDependenceType == ompt_dependence_type_inout || 
            otherDependenceType == ompt_dependence_type_mutexinoutset) {
          addEdge((void*)otherTaskData, (void*)curTaskData);
          RAW_DLOG(INFO, "add edge: other task data: %lx -> cur task data: %lx", otherTaskData, curTaskData);
        }
      } else if (dependenceType == ompt_dependence_type_out || dependenceType == ompt_dependence_type_inout) {
        if (otherDependenceType == ompt_dependence_type_in ||
            otherDependenceType == ompt_dependence_type_inout || 
            otherDependenceType == ompt_dependence_type_mutexinoutset) {
          addEdge((void*)otherTaskData, (void*)curTaskData);
          RAW_DLOG(INFO, "add edge: other task data: %lx -> cur task data: %lx", otherTaskData, curTaskData);
        }
      } else if (dependenceType == ompt_dependence_type_mutexinoutset) {
        if (otherDependenceType == ompt_dependence_type_in ||
            otherDependenceType == ompt_dependence_type_inout || 
            otherDependenceType == ompt_dependence_type_out) {
          addEdge((void*)otherTaskData, (void*)curTaskData);
          RAW_DLOG(INFO, "add edge: other task data: %lx -> cur task data: %lx", otherTaskData, curTaskData);
        }
      }
    }
  } 
}

void TaskDependenceGraph::addEdge(void* from, void* to) {
  if (mGraph.find(from) == mGraph.end()) {
    std::vector<void*> nodes;
    nodes.push_back(to);
    mGraph[from] = nodes;
  } else {
    mGraph[from].push_back(to);
  }
}

/*
 * Use dfs to find directed path. We assume the graph is DAG.
 */
bool TaskDependenceGraph::hasPath(void* src, void* dest) {
  std::unordered_map<void*, bool> visited;  
  if (mGraph.find(src) == mGraph.end()) {
    return false;	
  }
  std::stack<void*> todo;
  todo.push(src);
  while (!todo.empty()) {
    auto cur = todo.top();
    todo.pop(); 
    if (cur == dest) {
      return true;
    }
    if (visited.find(cur) != visited.end()) {
      continue; 
    }
    visited[cur] = true;
    for (const auto neighbor : mGraph[cur]) {
      if (visited.find(neighbor) == visited.end()) {
        todo.push(neighbor);
      }
    }
  }
  return false;
}
