#include "PerformanceCounters.h"

#include <glog/logging.h>

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

void PerformanceCounters::printPerformanceCounters() const {
  LOG(INFO) << "# Check Access Function Call: " << mNumCheckAccessFunctionCall.load();      
  LOG(INFO) << "# Access History Record Overflow (threshold=" << mAccessHistoryRecordThreshold << "):  " << mNumAccessHistoryOverflow.load();
  LOG(INFO) << "# Memory Access Instrumentation Call: " << mNumMemoryAccessInstrumentationCall.load();
}

