#include "PerformanceCounters.h"

#include <glog/logging.h>

void PerformanceCounters::bumpNumCheckAccessFunctionCall() {
  mNumCheckAccessFunctionCall++; 
}

void PerformanceCounters::bumpNumAccessHistoryOverflow(int numRecords) {
  if (numRecords > mAccessHistoryRecordThreshold) {
    mNumAccessHistoryOverflow++;
  }
}

void PerformanceCounters::printPerformanceCounters() const {
  LOG(INFO) << "# Check Access Function Call: " << mNumCheckAccessFunctionCall.load();      
  LOG(INFO) << "# Access History Record Overflow (threshold=" << mAccessHistoryRecordThreshold << "):  " << mNumAccessHistoryOverflow.load();
}

