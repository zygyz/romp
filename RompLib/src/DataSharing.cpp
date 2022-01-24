#include "DataSharing.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>
#include <memory>

#include "AccessHistory.h"
#include "CoreUtil.h"
#include "TaskInfoQuery.h"
#include "ShadowMemory.h"
#include "TaskData.h"
#include "ThreadData.h"

#define STATIC_THREAD_PRIVATE_LOWER_BOUND  0xfff8000000000000
namespace romp {
  
/*
 * Analayze data sharing property of current memory access. 
 * Return the type of data sharing if the analysis is successful, 
 * otherwise return eUndefined. We assert threadDataPtr is not nullptr;
 * TODO: implement logic for checking if the memory access is for a 
 * task private data stored in explicit task's runtime data structure.
 */
DataSharingType analyzeDataSharing(const void* threadDataPtr, 
                                   const void* address,
                                   const ompt_frame_t* taskFrame) {
  if (!threadDataPtr) {
    RAW_LOG(ERROR, "thread data pointer is null");
    return eUndefined;
  }
  if (!taskFrame->exit_frame.ptr) {
    // note that exit_frame is a union
    RAW_LOG(WARNING, "exit frame is not set");      
    return eUndefined;
  }
  const auto curExitFrameAddr = taskFrame->exit_frame.ptr;
  const auto threadData = reinterpret_cast<const ThreadData*>(threadDataPtr);
  const auto stackTopAddr = threadData->stackTopAddr;
  const auto stackBaseAddr = threadData->stackBaseAddr;
  if (!stackTopAddr || !stackBaseAddr) {
    RAW_LOG(INFO, "thread stack bound is not completely set");
    return eUndefined;
  }
  const auto addressValue = reinterpret_cast<const uint64_t>(address);
  if (addressValue >= static_cast<const uint64_t>(
                  STATIC_THREAD_PRIVATE_LOWER_BOUND)) {
    return eStaticThreadPrivate;  
  }
  if (addressValue < reinterpret_cast<const uint64_t>(stackBaseAddr) || 
      addressValue > reinterpret_cast<const uint64_t>(stackTopAddr)) {
    // Current memory access falls out of the thread stack's 
    // top and bottom boundary. Then the memory access is a 
    // non thread private access.
    return eNonThreadPrivate;
  } 
  if (addressValue < reinterpret_cast<const uint64_t>(curExitFrameAddr)) {
    return eThreadPrivateBelowExit;
  } else {
    return eThreadPrivateAboveExit;
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
  ompt_frame_t omptFrame;
  int taskType = 0;
  if (!queryFrameInfo(0, taskType, &omptFrame)) {
    RAW_LOG(FATAL, "cannot get frame info");
    return;
  }
  auto taskDataPtr = static_cast<TaskData*>(taskData);
  auto exitFrameAddr = taskDataPtr->exitFrame;
  auto threadData = queryOmpThreadInfo();
  if (threadData == nullptr) {
    RAW_LOG(FATAL, "cannot get thread info");
    return;
  }
  auto threadInfoPtr = static_cast<ThreadData*>(threadData);
  auto threadStackBase = threadInfoPtr->stackBaseAddr; 
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
  RAW_DLOG(INFO, "task private mem base: %lx, task private data size: %lu",
           taskPrivateDataBase, taskPrivateDataSize); 
  auto taskPrivateDataEnd = computeAddressRangeEnd(taskPrivateDataBase, 
          taskPrivateDataSize);
  recycleMemRange(taskPrivateDataBase, taskPrivateDataEnd);
}

}


