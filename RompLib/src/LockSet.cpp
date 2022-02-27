#include "LockSet.h"

#include <algorithm>
#include <glog/logging.h>
#include <glog/raw_logging.h>

SmallLockSet::SmallLockSet() {
  for (int i = 0; i < 4; ++i) {
    _locks[i] = 0;
  }
  _numLocks = 0;
}

SmallLockSet::SmallLockSet(const SmallLockSet& lockset) {
  for (int i = 0; i < lockset._numLocks; ++i) {
    _locks[i] = lockset._locks[i];
  }
  _numLocks = lockset._numLocks;
}

std::string SmallLockSet::toString() const {
  std::stringstream stream;
  for (int i = 0; i < _numLocks; ++i) {
    stream << std::hex << _locks[i] << "|";
  }
  auto result = "<" + stream.str() + ">";
  return result;
}

void SmallLockSet::addLock(uint64_t lock) {
  if (_numLocks == 4) {
    RAW_LOG(FATAL, "number of nesting locks exceeds limit of 4");
  }  
  _locks[_numLocks] = lock; 
  _numLocks++;
}

/*
 * Compute the intersect of two set of locks
 * Return true if two set of locks have common lock
 * Return false otherwise
 */
bool SmallLockSet::hasCommonLock(const LockSet& other) const {
  auto otherLockSet = dynamic_cast<const SmallLockSet&>(other);
  auto otherNumLocks = otherLockSet._numLocks;
  for (int i = 0; i < _numLocks; ++i) {
    auto lock = _locks[i];
    for (int j = 0; j < otherNumLocks; ++j) {
      auto otherLock = otherLockSet._locks[j]; 
      if (otherLock == lock) {
        return true;
      } 
    } 
  }
  return false;
}

void* SmallLockSet::getLocks() {
  return static_cast<void*>(_locks);
}

void SmallLockSet::removeLock(uint64_t lock) {
  auto index = -1;
  for (int i = 0; i < _numLocks; ++i) {
    if (_locks[i] == lock) {
      index = i; 
      break;
    }
  }
  if (index == -1) {
    RAW_LOG(FATAL, "cannot find lock to delete: %lu", lock);
  }
  for (int i = index + 1; i < _numLocks; ++i) {
    _locks[i - 1] = _locks[i];
  }
  _numLocks--;
}

uint16_t SmallLockSet::getNumLocks() const {
  return _numLocks;
}

bool hasCommonLock(LockSet* lockSetA, LockSet* lockSetB) {
  if (lockSetA == nullptr) {
    return false;
  } 
  if (lockSetB == nullptr) {
    return false;
  }
  auto numLocksInLockSetA = lockSetA->getNumLocks();
  auto numLocksInLockSetB = lockSetB->getNumLocks();
  for (int i = 0; i < numLocksInLockSetA; ++i) {
    auto lock = static_cast<uint64_t*>
    for (int j = 0; j < numLocksInLockSetB; ++j) {
       
    }
  }
}
/*
 * Return true if lock set `me` is the subset of lock set `other`
 */
bool isSubset(LockSet* me, LockSet* other) {
  if (me == nullptr) { 
    return true;
  }
  if (other == nullptr) {
    return false; 
  }
  auto numLocksMe= me->getNumLocks();
  auto numLocksOther = other->getNumLocks();
  if (numLocksOther < numLocksMe) {
    return false;
  }
  auto meLocks = me->getLocks();
  auto otherLocks = other->getLocks();
  for (int i = 0; i < numLocksMe; ++i) {
    auto lock = static_cast<uint64_t*>(meLocks)[i];
    for (int j = 0; j < numLocksOther; ++j) {
      auto otherLock = static_cast<uint64_t*>(otherLocks)[j];
      if (otherLock != lock) {
        return false; 
      }
    } 
  }
  return true;
}

std::shared_ptr<LockSet> SmallLockSet::clone() const {
  return std::make_shared<SmallLockSet>(*this);
}
