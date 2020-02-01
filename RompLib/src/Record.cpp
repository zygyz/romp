#include "Record.h"

namespace romp {
  
/*
 * If current access is write, set the lowest bit to 1. Otherwise, set to 0.
 * _state variable is 8-bit wide.
 */
void Record::setAccessType(bool isWrite) {
  if (isWrite) {
    _state |= 0x1;
  } else {
    _state &= 0xfe; 
  }
}

/*
 * If the current memory access is an atomic instruction, in x86, this is 
 * indicated as having hw lock in the instruction. We seperate this hw lock
 * from the software locks implemented with atomic instructions.
 * If there is hw lock, set second lowest bit to 1, otherwise, set to 0.
 */
void Record::setHasHwLock(bool hwLock) {
  if (hwLock) {
    _state |= 0x2;
  } else {
    _state &= 0xfd;
  }
}

bool Record::isWrite() const {
  return (_state & 0x1) == 0x1;
}

bool Record::hasHwLock() const {
  return (_state & 0x2) == 0x2;
}
/*
 * toString() is mainly for debugging
 */
std::string Record::toString() const {
  std::string result = "";
  auto labelStr = _label? _label->toString() : std::string("[empty label]");
  result += std::string("Label:") + labelStr;
  result += isWrite()? std::string("@write") : std::string("@read");
  return result;
}

Label* Record::getLabel() const {
  return _label? _label.get() : nullptr;
}

LockSet* Record::getLockSet() const {
  return _lockSet? _lockSet.get() : nullptr;
}

void* Record::getInstnAddr() const {
  return _instnAddr;
}

void* Record::getTaskPtr() const {
  return _taskPtr;
}
}
