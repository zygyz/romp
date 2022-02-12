#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "pfq-rwlock.h"
#include "Record.h"

enum AccessHistoryFlag {
  eDataRaceFound = 0x1,
  eMemoryRecycled = 0x2,
};

class AccessHistory {
public: 
  AccessHistory(); 
  pfq_rwlock_t& getLock();
  std::vector<Record>* getRecords();
  std::vector<Record>* peekRecords(); 
  void setFlag(AccessHistoryFlag flag);
  void clearRecords();
  void clearFlags();
  void clearFlag(AccessHistoryFlag flag);
  void addRecordToAccessHistory(const Record& record);
  bool dataRaceFound() const;
  bool memIsRecycled() const;
  bool hasRecords() const;
  uint64_t getNumRecords() const;
private:
  pfq_rwlock_t mLock; 
  std::unique_ptr<std::vector<Record>> mRecords; 
  uint8_t mState;
};
