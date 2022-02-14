#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "mcs-lock.h"
#include "Record.h"

namespace romp {

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
  void clearRecords();
  void clearFlags();
  void clearFlag(AccessHistoryFlag flag);
  bool dataRaceFound() const;
  bool memIsRecycled() const;
  bool hasRecords() const;
  uint64_t getState() const;
  uint64_t getNumRecords() const;
private:
  void initializeRecordStorage();
private:
  mcs_lock_t mLock; 
  uint64_t mState;  
  std::unique_ptr<std::vector<Record>> mRecords; 
};

}
