#pragma once
#include <omp-tools.h>

#include "mcs-lock.h"
#include "TaskDependenceGraph.h"

typedef struct ParallelRegionData {
  void* dataPtr;  
  unsigned int numParallelism;
  int parallelFlag;
  mcs_lock_t lock;      
  std::atomic_int expTaskCount; 
  ParallelRegionData() { mcs_init(&lock); }
  ParallelRegionData(unsigned int n, int p): numParallelism(n), parallelFlag(p) {
    dataPtr = nullptr; 
    expTaskCount = 0;
    mcs_init(&lock);
  } 
  TaskDependenceGraph taskDepGraph;
} ParallelRegionData;

void maintainTaskDeps(const ompt_dependence_t& dependence, void* taskPtr, ParallelRegionData* parRegionData);

