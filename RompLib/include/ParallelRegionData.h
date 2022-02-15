#pragma once
#include <omp-tools.h>

#include "pfq-rwlock.h"
#include "TaskDependenceGraph.h"

typedef struct ParallelRegionData {
  void* dataPtr;  
  unsigned int numParallelism;
  int parallelFlag;
  pfq_rwlock_t lock;      
  std::atomic_int expTaskCount; 
  ParallelRegionData() { pfq_rwlock_init(&lock); }
  ParallelRegionData(unsigned int n, int p): numParallelism(n), parallelFlag(p) {
    dataPtr = nullptr; 
    expTaskCount = 0;
    pfq_rwlock_init(&lock);
  } 
  TaskDependenceGraph taskDepGraph;
} ParallelRegionData;

void maintainTaskDeps(const ompt_dependence_t& dependence, void* taskPtr, ParallelRegionData* parRegionData);

