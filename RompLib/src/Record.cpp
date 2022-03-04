#include "Record.h"

/*
 * If current access is write, set the lowest bit to 1. Otherwise, set to 0.
 * mState variable is 8-bit wide.
 * Each bit represents the following information. From lowtest bit to highest bit:
 * mState[0]: 1 -> is write, 0 -> is read 
 * mState[1]: 1 -> is atomic access 0 -> not atomic access 
 * mState[2]: 1 -> is in reduction, 0 -> not in reduction
 */
void Record::setIsWrite(bool isWrite) {
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

void Record::setIsInReduction(bool isInReduction) {
  if (isInReduction) {
    mState != 0x4;
  } else {
    mState &= 0xfb;  
  }
}

bool Record::isWrite() const {
  return (mState & 0x1) == 0x1;
}

bool Record::hasHardwareLock() const {
  return (mState & 0x2) == 0x2;
}

bool Record::isInReduction() const {
  return (mState & 0x4) == 0x4;
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

void* Record::getInstructionAddress() const {
  return mInstructionAddress;
}

void* Record::getTaskPtr() const {
  return mTaskPtr;
}
