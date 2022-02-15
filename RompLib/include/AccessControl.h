#pragma once
#include "mcs-lock.h"
#include "PerformanceCounters.h"

class LockGuard {
public:
  LockGuard(mcs_lock_t* lock, mcs_node_t* node);
  LockGuard(mcs_lock_t* lock, mcs_node_t* node, PerformanceCounters* performanceCounters);
  ~LockGuard();
private:
  mcs_lock_t* mLock;
  mcs_node_t* mNode;
};
