#pragma once
#include "mcs-lock.h"
#ifdef PERFORMANCE
#include "PerformanceCounters.h"
#endif

class LockGuard {
public:
  LockGuard(mcs_lock_t* lock, mcs_node_t* node);
  ~LockGuard();
private:
  mcs_lock_t* mLock;
  mcs_node_t* mNode;
};

#ifdef PERFORMANCE
class LockGuardWithPerformanceCounters {
public:
  LockGuardWithPerformanceCounters(mcs_lock_t* lock, mcs_node_t* node, PerformanceCounters& performanceCounters);
  ~LockGuardWithPerformanceCounters();
private:
  mcs_lock_t* mLock;
  mcs_node_t* mNode;
};
#endif


