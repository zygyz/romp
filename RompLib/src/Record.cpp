#include "Record.h"

/*
 * If current access is write, set the lowest bit to 1. Otherwise, set to 0.
 * mState variable is 8-bit wide.
 */
void Record::setAccessType(bool isWrite) {
  if (isWrite) {
    mState |= 0x1;
  } else {
    mState &= 0xfe; 
  }
}

/*
 * If the current memory access is an atomic instruction, in x86, this is 
 * indicated as having hardware lock in the instruction. We seperate this hardware lock
 * from the software locks implemented with atomic instructions.
 * If there is hardware lock, set second lowest bit to 1, otherwise, set to 0.
 */
void Record::setHasHardwareLock(bool hardwareLock) {
  if (hardwareLock) {
    mState |= 0x2;
  } else {
    mState &= 0xfd;
  }
}

bool Record::isWrite() const {
  return (mState & 0x1) == 0x1;
}

bool Record::hasHardwareLock() const {
  return (mState & 0x2) == 0x2;
}
/*
 * toString() is mainly for debugging
 */
std::string Record::toString() const {
  std::string result = "";
  auto labelStr = mLabel? mLabel->toString() : std::string("[empty label]");
  result += std::string("Label:") + labelStr;
  result += isWrite()? std::string("@write") : std::string("@read");
  return result;
}

Label* Record::getLabel() const {
  return mLabel? mLabel.get() : nullptr;
}

LockSet* Record::getLockSet() const {
  return mLockSet? mLockSet.get() : nullptr;
}

uint64_t Record::getCheckedMemoryAddress() const {
  return mCheckedMemoryAddress;
}

void* Record::getTaskPtr() const {
  return mTaskPtr;
}
