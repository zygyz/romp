#pragma once
#include <string>
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
  std::unordered_map<uint64_t, std::string> dupReadTable;
  std::unordered_map<uint64_t, std::string> dupWriteTable;
  ThreadData() : stackBaseAddr(nullptr), 
                 stackTopAddr(nullptr), 
                 lowestAccessedAddr((void*)ADDR_MAX) {}

  void setLowestAddr(void* addr) {
    lowestAccessedAddr = addr;
  }

  void resetLowestAddr() {
    lowestAccessedAddr = (void*)ADDR_MAX;
  }

  bool isDupRead(uint64_t memAddr, const std::string& labelStr) {
    if (dupReadTable.find(memAddr) != dupReadTable.end() && 
        dupReadTable[memAddr] == labelStr) {
      return true;
    } else {
      dupReadTable[memAddr] = labelStr;
      return false;
    }
  }

  bool isDupWrite(uint64_t memAddr, const std::string& labelStr) {
    if (dupWriteTable.find(memAddr) != dupWriteTable.end() && 
        dupWriteTable[memAddr] == labelStr) {
      return true;
    } else {
      dupWriteTable[memAddr] = labelStr;
      return false;
    }
  }
} ThreadData;

}
