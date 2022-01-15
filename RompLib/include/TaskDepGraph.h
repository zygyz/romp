#pragma once
#include <omp-tools.h>
#include <unordered_map>
#include <vector>

namespace romp {

/*
 * Class TaskDepGraph maintains a directed acylic graph using map.
 * Each node is represented by the pointer to task's allocated TaskData data
 * structure. There exists a directed edge from node a to node b if task b 
 * is dependent on task a, i.e., task a happens before task b
 */	
class TaskDepGraph {
    
public:
  TaskDepGraph() {}
  ~TaskDepGraph(){}
  void addDeps(const ompt_dependence_t& dependence, void* taskPtr);
  bool hasPath(void* from, void* to);
private:
  void addEdge(void* from, void* to);
  std::unordered_map<void*, 
	  std::vector<std::pair<void*, ompt_dependence_type_t>>> _deps;
  std::unordered_map<void*, std::vector<void*>> _graph;
};

}
