#pragma once
#include <omp-tools.h>

#include "TaskInfoQuery.h"

enum DataSharingType { 
  // memory address not in current thread stack address range
  eNonThreadPrivate= 1,  
  // memory access in current thread address range and within current task stack frame range
  eThreadPrivateAccessOtherTask = 2, 
  // memory access in current thread address range and out of current task stack frame range
  eThreadPrivateAccessCurrentTask = 3, 
  // memory access in heap and within current task's private memory
  eExplicitTaskPrivate = 4,  
  // memory access that is likely to be not task private, we don't have enough information to infer the data sharing type
  eUnknown = 5,  
  // memory access that is performed by non worker thread.
  eNonWorkerThread = 6, 
  // memory access that is performend by initial thread.
  eInitialThread = 7, 
};

bool shouldCheckMemoryAccess(const ThreadInfo& threadInfo, const TaskMemoryInfo& taskMemoryInfo, const TaskInfo& taskInfo, const uint64_t memoryAddress, const ompt_frame_t* taskFrame, DataSharingType& dataSharingType, const bool isWrite);
DataSharingType analyzeDataSharingType(const ThreadInfo& threadInfo, const TaskMemoryInfo& taskMemoryInfo, const uint64_t memoryAddress, const ompt_frame_t* taskFrame);
bool isDuplicateMemoryAccess(const uint64_t memoryAddress, const TaskInfo& taskInfo, bool isWrite);
void recycleTaskThreadStackMemory(void* taskData);
void recycleTaskPrivateMemory();
void recycleMemRange(void* lowerBound, void* higherBound);
