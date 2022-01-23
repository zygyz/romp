#include "Record.h"

namespace romp {
  
/*
 * If current access is write, set the lowest bit to 1. Otherwise, set to 0.
 * m_state variable is 8-bit wide.
 */
void Record::setAccessType(bool isWrite) {
  if (isWrite) {
    m_state |= 0x1;
  } else {
    m_state &= 0xfe; 
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
    m_state |= 0x2;
  } else {
    m_state &= 0xfd;
  }
}

bool Record::isWrite() const {
  return (m_state & 0x1) == 0x1;
}

bool Record::hasHardwareLock() const {
  return (m_state & 0x2) == 0x2;
}
/*
 * toString() is mainly for debugging
 */
std::string Record::toString() const {
  std::string result = "";
  auto labelStr = m_label? m_label->toString() : std::string("[empty label]");
  result += std::string("Label:") + labelStr;
  result += isWrite()? std::string("@write") : std::string("@read");
  return result;
}

Label* Record::getLabel() const {
  return m_label? m_label.get() : nullptr;
}

LockSet* Record::getLockSet() const {
  return m_lockSet? m_lockSet.get() : nullptr;
}

void* Record::getInstructionAddress() const {
  return m_instructionAddress;
}

void* Record::getTaskPtr() const {
  return m_taskPtr;
}
}
