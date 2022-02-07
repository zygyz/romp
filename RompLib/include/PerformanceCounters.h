#pragma once
#include <atomic>

class PerformanceCounters {
public: 
  PerformanceCounters(int accessHisotryRecordThreshold): 
          mAccessHistoryRecordThreshold(accessHisotryRecordThreshold)  {}
  void bumpNumCheckAccessFunctionCall();
  void bumpNumAccessHistoryOverflow(int numRecords);
  void printPerformanceCounters() const;
private:
  std::atomic_long mNumCheckAccessFunctionCall;
  std::atomic_long mNumAccessHistoryOverflow;
  int mAccessHistoryRecordThreshold;
};
