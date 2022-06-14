#pragma once
#include <omp-tools.h>

#include "mcs-lock.h"
#include "TaskDependenceGraph.h"

typedef struct ParallelRegionData {
  void* dataPtr;  
  unsigned int numParallelism;
  int parallelFlag;
  mcs_lock_t lock;      
  ParallelRegionData() { mcs_init(&lock); }
  ParallelRegionData(unsigned int n, int p): numParallelism(n), parallelFlag(p) {
    dataPtr = nullptr; 
    mcs_init(&lock);
  } 
  TaskDependenceGraph taskDependenceGraph;
  void maintainTaskDependence(void* taskPtr, const ompt_dependence_t& dependence);
} ParallelRegionData;



