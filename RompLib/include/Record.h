#pragma once
#include "Label.h"
#include "LockSet.h"


namespace romp {

/*
 * `Record` class stores a sync info associated with a single memory access.
 */
class Record {
  
public:
  Record(): _state(0), _label(nullptr), _lockSet(nullptr), 
    _taskPtr(nullptr), _instnAddr(0){}
  Record(bool isWrite, 
         std::shared_ptr<Label> label, 
         std::shared_ptr<LockSet> lockSet,   
         void* taskPtr, 
         void* instnAddr,
	 bool hasHwLock): 
      _label(label), _lockSet(lockSet), _taskPtr(taskPtr), 
      _instnAddr(instnAddr) { 
        setAccessType(isWrite); 
	setHasHwLock(hasHwLock);
      }
  void setAccessType(bool isWrite);
  void setHasHwLock(bool hwLock);
  bool isWrite() const;
  bool hasHwLock() const;
  std::string toString() const;
  Label* getLabel() const;
  LockSet* getLockSet() const;
  void* getInstnAddr() const; 
  void* getTaskPtr() const;
private:
  uint8_t _state; // store state information
  std::shared_ptr<Label> _label; // task label associated with the record
  std::shared_ptr<LockSet> _lockSet; // lock set associated with the record
  void* _taskPtr; // pointer to data of encountering task
  void* _instnAddr;
};


}

