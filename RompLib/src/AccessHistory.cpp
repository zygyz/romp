#include "AccessHistory.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>

namespace romp {

AccessHistory::AccessHistory() {
  mState = 0;
  mcs_init(&mLock);
  initializeRecordStorage();
}

void AccessHistory::initializeRecordStorage() {
  mRecords = std::make_unique<std::vector<Record>>();
}

mcs_lock_t & AccessHistory::getLock() {
  return mLock;
}

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

bool AccessHistory::hasRecords() const {
  return mRecords && mRecords->size() > 0; 
}

uint64_t AccessHistory::getState() const {
  return mState;
}

}
