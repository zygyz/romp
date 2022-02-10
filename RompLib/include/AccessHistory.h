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
  AccessHistory() : _state(0) { mcs_init(&_lock); }
  mcs_lock_t& getLock();
  std::vector<Record>* getRecords();
  void setFlag(AccessHistoryFlag flag);
  void clearFlags();
  void clearFlag(AccessHistoryFlag flag);
  bool dataRaceFound() const;
  bool memIsRecycled() const;
  uint64_t getState() const;
private:
  void _initRecords();
private:
  mcs_lock_t _lock; 
  uint64_t _state;  
  std::unique_ptr<std::vector<Record>> _records; 

};

}
