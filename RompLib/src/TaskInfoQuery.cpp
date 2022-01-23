#include "TaskInfoQuery.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>
#include <pthread.h>


namespace romp {

/* 
 * Helper function to determine if the query function get available result.
 */
bool infoIsAvailable(const int retVal) { 
  if (retVal == 0) {
    // task does not exist
    return false; 
  } else if (retVal == 1) {
    // task exists at the specified ancestor level but the information 
    // is not available 
    RAW_LOG(WARNING, "task exists but info is not available");
    return false;
  } else if (retVal == 2) {
    // task exists at the specified ancestor level and the information
    // is available
    return true;
  } else {
    RAW_LOG(FATAL, "unknown return value");
    return false;
  }
}

/*
 * Query all openmp task information given the task level in one time.
 * This function could be called when multiple aspects of information about 
 * openmp task is needed. 
 */
bool queryAllTaskInfo(const int ancestorLevel, 
                      int& taskType,
                      int& threadNum,
                      AllTaskInfo& allTaskInfo) {
  auto retVal = omptGetTaskInfo(ancestorLevel, &taskType, 
          &allTaskInfo.taskData, &allTaskInfo.taskFrame, 
          &allTaskInfo.parallelData, &threadNum);
  return infoIsAvailable(retVal);
}

/*
 * Query openmp task information given the task level. If the information is
 * available, set dataPtr to the pointer to actual data, then return true. 
 * If the information is not available, set dataPtr to nullptr and return false. 
 */
bool queryTaskInfo(const int ancestorLevel,
                   int& taskType,
                   int& threadNum,
                   void*& dataPtr) {
  int retVal = -1;
  dataPtr = nullptr;
  ompt_data_t omptTaskData;
  auto taskDataPtr = &omptTaskData;
  auto taskDataPtrPtr = &taskDataPtr;   
  retVal = omptGetTaskInfo(ancestorLevel, &taskType, taskDataPtrPtr, NULL,
              NULL, &threadNum);
  dataPtr = taskDataPtr->ptr;
  if (!dataPtr || !infoIsAvailable(retVal)) {
    RAW_LOG(WARNING, "task data info is not available");
    return false;
  }
  return true; 
}

/*
 * Query openmp task's frame information given the task level. If the 
 * information is available, set 
 */
bool queryFrameInfo(const int ancestorLevel, 
                    int& taskType,
                    ompt_frame_t* omptFramePtr) {
  int retVal = -1;
  auto omptFramePtrPtr = &omptFramePtr;
  retVal = omptGetTaskInfo(ancestorLevel, &taskType, NULL, omptFramePtrPtr,
          NULL, NULL);
  if (!infoIsAvailable(retVal)) {
    RAW_LOG(WARNING, "ompt frame info is not available");
    return false;
  } 
  return true; 
} 

/*
 * Query openmp runtime information about the parallel region. 
 * On success, set dataPtr to pointer to parallel region data, and return true. 
 * Otherwise, set dataPtr to nullptr and return false.
 */
bool queryParallelInfo(
        const int ancestorLevel,
        int& teamSize,
        void*& dataPtr) {
  dataPtr = nullptr; 
  ompt_data_t omptParData;
  auto parDataPtr = &omptParData;
  auto parDataPtrPtr = &parDataPtr;
  auto retVal = omptGetParallelInfo(ancestorLevel, parDataPtrPtr, &teamSize);
  if (!infoIsAvailable(retVal)) {
    return false;
  }   
  dataPtr = parDataPtr->ptr;
  return true;
}

/*
 * Query openmp runtime information about the thread. 
 * If thread data pointer is not nullptr, return true and pass the pointer
 * to dataPtr. Otherwise, return false.
 */
bool queryOmpThreadInfo(void*& dataPtr) {
  dataPtr = nullptr;
  auto curThreadData = omptGetThreadData();
  if (!curThreadData || !(curThreadData->ptr)) {
    return false;
  }
  dataPtr = curThreadData->ptr;
  return true;
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
