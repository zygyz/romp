#pragma once
#include "mcs-lock.h"
#ifdef PERFORMANCE
#include "PerformanceCounters.h"
#endif

class LockGuard {
public:
  LockGuard(mcs_lock_t* lock, mcs_node_t* node): mLock(lock), mNode(node) {
   mcs_lock(mLock, mNode);
  }
  ~LockGuard() {
    mcs_unlock(mLock, mNode);
  }
private:
  mcs_lock_t* mLock;
  mcs_node_t* mNode;
};

#ifdef PERFORMANCE
class LockGuardWithPerformanceCounters {
public:
  LockGuardWithPerformanceCounters(mcs_lock_t* lock, mcs_node_t* node, PerformanceCounters& performanceCounters): mLock(lock), mNode(node) {
    if (mcs_trylock(mLock, mNode)) { 
      return;
    }
    mcs_lock(mLock, mNode);
    performanceCounters.bumpNumAccessHistoryContention();
  }
  ~LockGuardWithPerformanceCounters() {
    mcs_unlock(mLock, mNode);
  }
private:
  mcs_lock_t* mLock;
  mcs_node_t* mNode;
};
#endif


