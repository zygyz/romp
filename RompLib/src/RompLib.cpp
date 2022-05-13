#include <experimental/filesystem>
#include <glog/logging.h>
#include <glog/raw_logging.h>
#include <limits.h>
#include <unistd.h>

#include "AccessControl.h"
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

using LabelPtr = std::shared_ptr<Label>;
using LockSetPtr = std::shared_ptr<LockSet>;

ShadowMemory<AccessHistory> shadowMemory;
extern PerformanceCounters gPerformanceCounters;

bool checkDataRace(AccessHistory* accessHistory, const LabelPtr& curLabel, const LockSetPtr& curLockSet, void* instnAddr, 
                   void* currentTaskData, int taskFlags, bool isWrite, bool hasHardwareLock, uint64_t checkedAddress) {
#ifdef PERFORMANCE
  gPerformanceCounters.bumpNumCheckAccessFunctionCall();
#endif
  mcs_node_t node; // major bottleneck
#ifdef PERFORMANCE
  LockGuard guard(&(accessHistory->getLock()), &node, &gPerformanceCounters);
#else
  LockGuard guard(&(accessHistory->getLock()), &node, nullptr);
#endif
  auto records = accessHistory->getRecords();
#ifdef PERFORMANCE
  auto numRecords = accessHistory->getNumRecords();
  gPerformanceCounters.bumpNumAccessHistoryOverflow(numRecords);
  gPerformanceCounters.updateMaximumAccessRecordsNum(accessHistory->getNumRecords()); 
#endif
  if (accessHistory->dataRaceFound()) {
    //  data race has already been found on this memory location, romp only 
    //  reports one data race on any memory location in one run. Once the data 
    //  race is reported, romp clears the access history with respect to this
    //  memory location and mark this memory location as found. Future access 
    //  to this memory location does not go through data race checking.
    if (accessHistory->hasRecords()) {
      accessHistory->clearRecords();
    }
    return true;
  }
  if (accessHistory->memIsRecycled()) {
    //  The memory slot is recycled because of the end of explicit task. 
    //  reset the memory state flag and clear the access records.
     accessHistory->clearFlags();
     accessHistory->clearRecords();
     return false;
  }

  auto curRecord = Record(isWrite, curLabel, curLockSet, currentTaskData, checkedAddress, hasHardwareLock);

  if (!accessHistory->hasRecords()) {
    // no access record, add current access to the record
    accessHistory->addRecordToAccessHistory(curRecord);
    return false;
  }
  // check previous access records with current access
  auto isHistBeforeCurrent = false;
  auto it = records->begin();
  std::vector<Record>::const_iterator cit;
  auto skipAddCurrentRecord = false;
  int diffIndex;
#ifdef PERFORMANCE
  uint64_t numAccessRecordsTraversed = 0; 
#endif
  auto dataRaceFound = false;
  while (it != records->end()) {
    cit = it; 
    auto histRecord = *cit;
#ifdef PERFORMANCE
    numAccessRecordsTraversed++;
#endif
    if (analyzeRaceCondition(histRecord, curRecord, isHistBeforeCurrent, diffIndex, checkedAddress)) {
      RAW_DLOG(INFO, "FOUND data race on: %lx hist: isWrite: %d %s cur: isWrite: %d %s", checkedAddress, histRecord.isWrite(), histRecord.getLabel()->toString().c_str(), curRecord.isWrite(), curLabel->toString().c_str());
      gDataRaceFound = true;
      accessHistory->setFlag(eDataRaceFound);
      dataRaceFound = true;
      break;
    }
    auto decision = manageAccessRecord(histRecord, curRecord, 
            isHistBeforeCurrent, diffIndex);
    if (decision == eSkipAddCurrentRecord) {
      skipAddCurrentRecord = true;
    }
    modifyAccessHistory(decision, records, it);
  }
#ifdef PERFORMANCE
  gPerformanceCounters.bumpNumTotalAccessRecordsTraversed(numAccessRecordsTraversed);
  if (skipAddCurrentRecord) {
    gPerformanceCounters.bumpNumSkipAddingCurrentRecord();
  } 
#endif
  if (!skipAddCurrentRecord) {
    accessHistory->addRecordToAccessHistory(curRecord); 
  }
  return dataRaceFound;
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
                 bool hasHardwareLock,
                 bool isWrite) {
#ifdef PERFORMANCE
  gPerformanceCounters.bumpNumMemoryAccessInstrumentationCall();
#endif
  if (!gOmptInitialized || bytesAccessed == 0) {
    return;
  }
  TaskInfo taskInfo;
  ParallelRegionInfo parallelRegionInfo;
  ThreadInfo threadInfo;
  if (!queryRuntimeInfo(threadInfo, parallelRegionInfo, taskInfo)) {
    RAW_LOG(FATAL, "failed to fetch openmp runtime information");
  }
  if (taskInfo.flags == ompt_task_initial) { 
    // don't check data race for initial task
    return;
  }
  if (!taskInfo.taskData->ptr) {
    RAW_LOG(WARNING, "pointer to current task data is null");
    return;
  }

  auto currentTaskData = static_cast<TaskData*>(taskInfo.taskData->ptr);
  currentTaskData->exitFrame = taskInfo.taskFrame->exit_frame.ptr;

  auto& curLabel = currentTaskData->label;
  auto& curLockSet = currentTaskData->lockSet;
  auto memUnitAccessed = gUseWordLevelCheck ? (1 + ((bytesAccessed - 1) / 4)) : bytesAccessed; // implementation of ceil(bytesAccessed / 4)
  TaskMemoryInfo taskMemoryInfo;
  queryTaskMemoryInfo(taskMemoryInfo);
  for (uint64_t i = 0; i < memUnitAccessed; ++i) {
    auto checkedAddress = gUseWordLevelCheck ? reinterpret_cast<uint64_t>(baseAddress) + i * 4 :
                                           reinterpret_cast<uint64_t>(baseAddress) + i;      
    if (shouldCheckMemoryAccess(threadInfo, taskMemoryInfo, checkedAddress, taskInfo.taskFrame)) {
      auto accessHistory = shadowMemory.getShadowMemorySlot(checkedAddress);
      if (checkDataRace(accessHistory, curLabel, curLockSet, instnAddr, static_cast<void*>(currentTaskData), taskInfo.flags, isWrite, hasHardwareLock, checkedAddress)) {
        return;
      }
    }
  }
}
}
