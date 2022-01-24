#include <experimental/filesystem>
#include <glog/logging.h>
#include <glog/raw_logging.h>
#include <limits.h>
#include <unistd.h>

#include "AccessHistory.h"
#include "Core.h"
#include "CoreUtil.h"
#include "DataSharing.h"
#include "Initialize.h"
#include "Label.h"
#include "LockSet.h"
#include "ShadowMemory.h"
#include "TaskData.h"
#include "ThreadData.h"

namespace fs = std::experimental::filesystem;

namespace romp {

using LabelPtr = std::shared_ptr<Label>;
using LockSetPtr = std::shared_ptr<LockSet>;

ShadowMemory<AccessHistory> shadowMemory;

/*
 * Driver function to do data race checking and access history management.
 */
void checkDataRace(AccessHistory* accessHistory, const LabelPtr& curLabel, 
                   const LockSetPtr& curLockSet, const CheckInfo& checkInfo) {
  McsNode node;
  LockGuard guard(&(accessHistory->getLock()), &node);
  auto dataSharingType = checkInfo.dataSharingType;
  if (dataSharingType == eThreadPrivateBelowExit || 
          dataSharingType == eStaticThreadPrivate) {
    return;
  }
  auto records = accessHistory->getRecords();
  if (accessHistory->dataRaceFound()) {
    //  data race has already been found on this memory location, romp only 
    //  reports one data race on any memory location in one run. Once the data 
    //  race is reported, romp clears the access history with respect to this
    //  memory location and mark this memory location as found. Future access 
    //  to this memory location does not go through data race checking.
    if (!records->empty()) {
      LOG(INFO) << "clearing records since we have found data race on this access history" << accessHistory;
      records->clear();
    }
    return;
  }
  if (accessHistory->memIsRecycled()) {
    //  The memory slot is recycled because of the end of explicit task. 
    //  reset the memory state flag and clear the access records.
     accessHistory->clearFlags();
     records->clear();
     return;
  }
  if (isDuplicateMemoryAccess(checkInfo)) {
    return;
  }
  auto curRecord = Record(checkInfo.isWrite, curLabel, curLockSet, 
          checkInfo.taskPtr, checkInfo.instnAddr, checkInfo.hardwareLock);
  if (records->empty()) {
    // no access record, add current access to the record
    records->push_back(curRecord);
  } else {
    // check previous access records with current access
    auto isHistBeforeCurrent = false;
    auto it = records->begin();
    std::vector<Record>::const_iterator cit;
    auto skipAddCur = false;
    int diffIndex;
    while (it != records->end()) {
      cit = it; 
      auto histRecord = *cit;
      if (analyzeRaceCondition(histRecord, curRecord, isHistBeforeCurrent, 
                  diffIndex)) {
        gDataRaceFound = true;
        gNumDataRace++;
        if (gReportLineInfo) {
          McsNode node;	
          LockGuard recordGuard(&gDataRaceLock, &node);
          gDataRaceRecords.push_back(DataRaceInfo(histRecord.getInstructionAddress(),
                                                  curRecord.getInstructionAddress(),
                                                  checkInfo.byteAddress));
        } else if (gReportAtRuntime) {
          reportDataRace(histRecord.getInstructionAddress(), curRecord.getInstructionAddress(),
                         checkInfo.byteAddress);
        }
        accessHistory->setFlag(eDataRaceFound);  
	break;
      }
      auto decision = manageAccessRecord(histRecord, curRecord, 
              isHistBeforeCurrent, diffIndex);
      if (decision == eSkipAddCur) {
        skipAddCur = true;
      }
      modifyAccessHistory(decision, records, it);
    }
    if (!skipAddCur) {
      records->push_back(curRecord); 
    }
  }
}

extern "C" {

ompt_start_tool_result_t* ompt_start_tool(
        unsigned int ompVersion,
        const char* runtimeVersion) {
  ompt_data_t data;
  static ompt_start_tool_result_t startToolResult = { 
      &omptInitialize, &omptFinalize, data}; 
  char result[PATH_MAX];
  auto count = readlink("/proc/self/exe", result, PATH_MAX);
  if (count == 0) {
    LOG(FATAL) << "cannot get current executable path";
  }
  auto appPath = std::string(result, count);
  LOG(INFO) << "ompt_start_tool on executable: " << appPath;
  auto success = Dyninst::SymtabAPI::Symtab::openFile(gSymtabHandle, appPath);
  if (!success) {
    LOG(FATAL) << "cannot parse executable into symtab: " << appPath;
  }
  return &startToolResult;
}

void checkAccess(void* baseAddress,
                 uint32_t bytesAccessed,
                 void* instnAddr,
                 bool hardwareLock,
                 bool isWrite) {
  if (!gOmptInitialized || bytesAccessed == 0) {
    return;
  }
  TaskInfo taskInfo;
  ParallelRegionInfo parallelRegionInfo;
  void* currentThreadData = nullptr;
  if (!queryRuntimeInfo(currentThreadData, parallelRegionInfo, taskInfo)) {
    RAW_LOG(FATAL, "failed to fetch openmp runtime information");
  }
  if (!taskInfo.taskData->ptr) {
    RAW_LOG(WARNING, "pointer to current task data is null");
    return;
  }
  if (taskInfo.flags == ompt_task_initial) { 
    // don't check data race for initial task
    return;
  }
  auto currentTaskData = static_cast<TaskData*>(taskInfo.taskData->ptr);
  currentTaskData->exitFrame = taskInfo.taskFrame->exit_frame.ptr;

  auto& curLabel = currentTaskData->label;
  auto& curLockSet = currentTaskData->lockSet;
  auto memUnitAccessed = gUseWordLevelCheck ? (1 + ((bytesAccessed - 1) / 4)) : // implementation of ceil(bytesAccessed / 4)
                         bytesAccessed; 
  auto dataSharingType = analyzeDataSharing(currentThreadData, baseAddress, 
                                           taskInfo.taskFrame);
  CheckInfo checkInfo(taskInfo,  instnAddr, 
          static_cast<void*>(currentTaskData), taskInfo.flags, isWrite, hardwareLock, dataSharingType);
  for (uint64_t i = 0; i < memUnitAccessed; ++i) {
    auto curAddress = gUseWordLevelCheck ? reinterpret_cast<uint64_t>(baseAddress) + i * 4 :
                                           reinterpret_cast<uint64_t>(baseAddress) + i;      
    auto accessHistory = shadowMemory.getShadowMemorySlot(curAddress);
    checkInfo.byteAddress = curAddress;
    checkDataRace(accessHistory, curLabel, curLockSet, checkInfo);
  }
}

}

}

