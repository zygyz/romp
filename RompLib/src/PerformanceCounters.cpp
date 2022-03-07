#include "PerformanceCounters.h"

#include <glog/logging.h>

#define ACCESS_HISTORY_RECORDS_THRESHOLD 8


PerformanceCounters gPerformanceCounters(ACCESS_HISTORY_RECORDS_THRESHOLD);

void PerformanceCounters::bumpNumCheckAccessFunctionCall() {
  mNumCheckAccessFunctionCall.fetch_add(1, std::memory_order_relaxed);
}

void PerformanceCounters::bumpNumAccessHistoryOverflow(int numRecords) {
  if (numRecords > mAccessHistoryRecordThreshold) {
    mNumAccessHistoryOverflow.fetch_add(1, std::memory_order_relaxed);
  }
}

void PerformanceCounters::bumpNumMemoryAccessInstrumentationCall() {
  mNumMemoryAccessInstrumentationCall.fetch_add(1, std::memory_order_relaxed);
}

void PerformanceCounters::bumpNumAccessControlContention() {
  mNumAccessControlContention.fetch_add(1, std::memory_order_relaxed);
}

void PerformanceCounters::updateMaximumAccessRecordsNum(uint64_t value_new) {
  // update the maximum value atomically  
  auto previous_value = mMaximumAccessRecordsNum.load(); 
  while (previous_value < value_new && !mMaximumAccessRecordsNum.compare_exchange_weak(previous_value, value_new));
}

void PerformanceCounters::bumpNumTotalAccessRecordsTraversed(uint64_t numRecordsTraversed) {
  mNumTotalAccessRecordsTraversed.fetch_add(numRecordsTraversed, std::memory_order_relaxed);
}

void PerformanceCounters::printPerformanceCounters() const {
  LOG(INFO) << "# Check Access Function Call: " << mNumCheckAccessFunctionCall.load();      
  LOG(INFO) << "# Access History Record Overflow (threshold=" << mAccessHistoryRecordThreshold << "):  " << mNumAccessHistoryOverflow.load();
  LOG(INFO) << "# Memory Access Instrumentation Call: " << mNumMemoryAccessInstrumentationCall.load();
  LOG(INFO) << "# Access Control Contention: " << mNumAccessControlContention.load();
  LOG(INFO) << "# Maximum Access Records Number: " << mMaximumAccessRecordsNum.load();
}
