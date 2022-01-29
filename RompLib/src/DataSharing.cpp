#include "DataSharing.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>
#include <memory>
#include <omp-tools.h>

#include "AccessHistory.h"
#include "CoreUtil.h"
#include "TaskInfoQuery.h"
#include "ShadowMemory.h"
#include "TaskData.h"
#include "ThreadData.h"

#define USER_SPACE_VIRTUAL_MEMORY_BOUND 0x00007fffffffffff //canonical form x86-64 VM layout 
#define MINIMUM_STACK_FRAME_SIZE 32

namespace romp {
 
bool shouldCheckMemoryAccess(const ThreadInfo& threadInfo, 
                             const void* memoryAddress,
                             const ompt_frame_t* taskFrame) {
  const auto dataSharingType = analyzeDataSharingType(threadInfo, memoryAddress, taskFrame);
  switch(dataSharingType) {
    case eNonThreadPrivate:
    case eThreadPrivateAccessOtherTask:
    case eThreadMetaDataNotSet: 
    case eTaskExitFrameNotSet:
    case eUndefined:
      return true;
    case eThreadPrivateAccessCurrentTask:
    case eTaskPrivate:
    case eNonWorkerThread:
      return false; 
  }
}

DataSharingType analyzeDataSharingType(const ThreadInfo& threadInfo, 
                                       const void* memoryAddress,
                                       const ompt_frame_t* taskFrame) {
  // This function tries to infer data sharing property of the memory access to memoryAddress. 
  // TODO: implement logic for checking if the memory access is for a 
  // task private data stored in explicit task's runtime data structure.
  if (threadInfo.threadType == ompt_thread_other || threadInfo.threadType == ompt_thread_unknown) {
    // not worker thread that executes the program.
    return eNonWorkerThread;
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

  const auto memoryAddressValue = reinterpret_cast<const uint64_t>(memoryAddress);
  if (memoryAddressValue > reinterpret_cast<const uint64_t>(threadData->stackTopAddress) ||
      memoryAddressValue < reinterpret_cast<const uint64_t>(threadData->stackBaseAddress)) {
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
  if (exitFrame.ptr && exitFrameFlags == ompt_frame_application && 
      memoryAddressValue > reinterpret_cast<const uint64_t>(exitFrame.ptr)) {
    // memory address is above the first procedure frame executing current task region. 
    return eThreadPrivateAccessOtherTask; 
  }
  if (enterFrame.ptr && enterFrameFlags == ompt_frame_application && 
      memoryAddressValue == reinterpret_cast<const uint64_t>(enterFrame.ptr)) {
    // we don't know how large the stack frame for enterFrame is. We can only use the frame base address.
    return eThreadPrivateAccessCurrentTask; 
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
    //std::unique_lock<std::mutex> guard(accessHistory->getMutex());
    McsNode node;
    LockGuard guard(&(accessHistory->getLock()), &node);
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
    RAW_LOG(FATAL, "cannot get thread info");
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
    RAW_LOG(INFO, "cannot get task private data memory info");
    return;
  }
  auto taskPrivateDataEnd = computeAddressRangeEnd(taskPrivateDataBase, 
          taskPrivateDataSize);
  recycleMemRange(taskPrivateDataBase, taskPrivateDataEnd);
}

}


