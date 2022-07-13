#include "Record.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>


// mState bit allocation
// bit 0: access type (write/read)
// bit 1: hardware lock 
// bit 2: is in reduction
// bit 3: is TLS access
// bit 4-6: data shairng type (3 bits)
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

void Record::setIsInReduction(bool isInReduction) { 
  if (isInReduction) {
    mState |= 0x4; // 0b100    
  }
}

void Record::setIsTLSAccess(bool isTLSAccess) {
  if (isTLSAccess) {
    mState |= 0x8; // 0b1000
  } 
}

void Record::setDataSharingType(int dataSharingType) {
  mState |= (dataSharingType << 4);
}

int Record::getDataSharingType() const {
  return (int) (mState >> 4);
}

void* Record::getMemoryAddressOwner() const {
  return mOwner;
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

bool Record::isInReduction() const {
  return (mState & 0x4) == 0x4; 
}

bool Record::isTLSAccess() const {
  return (mState & 0x8) == 0x8;
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

void* Record::getInstructionAddress() const {
  return mInstructionAddress;
}

uint8_t Record::getWorkShareRegionId() const {
  return mWorkShareRegionId;
}
