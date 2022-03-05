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
         bool isInReduction): 
      mLabel(label), mLockSet(lockSet), mTaskPtr(taskPtr), 
      mCheckedMemoryAddress(checkedMemoryAddress) { 
        setIsWrite(isWrite); 
	setHasHardwareLock(hasHardwareLock);
        setIsInReduction(isInReduction);
      }
  void setIsWrite(bool isWrite);
  void setIsInReduction(bool isInReduction);
  void setHasHardwareLock(bool hardwareLock);
  bool isWrite() const;
  bool hasHardwareLock() const;
  bool isInReduction() const;
  std::string toString() const;
  Label* getLabel() const;
  LockSet* getLockSet() const;
  uint64_t getCheckedMemoryAddress() const; 
  void* getTaskPtr() const;
private:
  uint8_t mState; // store state information
  std::shared_ptr<Label> mLabel; // task label associated with the record
  std::shared_ptr<LockSet> mLockSet; // lock set associated with the record
  void* mTaskPtr; // pointer to data of encountering task
  uint64_t  mCheckedMemoryAddress;  
};
