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

void checkDataRace(AccessHistory* accessHistory, const LabelPtr& curLabel, const LockSetPtr& curLockSet, void* instnAddr, 
                   void* currentTaskData, int taskFlags, bool isWrite, bool hasHardwareLock, uint64_t checkedAddress) {
#ifdef PERFORMANCE
  gPerformanceCounters.bumpNumCheckAccessFunctionCall();
#endif
  pfq_rwlock_node_t me;
  ReaderWriterLockGuard guard(&(accessHistory->getLock()), &me, &gPerformanceCounters);
#ifdef PERFORMANCE
  gPerformanceCounters.bumpNumAccessHistoryOverflow(accessHistory->getNumRecords());
#endif
rollback: // will refactor to remove the tag 
  if (accessHistory->dataRaceFound()) {
    //  data race has already been found on this memory location, romp only 
    //  reports one data race on any memory location in one run. Once the data 
    //  race is reported, romp clears the access history with respect to this
    //  memory location and mark this memory location as found. Future access 
    //  to this memory location does not go through data race checking.
    if (!accessHistory->hasRecords()) {
      return; 
    }
    guard.upgradeFromReaderToWriter();
    if (accessHistory->hasRecords()) {
      accessHistory->clearRecords(); 
    }
    return;
  }
  if (accessHistory->memIsRecycled()) {
    //  The memory slot is recycled because of the end of explicit task. 
    //  reset the memory state flag and clear the access records.
    guard.upgradeFromReaderToWriter(); 
    if (accessHistory->memIsRecycled()) {
      accessHistory->clearFlags();
      accessHistory->clearRecords();
      return;
    }
  }
  auto curRecord = Record(isWrite, curLabel, curLockSet, currentTaskData, instnAddr, hasHardwareLock);
  if (!accessHistory->hasRecords()) {
    // no access record, add current access to the record
    auto hasWriteWriteContention = guard.upgradeFromReaderToWriter();
    if (!hasWriteWriteContention || hasWriteWriteContention && !accessHistory->hasRecords()) {
      accessHistory->addRecordToAccessHistory(curRecord);
      return;
    } else {
      goto rollback; 
    }
  }
  // check previous access records with current access
  auto records = accessHistory->getRecords();
  auto isHistBeforeCurrent = false;
  auto it = records->begin();
  std::vector<Record>::const_iterator cit;
  auto skipAddCurrentRecord = false;
  int diffIndex;
  std::vector<int> recordsToBeRemoved;
  while (it != records->end()) {
    cit = it; 
    auto histRecord = *cit;
    if (analyzeRaceCondition(histRecord, curRecord, isHistBeforeCurrent, diffIndex, checkedAddress)) {
      RAW_DLOG(INFO, "FOUND data race on: %lx hist: isWrite: %d %s cur: isWrite: %d %s", checkedAddress, histRecord.isWrite(), histRecord.getLabel()->toString().c_str(), curRecord.isWrite(), curLabel->toString().c_str());
      gDataRaceFound = true;
      accessHistory->setFlag(eDataRaceFound);
      return;
    }
    auto decision = manageAccessRecord(accessHistory->getRecordState(), histRecord, curRecord, isHistBeforeCurrent, diffIndex);
    if (decision == eSkipAddCurrentRecord) {
      skipAddCurrentRecord = true;
    } else if (decision == eDeleteHistoryRecord) {
      recordsToBeRemoved.push_back(it - records->begin());
    }
    it++;
  } 
  auto hasRecordsToRemove = !recordsToBeRemoved.empty();
  if (hasRecordsToRemove && accessHistory->getNumRecords() > ACCESS_RECORD_NUM_LIMIT) {
    if (!guard.upgradeFromReaderToWriter()) {
      RAW_DLOG(INFO, "remove access records, access history number: %d", accessHistory->getNumRecords());
      accessHistory->removeRecords(recordsToBeRemoved);
    }
  }
  if (!skipAddCurrentRecord) {
  // if we need to add current record to access history
  // we just add it to the access history and not rolling back. Because if there is 
  // write write contention, adding record to history is always safe 
    guard.upgradeFromReaderToWriter();
    accessHistory->addRecordToAccessHistory(curRecord); 
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
    auto checkedAddress = gUseWordLevelCheck ? reinterpret_cast<uint64_t>(baseAddress) + i * 4 : reinterpret_cast<uint64_t>(baseAddress) + i;      
    if (shouldCheckMemoryAccess(threadInfo, taskMemoryInfo, taskInfo, checkedAddress, isWrite)) {
      auto accessHistory = shadowMemory.getShadowMemorySlot(checkedAddress);
      checkDataRace(accessHistory, curLabel, curLockSet, instnAddr, static_cast<void*>(currentTaskData), taskInfo.flags, isWrite, hasHardwareLock, checkedAddress);
    }
  }
}
}
