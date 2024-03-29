#include "AccessHistory.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>

AccessHistory::AccessHistory() {
  mState = 0;
  pfq_rwlock_init(&mLock);
  mRecords = std::make_unique<std::vector<Record>>();
}

void AccessHistory::setOwner(void* owner) {
  mOwner = owner;
}

void* AccessHistory::getOwner() const {
  return mOwner;
}

pfq_rwlock_t & AccessHistory::getLock() {
  return mLock;
}

std::vector<Record>* AccessHistory::getRecords() {
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

void AccessHistory::addRecordToAccessHistory(const Record& record) {
  if (!mRecords) {
    mRecords = std::make_unique<std::vector<Record>>();   
  }
  mRecords->push_back(record);
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

uint8_t AccessHistory::getState() const {
  return mState;
}

uint64_t AccessHistory::getNumRecords() const {
  return mRecords ? mRecords->size() : 0;
}

void AccessHistory::removeRecords(const std::vector<int>& recordsToBeRemoved) {
  if (!mRecords ||  mRecords->empty()) {
    return; 
  }
  if (recordsToBeRemoved.size() > mRecords->size()) {
    RAW_DLOG(WARNING, "records to be removed: %d record size: %d", recordsToBeRemoved.size(), mRecords->size());
    //TODO: fix record removal candidate number larger than record size problem.
    //For now we abort this removal 
    return; 
  }
  for (auto it = recordsToBeRemoved.rbegin(); it != recordsToBeRemoved.rend(); it++) {
    mRecords->erase(mRecords->begin() + *it);
  }  
}
