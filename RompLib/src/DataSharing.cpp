#include "DataSharing.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>
#include <memory>
#include <omp-tools.h>

#include "AccessControl.h"
#include "AccessHistory.h"
#include "CoreUtil.h"
#include "PerformanceCounters.h"
#include "ShadowMemory.h"
#include "TaskData.h"
#include "TaskInfoQuery.h"
#include "ThreadData.h"

#define USER_SPACE_VIRTUAL_MEMORY_BOUND 0x00007fffffffffff //canonical form x86-64 VM layout 
#define MINIMUM_STACK_FRAME_SIZE 32

extern PerformanceCounters gPerformanceCounters; 

bool shouldCheckMemoryAccess(const ThreadInfo& threadInfo, 
                             const TaskMemoryInfo& taskMemoryInfo,
                             const TaskInfo& taskInfo,
                             const uint64_t memoryAddress,
                             const ompt_frame_t* taskFrame,
                             DataSharingType& dataSharingType) {
  if (isDuplicateMemoryAccess(memoryAddress, taskIdfo, isWrite)) {
    return false;
  }
  dataSharingType = analyzeDataSharingType(threadInfo, taskMemoryInfo, memoryAddress, taskFrame);
  return dataSharingType != eNonWorkerThread && dataSharingType != eInitialThread;
}

bool isDuplicateMemoryAccess(const uint64_t memoryAddress, const TaskInfo& taskInfo, bool isWrite) {
  const auto taskData = static_cast<TaskData*>(taskInfo.taskData->ptr);  
  if (taskData->duplicateMap.find(memoryAddress) == taskData->duplicateMap.end() || 
      (taskData->duplicateMap[memoryAddress] == false && isWrite)) {
    taskData->duplicateMap[memoryAddress] = isWrite;
    return false;
  } 
  return true;  
}

DataSharingType analyzeDataSharingType(const ThreadInfo& threadInfo, 
                                       const TaskMemoryInfo& taskMemoryInfo,
                                       const uint64_t memoryAddress,
                                       const ompt_frame_t* taskFrame) {
  // This function tries to infer data sharing property of the memory access to memoryAddress. 
  if (threadInfo.threadType == ompt_thread_other || threadInfo.threadType == ompt_thread_unknown) {
    // not worker thread that executes the program.
    return eNonWorkerThread;
  }
  if (threadInfo.threadType == ompt_thread_initial) {
    return eInitialThread; // we don't check memory accesses performed by initial thread 
  }
  if (!taskFrame || taskFrame->exit_frame.ptr == nullptr) {
    return eUnknown;
  } 

  const auto threadData = threadInfo.threadData;
  if (threadData == nullptr || 
      threadData->stackBaseAddress == nullptr || 
      threadData->stackTopAddress == nullptr) {
    // memory access is checked when thread has not called the thread begin callback yet.
    return eUnknown;  
  }
  if (memoryAddress > reinterpret_cast<const uint64_t>(threadData->stackTopAddress) ||
      memoryAddress < reinterpret_cast<const uint64_t>(threadData->stackBaseAddress)) {
    // memory address does not fall in current thread stack range 
    if (taskMemoryInfo.blockAddress != nullptr) {
      // check if the memory access is explicit task private 
      const auto taskPrivateMemoryBaseAddress = reinterpret_cast<const uint64_t>(taskMemoryInfo.blockAddress);
      const auto taskPrivateMemorySize = reinterpret_cast<const uint64_t>(taskMemoryInfo.blockSize);
      if (memoryAddress >= taskPrivateMemoryBaseAddress && memoryAddress <= taskPrivateMemoryBaseAddress + taskPrivateMemorySize) {
        // filter explicit task private memory locations. Memory accesses to these locations should be exclusive to the task. 
        return eExplicitTaskPrivate;
      }
    }
    return eNonThreadPrivate;
  }
  // now the memory access is within current thread's stack range. We want to figure out if the memory access is task private.
  auto enterFrame = taskFrame->enter_frame;
  auto exitFrame = taskFrame->exit_frame;
  // exitFrame.ptr is the upper bound of the current task's scratch space. 
  // enterFrame.ptr is set if current task creates children explicit tasks, if it is not set, enterFrame.ptr = 0
  if (enterFrame.ptr > exitFrame.ptr) {
    // enter frame is higher than exit frame  
    if (memoryAddress <= reinterpret_cast<const uint64_t>(exitFrame.ptr)) {
      return eThreadPrivateAccessCurrentTask;
    }  else {
      return eThreadPrivateAccessOtherTask; 
    } 
  } else if (memoryAddress <= reinterpret_cast<const uint64_t>(exitFrame.ptr) && memoryAddress >= reinterpret_cast<const uint64_t>(enterFrame.ptr)) {
    return eThreadPrivateAccessCurrentTask;
  } else {
    return eThreadPrivateAccessOtherTask;
  } 
  return eUnknown;  
}
