#pragma once

enum RecordManagementDecision{
  eNoOperation,
  eSkipAddCurrentRecord,
  eDeleteHistoryRecord,
};

enum NodeRelation {
  eUndefinedNodeRelation = 0,
  eSiblingParallel = 1,
  eNonSiblingParallel = 2,
  eHappensBefore = 3,
  eSameNode = 4,
};

enum LockRelation {
  eUndefinedLockRelation = 0,
  eHasCommonLock = 1,
  eHistoryLockSetContainsCurrentLockSetNonEmpty = 2, //note: both history lockset and current lockset have at lesat one lock 
  eCurrentLockSetContainsHistoryLockSetNonEmpty = 3,
  eNoCommonLock = 4, // both current and history access have at least one lock, just no common lock
  eHistoryNoLockCurrentHasLock = 6, 
  eCurrentNoLockHistoryHasLock = 7,
  eBothEmptyLock = 8,
};

enum OtherSynchronizationInfo {
  eUndefinedOtherSynchronizationInfo = 0,
  eInReduction = 1,
  eIsMutexTask = 2,
};

typedef struct RecordManagementInfo {
  NodeRelation nodeRelation; 
  LockRelation lockRelation; 
  OtherSynchronizationInfo otherSynchronizationInfo;
} RecordManagementInfo;
