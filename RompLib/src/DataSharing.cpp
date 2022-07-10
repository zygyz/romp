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
                             const uint64_t memoryAddress,
                             const ompt_frame_t* taskFrame,
                             DataSharingType& dataSharingType) {
  dataSharingType = analyzeDataSharingType(threadInfo, taskMemoryInfo, memoryAddress, taskFrame);
  auto shouldCheck = dataSharingType != eNonWorkerThread && dataSharingType != eInitialThread;
  if (!shouldCheck) {
    RAW_DLOG(INFO, "not checking mem addr: %lx", memoryAddress);
  }
  return shouldCheck;
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

/*
 * This function is responsible for marking memory ranges in 
 * [lowerBound, upperBound] to be deallocated.
 */
void recycleMemRange(void* lowerBound, void* upperBound) {
  if (upperBound < lowerBound) {
    RAW_LOG(WARNING, "upper bound is smaller than lower bound: %lx %lx, abort recycling", 
            upperBound, lowerBound);
    return;
  }
  auto start = reinterpret_cast<uint64_t>(lowerBound);
  auto end = reinterpret_cast<uint64_t>(upperBound);
  ShadowMemory<AccessHistory> shadowMemory;
  for (auto addr = start; addr <= end; addr++) {
    auto accessHistory = shadowMemory.getShadowMemorySlot(addr);
    mcs_node_t node;
#ifdef PERFORMANCE
    LockGuard guard(&(accessHistory->getLock()), &node, &gPerformanceCounters);
#else
    LockGuard guard(&(accessHistory->getLock()), &node, nullptr);
#endif
    accessHistory->setFlag(eMemoryRecycled);
  }
}

/*
 * This function is called when an explicit task is completed. This function 
 * looks up the task private thread stack memory locations and marks these 
 * locations as deallocated. The lower bound is the base of thread stack.
 */
void recycleTaskThreadStackMemory(void* taskData) {
  auto taskDataPtr = static_cast<TaskData*>(taskData);
  auto exitFrameAddr = taskDataPtr->exitFrame;
  ThreadInfo threadInfo; 
  if (!queryOmpThreadInfo(threadInfo)) {
    return;
  }
  auto threadData = threadInfo.threadData;
  if (threadData == nullptr) {
    RAW_LOG(FATAL, "cannot get thread data");
    return;
  }
  auto threadInfoPtr = static_cast<ThreadData*>(threadData);
  auto threadStackBase = threadInfoPtr->stackBaseAddress; 
  auto lowerBound = threadStackBase; 
  auto upperBound = exitFrameAddr;
  recycleMemRange(lowerBound, upperBound);
}

/*
 * This function is called when an explicit task is completed or is switched
 * out for other tasks. This function looks up the private memory allocated 
 * for the explicit task on heap, marks these memory location as deallocated.
 */
void recycleTaskPrivateMemory() {
  void* taskPrivateDataBase = nullptr;
  size_t taskPrivateDataSize = 0;
  if (!queryTaskMemoryInfo(&taskPrivateDataBase, &taskPrivateDataSize)) {
    return;
  }
  auto taskPrivateDataEnd = computeAddressRangeEnd(taskPrivateDataBase, 
          taskPrivateDataSize);
  recycleMemRange(taskPrivateDataBase, taskPrivateDataEnd);
}
