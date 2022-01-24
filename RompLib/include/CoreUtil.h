#pragma once
#include <utility>
#include <Symtab.h>

#include "DataSharing.h"
#include "TaskInfoQuery.h"
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
  CheckInfo(TaskInfo& taskInfo, 
            void* instnAddr,
            void* taskPtr,
            int flags,
            bool isWrite,
            bool hardwareLock,
            DataSharingType dataSharingType): 
                          taskInfo(std::move(taskInfo)), 
                          instnAddr(instnAddr),
                          taskPtr(taskPtr),
                          taskType(flags),
                          isWrite(isWrite),
                          hardwareLock(hardwareLock),
                          dataSharingType(dataSharingType){}
  TaskInfo taskInfo;
  void* instnAddr;
  void* taskPtr;
  int taskType;
  bool isWrite;
  bool hardwareLock; 
  uint64_t byteAddress;
  DataSharingType dataSharingType;
} CheckInfo; 

bool queryRuntimeInfo(void*& curThreadData,
                    ParallelRegionInfo& parallelRegionInfo,
                    TaskInfo& taskInfo);

void reportDataRaceWithLineInfo(const DataRaceInfo& dataRaceInfo,
                                Dyninst::SymtabAPI::Symtab* symtabHandle);

void reportDataRace(void* instnAddrPrev, void* instnAddrCur, uint64_t address);

void* computeAddressRangeEnd(void* baseAddr, size_t chunkSize);

void incrementLabelId();

}
