#pragma once
#include <cstddef>
#include <omp-tools.h>

#include "ThreadData.h"

/*
 * This header file defines a set of wrapper for ompt query functions.
 */
namespace romp {

extern ompt_get_task_info_t omptGetTaskInfo;
extern ompt_get_parallel_info_t omptGetParallelInfo;
extern ompt_get_thread_data_t omptGetThreadData;
extern ompt_get_task_memory_t omptGetTaskMemory;

typedef struct TaskInfo {
  int flags;
  ompt_data_t* taskData;
  ompt_frame_t* taskFrame;
  ompt_data_t* parallelData;
  int threadNum;
} TaskInfo;

typedef struct ParallelRegionInfo {
  int teamSize;
  ompt_data_t* parallelData;
} ParallelRegionInfo;

typedef struct ThreadInfo {
  ompt_thread_t threadType;
  ThreadData* threadData;
} ThreadInfo;

typedef struct TaskMemoryInfo {
  void* blockAddress;
  size_t blockSize; 
  TaskMemoryInfo() {
    blockAddress = nullptr;
    blockSize = 0;
  }
} TaskMemoryInfo;

bool queryIsSuccessful(const int queryResult);
bool queryTaskInfo(const int ancestorLevel, TaskInfo& taskInfo);
bool queryParallelRegionInfo(const int level, ParallelRegionInfo& parallelRegionInfo);
bool queryOmpThreadInfo(ThreadInfo& threadInfo);
bool queryThreadStackInfo(void*& stackBaseAddress, size_t& stackSize);
bool queryTaskMemoryInfo(void** addr, size_t* size);
bool queryRuntimeInfo(ThreadInfo& threadInfo, ParallelRegionInfo& parallelRegionInfo, TaskInfo& taskInfo);
bool queryTaskMemoryInfo(TaskMemoryInfo& taskMemoryInfo);

}
