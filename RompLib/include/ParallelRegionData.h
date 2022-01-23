#pragma once
#include <omp-tools.h>

#include "McsLock.h"
#include "TaskDependenceGraph.h"

namespace romp {

typedef struct ParRegionData {
  void* dataPtr;  
  unsigned int numParallelism;
  int parallelFlag;
  McsLock lock;      
  std::atomic_int expTaskCount; 
  ParRegionData() { mcsInit(&lock); }
  ParRegionData(unsigned int n, int p): numParallelism(n), parallelFlag(p) {
    dataPtr = nullptr; 
    expTaskCount = 0;
    mcsInit(&lock);
  } 
  TaskDependenceGraph taskDepGraph;
} ParRegionData;

void maintainTaskDeps(const ompt_dependence_t& dependence, 
		      void* taskPtr, 
		      ParRegionData* parRegionData);

}
