#pragma once
#include <omp-tools.h>

#include "pfq-rwlock.h"
#include "TaskDependenceGraph.h"

typedef struct ParallelRegionData {
  void* dataPtr;  
  unsigned int numParallelism;
  int parallelFlag;
  pfq_rwlock_t lock;      
  ParallelRegionData() { pfq_rwlock_init(&lock); }
  ParallelRegionData(unsigned int n, int p): numParallelism(n), parallelFlag(p) {
    dataPtr = nullptr; 
    pfq_rwlock_init(&lock);
  } 
  TaskDependenceGraph taskDependenceGraph;
  void maintainTaskDependence(void* taskPtr, const ompt_dependence_t& dependence);
} ParallelRegionData;



