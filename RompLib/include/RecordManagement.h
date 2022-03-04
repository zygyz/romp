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
  eHistoryLockSetContainsCurrentLockSet = 2,
  eCurrentLockSetContainsHistoryLockSet = 3,
  eNoCommonLock = 4,
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
