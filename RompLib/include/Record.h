#pragma once
#include "Label.h"
#include "LockSet.h"


namespace romp {

/*
 * `Record` class stores a sync info associated with a single memory access.
 */
class Record {
public:
  Record(): m_state(0), m_label(nullptr), m_lockSet(nullptr), 
    m_taskPtr(nullptr), m_instructionAddress(0){}
  Record(bool isWrite, 
         std::shared_ptr<Label> label, 
         std::shared_ptr<LockSet> lockSet,   
         void* taskPtr, 
         void* instructionAddress,
	 bool hasHardwareLock): 
      m_label(label), m_lockSet(lockSet), m_taskPtr(taskPtr), 
      m_instructionAddress(instructionAddress) { 
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
  uint8_t m_state; // store state information
  std::shared_ptr<Label> m_label; // task label associated with the record
  std::shared_ptr<LockSet> m_lockSet; // lock set associated with the record
  void* m_taskPtr; // pointer to data of encountering task
  void* m_instructionAddress;
};


}

