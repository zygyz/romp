#pragma once
#include <omp-tools.h>

namespace romp {

enum DataSharingType { 
    eStaticThreadPrivate, 
    eNonThreadPrivate,
    eThreadPrivateBelowExit, 
    eThreadPrivateAboveExit,
    eUndefined,
};

DataSharingType analyzeDataSharing(const void* threadDataPtr, 
                                   const void* address,
                                   const ompt_frame_t* taskFrame);

void recycleTaskThreadStackMemory(void* taskData);
void recycleTaskPrivateMemory();
void recycleMemRange(void* lowerBound, void* higherBound);

}
