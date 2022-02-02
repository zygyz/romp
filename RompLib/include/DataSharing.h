#pragma once
#include <omp-tools.h>

#include "TaskInfoQuery.h"

namespace romp {

enum DataSharingType { 
  eNonThreadPrivate,
  eThreadPrivateAccessOtherTask,
  eThreadPrivateAccessCurrentTask,
  eTaskExitFrameNotSet,
  eTaskPrivate,
  eNonWorkerThread,
  eThreadMetaDataNotSet, 
  eInitialThread,
  eOmpRuntime,
  eUndefined,
};

bool shouldCheckMemoryAccess(const ThreadInfo& threadInfo, const TaskMemoryInfo& taskMemoryInfo, const uint64_t memoryAddress, const ompt_frame_t* taskFrame);
DataSharingType analyzeDataSharingType(const ThreadInfo& threadInfo, const TaskMemoryInfo& taskMemoryInfo, const uint64_t memoryAddress, const ompt_frame_t* taskFrame);
void recycleTaskThreadStackMemory(void* taskData);
void recycleTaskPrivateMemory();
void recycleMemRange(void* lowerBound, void* higherBound);

}
