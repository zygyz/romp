#include "AccessControl.h"

LockGuard::LockGuard(mcs_lock_t* lock, mcs_node_t* node, PerformanceCounters* performanceCounters) {
  mLock = lock;
  mNode = node;
  if (mcs_trylock(mLock, mNode)) { 
    return;
  }
  mcs_lock(mLock, mNode);
  if (performanceCounters) {
    performanceCounters->bumpNumAccessControlContention();
  }
}

LockGuard::~LockGuard() {
  mcs_unlock(mLock, mNode);
}

bool upgradeFromReaderToWriter(pfq_rwlock_t* lock, pfq_rwlock_node_t* me, PerformanceCounters& performanceCounters) {
  auto lockUpgradeResult = pfq_rwlock_upgrade_from_read_to_write_lock(lock. me); 
#ifdef PERFORMANCE 
  if (lockUpgradeResult == upgraded_has_other_writer) {
    gPerformanceCounters.bumpNumAccessHistoryContention();
  }   
#endif
  return true; 
}
