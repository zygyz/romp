#pragma once
#include <atomic>
#include <unordered_map>
#define ADDR_MAX 0xffffffffffff

namespace romp {

/*
 * ThreadData stores information about thread. The pointer to this struct
 * is stored in the runtime data structure in openmp. It could be retrieved
 * by openmp runtime api.
 */
typedef struct ThreadData {
  void* stackBaseAddr;
  void* stackTopAddr;
  void* lowestAccessedAddr;
  std::atomic_uint64_t labelId;
  std::unordered_map<uint64_t, uint64_t> dupReadTable;
  std::unordered_map<uint64_t, uint64_t> dupWriteTable;
  
  ThreadData() : stackBaseAddr(nullptr), 
                 stackTopAddr(nullptr), 
                 lowestAccessedAddr((void*)ADDR_MAX) {}

  void setLowestAddr(void* addr) {
    lowestAccessedAddr = addr;
  }

  void resetLowestAddr() {
    lowestAccessedAddr = (void*)ADDR_MAX;
  }

  bool isDupRead(uint64_t memAddr, uint64_t labelId) {
    if (dupReadTable.find(memAddr) != dupReadTable.end() && 
        dupReadTable[memAddr] == labelId) {
      return true;
    } else {
      dupReadTable[memAddr] = labelId;
      return false;
    }
  }

  bool isDupWrite(uint64_t memAddr, uint64_t labelId) {
    if (dupWriteTable.find(memAddr) != dupWriteTable.end() && 
        dupWriteTable[memAddr] == labelId) {
      return true;
    } else {
      dupWriteTable[memAddr] = labelId;
      return false;
    }
  }
} ThreadData;

}
