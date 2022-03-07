#pragma once
#include <atomic>

class PerformanceCounters {
public: 
  PerformanceCounters(int accessHisotryRecordThreshold): 
          mAccessHistoryRecordThreshold(accessHisotryRecordThreshold)  {}
  void bumpNumCheckAccessFunctionCall();
  void bumpNumAccessHistoryOverflow(int numRecords);
  void bumpNumMemoryAccessInstrumentationCall();
  void bumpNumAccessControlContention();
  void printPerformanceCounters() const;
private:
  std::atomic_uint64_t mNumMemoryAccessInstrumentationCall;
  std::atomic_uint64_t mNumCheckAccessFunctionCall;
  std::atomic_uint64_t mNumAccessHistoryOverflow;
  std::atomic_uint64_t mNumAccessControlContention;
  int mAccessHistoryRecordThreshold;
};
