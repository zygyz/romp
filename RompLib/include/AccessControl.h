#pragma once
#include "pfq-rwlock.h"
#include "PerformanceCounters.h"

class LockGuard {
public:
  LockGuard(mcs_lock_t* lock, mcs_node_t* node, PerformanceCounters* performanceCounters);
  ~LockGuard();
private:
  mcs_lock_t* mLock;
  mcs_node_t* mNode;
};

class ReaderWriterLockGuard {
public:
  ReaderWriterLockGuard(pfq_rwlock_t* lock, pfq_rwlock_node_t* node);
  ReaderWriterLockGuard(pfq_rwlock_t* lock, pfq_rwlock_node_t* node, PerformanceCounters* performanceCounters);
  ~ReaderWriterLockGuard();
  bool upgradeFromReaderToWriter();
private:
  pfq_rwlock_t* mLock;
  pfq_rwlock_node_t* mNode;
  bool mWriteLockAcquired;
  PerformanceCounters* mPerformanceCounters;
};
