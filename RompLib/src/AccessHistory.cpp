#include "AccessHistory.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>

namespace romp {

void AccessHistory::_initRecords() {
  mRecords = std::make_unique<std::vector<Record>>();
}

mcs_lock_t & AccessHistory::getLock() {
  return mLock;
}

/*
 * If records pointer has not been initialized, initialize the record first.
 * Then return the raw pointer to the records vector.
 * We assume the access history is under mutual exclusion.
 */
std::vector<Record>* AccessHistory::getRecords() {
  if (!mRecords.get()) {
    _initRecords();
  }
  return mRecords.get();
}

void AccessHistory::setFlag(AccessHistoryFlag flag) {
  mState |= flag;
}

void AccessHistory::clearFlag(AccessHistoryFlag flag) {
  mState &= ~flag; 
}

void AccessHistory::clearFlags() {
  mState = 0; 
}

void AccessHistory::clearRecords() {
  if (mRecords) {
    mRecords->clear();
  }
}

bool AccessHistory::dataRaceFound() const {
  return (mState & eDataRaceFound) != 0;
}

bool AccessHistory::memIsRecycled() const {
  return (mState & eMemoryRecycled) != 0;
}

uint64_t AccessHistory::getState() const {
  return mState;
}

}
