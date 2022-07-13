#include "TaskInfoQuery.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>
#include <pthread.h>
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
  auto queryResult = omptGetTaskInfo(ancestorLevel, &taskInfo.flags, &taskInfo.taskData, 
                                     &taskInfo.taskFrame, &taskInfo.parallelData, &taskInfo.threadNum);
  return queryIsSuccessful(queryResult);
}

bool queryParallelRegionInfo(
        const int ancestorLevel,
        ParallelRegionInfo& info) {
  auto queryResult = omptGetParallelInfo(ancestorLevel, &info.parallelData, &info.teamSize);
  return queryIsSuccessful(queryResult);
}

bool queryOmpThreadInfo(ThreadInfo& threadInfo) {
  auto omptData = omptGetThreadData();
  if (omptData == nullptr) {
    RAW_LOG(FATAL, "cannot get thread data");
    return false;
  }
  threadInfo.threadType = (ompt_thread_t)(omptData->value);
  threadInfo.threadData = reinterpret_cast<ThreadData*>(omptData->ptr);
  return true;
}

/*
 * Query the stack base address and the stack size of the current thread.
 */
bool queryThreadStackInfo(void*& stackBaseAddress, size_t& stackSize) {
  stackBaseAddress = nullptr;
  stackSize = 0;
  pthread_attr_t attr; 
  if (pthread_getattr_np(pthread_self(), &attr) != 0) {
    RAW_LOG(WARNING, "cannot get pthread attribute");
    return false;
  }
  if (pthread_attr_getstack(&attr, &stackBaseAddress, &stackSize) != 0) {
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

bool queryRuntimeInfo(ThreadInfo& threadInfo, ParallelRegionInfo& parallelRegionInfo, TaskInfo& taskInfo) {
  if (!queryParallelRegionInfo(0, parallelRegionInfo)) {
    return false;
  }
  if (!queryTaskInfo(0, taskInfo)) {
    return false;
  }
  if (!queryOmpThreadInfo(threadInfo)) {
    return false;
  }
  return true;
}

bool queryTaskMemoryInfo(TaskMemoryInfo& taskMemoryInfo) {  
  // ompt_get_task_memory only supports blockNum = 0 in current implementation.
  return omptGetTaskMemory(&taskMemoryInfo.blockAddress, &taskMemoryInfo.blockSize, 0) == 1;
}
