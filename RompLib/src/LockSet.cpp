#include "LockSet.h"

#include <algorithm>
#include <glog/logging.h>
#include <glog/raw_logging.h>

std::string LockSet::toString() const {
  std::stringstream stream;
  for (std::pair<uint64_t, uint64_t> element : mLock) {
    stream << std::hex << element.first << "|";
  }
  auto result = "<" + stream.str() + ">";
  return result;
}

LockSet::LockSet(const LockSet& lockSet) {
  for (std::pair<uint64_t, uint64_t> element : lockSet.mLock) {
    mLock[element.first] = element.second;
  }
}

bool LockSet::isEmpty() const {
  return mLock.empty();
}

void LockSet::addLock(uint64_t lock) {
  mLock[lock] = 1; 
}

void LockSet::removeLock(uint64_t lock) {
  mLock.erase(lock); 
}

bool hasCommonLockImpl(const LockSet& l1, const LockSet& l2) {
  for (std::pair<uint64_t, uint64_t> element : l1.mLock) {
    auto lock = element.first;
    if (l2.mLock.find(lock) != l2.mLock.end()) {
      return true;
    }
  }
  return false;
}

bool hasCommonLock(LockSet* l1, LockSet* l2) {
  if (l1 == nullptr || l2 == nullptr) {
    return false; 
  } 
  return hasCommonLockImpl(*l1, *l2);
}

// return true if l1 is subset of l2
bool isSubSetImpl(const LockSet& l1, const LockSet& l2) {
  auto numLockInL1 = l1.mLock.size(); 
  auto numLockInL2 = l2.mLock.size(); 
  if (numLockInL1 > numLockInL2) {
    return false;
  }
  for (std::pair<uint64_t, uint64_t> element : l1.mLock) {
    auto lock = element.first;
    if (l2.mLock.find(lock) == l2.mLock.end()) {
      return false; 
    }
  }
  return true;
}

// return true if l1 is subet of l2 and both l1 and l2 are not null
bool isSubSet(LockSet* l1, LockSet* l2) {
  if (l1 == nullptr || l2 == nullptr) {
    return false;
  }
  return isSubSetImpl(*l1, *l2); 
}

std::shared_ptr<LockSet> LockSet::clone() const {
  return std::make_shared<LockSet>(*this);
}
