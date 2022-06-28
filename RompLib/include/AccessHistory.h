#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "mcs-lock.h"
#include "Record.h"

enum AccessHistoryFlag {
  eDataRaceFound = 0x1,
  eMemoryRecycled = 0x2,
};

class AccessHistory {
public: 
  AccessHistory(); 
  mcs_lock_t& getLock();
  std::vector<Record>* getRecords();
  void setFlag(AccessHistoryFlag flag);
  void setOwner(void* owner);
  void clearRecords();
  void clearFlags();
  void clearFlag(AccessHistoryFlag flag);
  void addRecordToAccessHistory(const Record& record);
  bool dataRaceFound() const;
  bool memIsRecycled() const;
  bool hasRecords() const;
  uint64_t getNumRecords() const;
  void* getOwner() const;
private:
  mcs_lock_t mLock; 
  std::unique_ptr<std::vector<Record>> mRecords; 
  uint8_t mState;
  void* mOwner;  // if the memory address is for a stack-allocated variable, record its owner.
};
