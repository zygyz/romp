#pragma once
#include <omp-tools.h>

#include "TaskInfoQuery.h"

enum DataSharingType { 
  eNonThreadPrivate = 0,
  eThreadPrivateAccessOtherTask = 1,
  eThreadPrivateAccessCurrentTask = 2,
  eTaskExitFrameNotSet = 3,
  eTaskPrivate = 4,
  eNonWorkerThread = 5,
  eThreadMetaDataNotSet = 6, 
  eInitialThread = 7,
  eOmpRuntime = 8,
  eUndefined = 9,
};

bool shouldCheckMemoryAccess(const ThreadInfo& threadInfo, const TaskMemoryInfo& taskMemoryInfo, const TaskInfo& taskInfo, const uint64_t memoryAddress, bool isWrite);
DataSharingType analyzeDataSharingType(const ThreadInfo& threadInfo, const TaskMemoryInfo& taskMemoryInfo, const uint64_t memoryAddress, const ompt_frame_t* taskFrame);
bool isDuplicateMemoryAccess(const uint64_t memoryAddress, const TaskInfo& taskInfo, bool isWrite);
void recycleTaskThreadStackMemory(void* taskData);
void recycleTaskPrivateMemory();
void recycleMemRange(void* lowerBound, void* higherBound);
