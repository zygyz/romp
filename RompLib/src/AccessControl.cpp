#include "AccessControl.h"

LockGuard::LockGuard(mcs_lock_t* lock, mcs_node_t* node) {
  mLock = lock;
  mNode = node;
  mcs_lock(mLock, mNode);
}

LockGuard::~LockGuard() {
  mcs_unlock(mLock, mNode);
}

#ifdef PERFORMANCE

LockGuardWithPerformanceCounters::LockGuardWithPerformanceCounters(mcs_lock_t* lock, mcs_node_t* node, PerformanceCounters& performanceCounters) {
  mLock = lock;
  mNode = node;
  if (mcs_trylock(mLock, mNode)) { 
    return;
  }
  mcs_lock(mLock, mNode);
  performanceCounters.bumpNumAccessHistoryContention();
}

LockGuardWithPerformanceCounters::~LockGuardWithPerformanceCounters() {
  mcs_unlock(mLock, mNode);
}

#endif


