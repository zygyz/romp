#pragma once
#include <utility>
#include <Symtab.h>

#include "DataSharing.h"
#include "QueryFuncs.h"
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
/* 
 * Wrap all necessary information for data race checking.
 */
typedef struct CheckInfo {
  CheckInfo(AllTaskInfo& allTaskInfo, 
            void* instnAddr,
            void* taskPtr,
            int taskType,
            bool isWrite,
            bool hwLock,
            DataSharingType dataSharingType): 
                          allTaskInfo(std::move(allTaskInfo)), 
                          instnAddr(instnAddr),
                          taskPtr(taskPtr),
                          taskType(taskType),
                          isWrite(isWrite),
                          hwLock(hwLock),
                          dataSharingType(dataSharingType){}
  AllTaskInfo allTaskInfo;
  void* instnAddr;
  void* taskPtr;
  int taskType;
  bool isWrite;
  bool hwLock; 
  uint64_t byteAddress;
  DataSharingType dataSharingType;
} CheckInfo; 

bool prepareAllInfo(int& taskType, 
                    int& teamSize, 
                    int& threadNum, 
                    void*& curParRegionData,
                    void*& curThreadData,
                    AllTaskInfo& allTaskInfo);

void reportDataRaceWithLineInfo(const DataRaceInfo& dataRaceInfo,
                                Dyninst::SymtabAPI::Symtab* symtabHandle);

void reportDataRace(void* instnAddrPrev, void* instnAddrCur, uint64_t address);

void* computeAddressRangeEnd(void* baseAddr, size_t chunkSize);

void incrementLabelId();

}
