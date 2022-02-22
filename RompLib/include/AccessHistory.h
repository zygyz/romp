#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "pfq-rwlock.h"
#include "Record.h"

#define ACCESS_RECORD_NUM_LIMIT 8

enum AccessHistoryFlag {
  eDataRaceFound = 0x1,
  eMemoryRecycled = 0x2,
};

enum AccessRecordState {
  eEmptyState = 0, // start state 
  eOneReaderState = 1,
  eOneWriterState = 2,
  eUndefinedState = 3,
};

class AccessHistory {
public: 
  AccessHistory(); 
  pfq_rwlock_t& getLock();
  std::vector<Record>* getRecords();
  void setFlag(AccessHistoryFlag flag);
  void clearRecords();
  void clearFlags();
  void clearFlag(AccessHistoryFlag flag);
  void addRecordToAccessHistory(const Record& record);
  void removeRecords(const std::vector<int>& recordsToBeRemoved);
  void setRecordState(AccessRecordState state);
  bool dataRaceFound() const;
  bool memIsRecycled() const;
  bool hasRecords() const;
  uint8_t getState() const;
  uint8_t getRecordState() const;
  uint64_t getNumRecords() const;
private:
  pfq_rwlock_t mLock; 
  uint8_t mState;  
  std::unique_ptr<std::vector<Record>> mRecords; 
  uint8_t mRecordState;
};
