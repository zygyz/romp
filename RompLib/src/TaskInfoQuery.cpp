#include "TaskInfoQuery.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>
#include <pthread.h>


namespace romp {

/* 
 * Helper function to determine if the query function get available result.
 */
bool queryIsSuccessful(const int queryResult) { 
  switch(queryResult) {
    case 0:
      return false;
    case 1:
      RAW_LOG(WARNING, "task exists but info is not available");
      return false;
    case 2:
      return true; 
    default:
      RAW_LOG(FATAL, "unknown query result value: %d", queryResult);  
  } 
  return false;
}

bool queryTaskInfo(const int ancestorLevel, TaskInfo& taskInfo) {
  auto queryResult = omptGetTaskInfo(ancestorLevel, &taskInfo.flags, 
                                     &taskInfo.taskData, &taskInfo.taskFrame, 
                                     &taskInfo.parallelData, &taskInfo.threadNum);
  return queryIsSuccessful(queryResult);
}


bool queryFrameInfo(const int ancestorLevel, 
                    int& taskType,
                    ompt_frame_t* omptFramePtr) {
  int retVal = -1;
  auto omptFramePtrPtr = &omptFramePtr;
  auto queryResult = omptGetTaskInfo(ancestorLevel, &taskType, NULL, omptFramePtrPtr,
          NULL, NULL);
  return queryIsSuccessful(queryResult);
} 

bool queryParallelRegionInfo(
        const int ancestorLevel,
        ParallelRegionInfo& info) {
  auto queryResult = omptGetParallelInfo(ancestorLevel, &info.parallelData, &info.teamSize);
  return queryIsSuccessful(queryResult);
}

void* queryOmpThreadInfo() {
  auto threadData = omptGetThreadData();
  return threadData? threadData->ptr : nullptr; 
}

/*
 * Query the stack base address and the stack size of the current thread.
 * On success, return true. Otherwise, return false and set stackAddr to 
 * nullptr and staskSize to 0.
 */
bool queryThreadStackInfo(void*& stackAddr, size_t& stackSize) {
  pthread_attr_t attr; 
  if (pthread_getattr_np(pthread_self(), &attr) != 0) {
    RAW_LOG(WARNING, "cannot get pthread attribute");
    return false;
  }
  if (pthread_attr_getstack(&attr, &stackAddr, &stackSize) != 0) {
    RAW_LOG(WARNING, "cannot get thread stack info");
    return false; 
  } 
  return true;
}

/*
 * Query the task private data memory associated with the explicit task.
 * The base address of the data memory is stored in `addr`. The size of the 
 * memory is stored in `size`.
 */
bool queryTaskMemoryInfo(void** addr, size_t* size) {
  return omptGetTaskMemory(addr, size, 0) == 1; 
}

}
