#pragma once
#include "Label.h"
#include "LockSet.h"

/*
 * `Record` class stores a metadata associated with a single memory access.
 */
class Record {
public:
  Record(): mState(0), mLabel(nullptr), mLockSet(nullptr), 
    mTaskPtr(nullptr), mCheckedMemoryAddress(0){}
  Record(bool isWrite, 
         std::shared_ptr<Label> label, 
         std::shared_ptr<LockSet> lockSet,   
         void* taskPtr, 
         uint64_t checkedMemoryAddress,
	 bool hasHardwareLock, 
         bool isInReduction,
         int dataSharingType, 
         void* instructionAddress, 
         uint8_t workShareRegionId, 
         bool isTLSAccess
      ): 
      mLabel(label), mLockSet(lockSet), mTaskPtr(taskPtr), 
      mCheckedMemoryAddress(checkedMemoryAddress),
      mInstructionAddress(instructionAddress),
      mWorkShareRegionId(workShareRegionId)
      { 
        mState = 0;
        setAccessType(isWrite); 
	setHasHardwareLock(hasHardwareLock);
        setDataSharingType(dataSharingType);
        setIsInReduction(isInReduction);
        setIsTLSAccess(isTLSAccess);
      }
  void setAccessType(bool isWrite);
  void setDataSharingType(int dataSharingType);
  void setHasHardwareLock(bool hardwareLock);
  void setIsInReduction(bool isInReduction);
  void setIsTLSAccess(bool isTLSAccess);
  bool isWrite() const;
  bool isInReduction() const;
  bool hasHardwareLock() const;
  bool isTLSAccess() const;
  std::string toString() const;
  Label* getLabel() const;
  LockSet* getLockSet() const;
  uint64_t getCheckedMemoryAddress() const; 
  int getDataSharingType() const;
  void* getTaskPtr() const;
  void* getInstructionAddress() const;
  uint8_t getWorkShareRegionId() const;
private:
  uint8_t mState; // store state information
  uint8_t mWorkShareRegionId;
  std::shared_ptr<Label> mLabel; // task label associated with the record
  std::shared_ptr<LockSet> mLockSet; // lock set associated with the record
  void* mTaskPtr; // pointer to data of encountering task
  uint64_t  mCheckedMemoryAddress;  
  void* mInstructionAddress;  // store the instruction address associated with the memory access
   
};
