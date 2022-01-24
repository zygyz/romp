#pragma once
#include <cstddef>
#include <omp-tools.h>

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

bool queryIsSuccessful(const int queryResult);
bool queryTaskInfo(const int ancestorLevel, TaskInfo& taskInfo);
bool queryParallelRegionInfo(const int level, ParallelRegionInfo& parallelRegionInfo);
bool queryFrameInfo(const int level, int& taskType, ompt_frame_t* framePtr);
void* queryOmpThreadInfo();
bool queryThreadStackInfo(void*& stackAddr, size_t& stackSize);
bool queryTaskMemoryInfo(void** addr, size_t* size);

}
