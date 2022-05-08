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
                             const ompt_frame_t* taskFrame) {
  const auto dataSharingType = analyzeDataSharingType(threadInfo, taskMemoryInfo, memoryAddress, taskFrame);
  switch(dataSharingType) {
    case eNonThreadPrivate:
    case eThreadPrivateAccessOtherTask:
    case eThreadMetaDataNotSet: 
    case eTaskExitFrameNotSet:
    case eUndefined:
//      RAW_DLOG(INFO,  "should check memory address, data sharing type: %d, memory address: %lx", dataSharingType, memoryAddress);
      return true;
    case eThreadPrivateAccessCurrentTask:
    case eTaskPrivate:
    case eNonWorkerThread:
    case eOmpRuntime:
    case eInitialThread:
      RAW_DLOG(INFO,  "should not check memory address, data sharing type: %d, memory address: %lx", dataSharingType, memoryAddress);
      return false; 
  }
  RAW_LOG(FATAL, "unexpected data sharing type: %d", dataSharingType);
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
    return eInitialThread;
  }
  if (!taskFrame || taskFrame->exit_frame.ptr == nullptr) {
    return eTaskExitFrameNotSet;
  } 

  const auto threadData = threadInfo.threadData;
  if (threadData == nullptr || 
      threadData->stackBaseAddress == nullptr || 
      threadData->stackTopAddress == nullptr) {
    // memory access is checked when thread has not called the thread begin callback yet.
    return eThreadMetaDataNotSet;  
  }

  if (memoryAddress > reinterpret_cast<const uint64_t>(threadData->stackTopAddress) ||
      memoryAddress < reinterpret_cast<const uint64_t>(threadData->stackBaseAddress)) {
    // memory address does not fall in current thread stack range, must not be thread private access.
    return eNonThreadPrivate;
  }
  // now the memory access is within current thread's stack range.
  const auto enterFrame = taskFrame->enter_frame;
  const auto enterFrameFlags = taskFrame->enter_frame_flags;
  const auto exitFrame = taskFrame->exit_frame;
  const auto exitFrameFlags = taskFrame->exit_frame_flags;
  // note: definition of exit_frame and enter_frame from OpenMP spec doc.
  // The exit_frame field of an ompt_frame_t object contains information to identify the first procedure 
  // frame executing the task region.   
  // The enter_frame field of an ompt_frame_t object contains information to identify the latest still active 
  // procedure frame executing the task region before entering the OpenMP runtime implementation or before 
  // executing a different task.
  // enter_frame_flags field indicates that the provided frame information points to a runtime or an application frame address. 
  // The same fields also specify the kind of information that is provided to identify the frame, These fields are a disjunction of values in the ompt_frame_flag_t enumeration type
  if (exitFrame.ptr && exitFrameFlags == (ompt_frame_application | ompt_frame_framepointer) && 
      memoryAddress > reinterpret_cast<const uint64_t>(exitFrame.ptr)) {
    // memory address is above the first procedure frame executing current task region. 
    return eThreadPrivateAccessOtherTask; 
  }
  if (enterFrame.ptr && enterFrameFlags == (ompt_frame_application | ompt_frame_framepointer)  && 
      memoryAddress == reinterpret_cast<const uint64_t>(enterFrame.ptr)) {
    // we don't know how large the stack frame for enterFrame is. We can only use the frame base address.
    return eThreadPrivateAccessCurrentTask; 
  }
  if (taskMemoryInfo.blockAddress != nullptr) {
    const auto taskPrivateMemoryBaseAddress = reinterpret_cast<const uint64_t>(taskMemoryInfo.blockAddress);
    const auto taskPrivateMemorySize = reinterpret_cast<const uint64_t>(taskMemoryInfo.blockSize);
    if (memoryAddress >= taskPrivateMemoryBaseAddress && memoryAddress <= taskPrivateMemoryBaseAddress + taskPrivateMemorySize) {
      // filter explicit task private memory locations. Memory accesses to these locations should be exclusive to the task. 
      return eTaskPrivate;
    }
  }
  return eUndefined;  
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
