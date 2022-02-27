#pragma once

enum RecordManagementDecision{
  eNoOperation,
  eSkipAddCurrentRecord,
  eDeleteHistoryRecord,
};

enum NodeRelation {
  eSibling = 0,
  eNonSiblingParallel = 1,
  eHappensBefore = 2,
  eHasCommonLock = 3, 
};

enum LockRelation {
  eHasCommonLock = 0,
  eHistoryLockSetContainsCurrentLockSet = 1,
  eCurrentLockSetContainsHistoryLockSet = 2,
  eNoCommonLock = 3,
};

typedef struct RecordManagementInfo {
  bool historyAccessHappensBeforeCurrentAccess;
  NodeRelation nodeRelation; 
  LockRelation lockRelation; 
} RecordManagementInfo;
