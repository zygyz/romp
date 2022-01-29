#pragma once
#include <utility>
#include <Symtab.h>

#include "DataSharing.h"
/*
 * This header files defines a set of functions used in romp that are not 
 * closely related to the core data race detection algorithm. For example, 
 * we declare a function that is dedicated to calling ompt query wrapper 
 * functions to setup useful information on every call to the instrumented 
 * `checkAccess` function. 
 * The goal is to help cleaning up the core checking function code and to
 * ease the maintenance effort.
 */

namespace romp {

typedef struct DataRaceInfo {
  DataRaceInfo() {}
  DataRaceInfo(void* instnAddrPrev, void* instnAddrCur, uint64_t memAddr):
               instnAddrPrev(instnAddrPrev), instnAddrCur(instnAddrCur), 
               memAddr(memAddr) {}
  void* instnAddrPrev;
  void* instnAddrCur;
  uint64_t memAddr;
} DataRaceInfo;

void reportDataRaceWithLineInfo(const DataRaceInfo& dataRaceInfo,Dyninst::SymtabAPI::Symtab* symtabHandle);
void reportDataRace(void* instnAddrPrev, void* instnAddrCur, uint64_t address);
void* computeAddressRangeEnd(void* baseAddr, size_t chunkSize);
}
