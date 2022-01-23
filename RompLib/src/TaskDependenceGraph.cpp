#include "TaskDependenceGraph.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>
#include <stack>

#include "TaskData.h"

namespace romp {

/*
 * Given a task referred to by taskPtr, register its dependence in _deps
 * _deps is a map that takes the variable address as the key. The value is 
 * a vector of (taskPtr, dependence type) pair. Each time a new dependence
 * is added to the _deps, it looks up all pairs in the corresponding vector 
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
void TaskDependenceGraph::addDeps(const ompt_dependence_t& deps,  void* taskPtr) {
  // dependence variable is stored in ptr field
  auto variable = deps.variable.ptr; 
  auto depType = deps.dependence_type;
  if (depType == ompt_dependence_type_source || 
      depType == ompt_dependence_type_sink) {
    RAW_LOG(WARNING, "dependence type is %d", depType);   
    return;
  }
  if (depType == ompt_dependence_type_mutexinoutset) {
    // Note this dependence type is not currently included in the omp library
    // so this flag is not set. We still implement this for being
    // future proof.
    auto taskData = static_cast<TaskData*>(taskPtr);
    taskData->isMutexTask = true;
  }
  // look up tasks that share the same dependence variable
  if (_deps.find(variable) == _deps.end()) {
    // no variable is found, it could be the first encountered task
    std::vector<std::pair<void*, ompt_dependence_type_t>>  depVec;
    depVec.push_back(std::make_pair(taskPtr, depType));
    _deps[variable] = depVec;
  } else {
    // found the dependence variable, iterate over all pairs in the vector
    auto curTaskData = static_cast<TaskData*>(taskPtr);
    for (const auto& pair : _deps[variable]) {
      auto otherTaskData = static_cast<TaskData*>(pair.first);
      auto otherTaskId = otherTaskData->expLocalId;
      RAW_DLOG(INFO, "variable: %lx task ptr: %lx exp id: %d", 
		      variable, otherTaskData, otherTaskId);
      auto curTaskId = curTaskData->expLocalId;   
      if (curTaskId > otherTaskId) {
        auto otherDepType = pair.second;          
        switch(depType) {
	  case ompt_dependence_type_in:
            if (otherDepType == ompt_dependence_type_out || 
                otherDepType == ompt_dependence_type_inout ||
                otherDepType == ompt_dependence_type_mutexinoutset) {
              addEdge((void*)otherTaskData, (void*)curTaskData);   
	    }	    
	    break;
	  case ompt_dependence_type_out:
	  case ompt_dependence_type_inout:
	    addEdge((void*)otherTaskData, (void*)curTaskData); 
	    break;
	  case ompt_dependence_type_mutexinoutset:
            if (otherDepType == ompt_dependence_type_in ||
	        otherDepType == ompt_dependence_type_inout ||
		otherDepType == ompt_dependence_type_out) {
              addEdge((void*)otherTaskData, (void*)curTaskData);
	    } 
	    break;
	  default:
	    break;
	}
      }
    }
  } 
}

void TaskDependenceGraph::addEdge(void* from, void* to) {
  if (_graph.find(from) == _graph.end()) {
    std::vector<void*> nodes;
    nodes.push_back(to);
    _graph[from] = nodes;
  } else {
    _graph[from].push_back(to);
  }
}

/*
 * Use dfs to find directed path. We assume the graph is DAG.
 */
bool TaskDependenceGraph::hasPath(void* from, void* to) {
  std::unordered_map<void*, bool> visited;  
  if (_graph.find(from) == _graph.end()) {
    return false;	
  }
  std::stack<void*> todo;
  todo.push(from);
  while (!todo.empty()) {
    auto cur = todo.top();
    todo.pop(); 
    if (cur == to) {
      return true;
    }
    if (visited.find(cur) != visited.end()) {
      continue; 
    }
    visited[cur] = true;
    for (const auto neighbor : _graph[cur]) {
      if (visited.find(neighbor) == visited.end()) {
        todo.push(neighbor);
      }
    }
  }
  return false;
}

}
