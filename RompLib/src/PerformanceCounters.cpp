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

void PerformanceCounters::bumpNumAccessHistoryReadWriteContention() {
  mNumAccessHistoryReadWriteContention.fetch_add(1, std::memory_order_relaxed);
}

void PerformanceCounters::bumpNumAccessHistoryWriteWriteContention() {
  mNumAccessHistoryWriteWriteContention.fetch_add(1, std::memory_order_relaxed);
}

void PerformanceCounters::bumpNumAccessHistoryWriteReadContention() {
  mNumAccessHistoryWriteReadContention.fetch_add(1, std::memory_order_relaxed);
}

void PerformanceCounters::printPerformanceCounters() const {
  LOG(INFO) << "# Check Access Function Call: " << mNumCheckAccessFunctionCall.load();      
  LOG(INFO) << "# Access History Record Overflow (threshold=" << mAccessHistoryRecordThreshold << "):  " << mNumAccessHistoryOverflow.load();
  LOG(INFO) << "# Memory Access Instrumentation Call: " << mNumMemoryAccessInstrumentationCall.load();
  LOG(INFO) << "# Access Control Contention: " << mNumAccessControlContention.load();
  LOG(INFO) << "# Access Control Write Write Contention: " << mNumAccessHistoryWriteWriteContention.load();
  LOG(INFO) << "# Access Control Write Read Contention: " << mNumAccessHistoryWriteReadContention.load();
  LOG(INFO) << "# Access Control Read Write Contention: " << mNumAccessHistoryReadWriteContention.load();
}
