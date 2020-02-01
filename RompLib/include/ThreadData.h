#pragma once
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
  ThreadData() : stackBaseAddr(nullptr), 
                 stackTopAddr(nullptr), 
                 lowestAccessedAddr((void*)ADDR_MAX) {}

  void setLowestAddr(void* addr) {
    lowestAccessedAddr = addr;
  }

  void resetLowestAddr() {
    lowestAccessedAddr = (void*)ADDR_MAX;
  }
} ThreadData;

}
