#pragma once
#include "Label.h"
#include "LockSet.h"

/*
 * `Record` class stores a metadata associated with a single memory access.
 */
class Record {
public:
  Record(): mState(0), mLabel(nullptr), mLockSet(nullptr), 
    mTaskPtr(nullptr), mInstructionAddress(0){}
  Record(bool isWrite, 
         std::shared_ptr<Label> label, 
         std::shared_ptr<LockSet> lockSet,   
         void* taskPtr, 
         void* instructionAddress,
	 bool hasHardwareLock): 
      mLabel(label), mLockSet(lockSet), mTaskPtr(taskPtr), 
      mInstructionAddress(instructionAddress) { 
        setAccessType(isWrite); 
	setHasHardwareLock(hasHardwareLock);
      }
  void setAccessType(bool isWrite);
  void setHasHardwareLock(bool hardwareLock);
  bool isWrite() const;
  bool hasHardwareLock() const;
  std::string toString() const;
  Label* getLabel() const;
  LockSet* getLockSet() const;
  void* getInstructionAddress() const; 
  void* getTaskPtr() const;
private:
  uint8_t mState; // store state information
  std::shared_ptr<Label> mLabel; // task label associated with the record
  std::shared_ptr<LockSet> mLockSet; // lock set associated with the record
  void* mTaskPtr; // pointer to data of encountering task
  void* mInstructionAddress;  
};
