#include "PerformanceCounters.h"

void PerformanceCounters::bumpNumCheckAccessFunctionCall() {
  mNumCheckAccessFunctionCall++; 
}

void PerformanceCounters::bumpNumAccessHistoryOverflow(int numRecords) {
  if (numRecords > mAccessHistoryRecordThreshold) {
    mNumAccessHistoryOverflow++;
  }
}

