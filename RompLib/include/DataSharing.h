#pragma once
#include <omp-tools.h>

#include "TaskInfoQuery.h"

enum DataSharingType { 
  eNonThreadPrivate = 0, 
  eThreadPrivateAccessOtherTask = 1,
  eThreadPrivateAccessCurrentTask = 2,
  eTaskExitFrameNotSet = 3,  // 0x11
  eExplicitTaskPrivate = 4,  // 0x100
  eNonWorkerThread = 5, // 0x101
  eThreadMetaDataNotSet = 6,  // 0x110
  eInitialThread = 7, // 0x111
  eUndefined = 8, // 0x1000
};

bool shouldCheckMemoryAccess(const ThreadInfo& threadInfo, const TaskMemoryInfo& taskMemoryInfo, const uint64_t memoryAddress, const ompt_frame_t* taskFrame, DataSharingType& dataSharingType);
DataSharingType analyzeDataSharingType(const ThreadInfo& threadInfo, const TaskMemoryInfo& taskMemoryInfo, const uint64_t memoryAddress, const ompt_frame_t* taskFrame);
void recycleTaskThreadStackMemory(void* taskData);
void recycleTaskPrivateMemory();
void recycleMemRange(void* lowerBound, void* higherBound);
