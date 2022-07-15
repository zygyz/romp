#include "AccessControl.h"
#include "PerformanceCounters.h"

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

ReaderWriterLockGuard::ReaderWriterLockGuard(pfq_rwlock_t* lock, pfq_rwlock_node_t* node, PerformanceCounters* performanceCounters) {
  mLock = lock;
  mNode = node;
  mPerformanceCounters = performanceCounters; 
  mWriteLockAcquired = false; 
  pfq_rwlock_read_lock(mLock, mPerformanceCounters);
}

ReaderWriterLockGuard::~ReaderWriterLockGuard() {
  if (mWriteLockAcquired) {
    pfq_rwlock_write_unlock(mLock, mNode);
  } else {
    pfq_rwlock_read_unlock(mLock);
  }
}

// return true if there exists write-write contention
bool ReaderWriterLockGuard::upgradeFromReaderToWriter() {
  if (mWriteLockAcquired) {
    return false;
  }
  mWriteLockAcquired = true;
  return pfq_rwlock_upgrade_from_read_to_write_lock(mLock, mNode, mPerformanceCounters); 
}
