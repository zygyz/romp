#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "Record.h"

namespace romp {

enum AccessHistoryFlag {
  eDataRaceFound = 0x1,
  eMemoryRecycled = 0x2,
};

class AccessHistory {

public: 
  AccessHistory() : _state(0) {}
  std::mutex& getMutex();
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
  std::mutex _mutex; // use simple mutex, avoid premature optimization
  uint64_t _state;  
  std::unique_ptr<std::vector<Record>> _records; 

};

}
