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
  void updateMaximumAccessRecordsNum(uint64_t value_new); 
  void bumpNumTotalAccessRecordsTraversed(uint64_t numRecordsTraversed);
  void bumpNumSkipAddingCurrentRecord();
  void printPerformanceCounters() const;
private:
  std::atomic_uint64_t mNumMemoryAccessInstrumentationCall;
  std::atomic_uint64_t mNumCheckAccessFunctionCall;
  std::atomic_uint64_t mNumAccessHistoryOverflow;
  std::atomic_uint64_t mNumAccessControlContention;
  std::atomic_uint64_t mMaximumAccessRecordsNum;
  std::atomic_uint64_t mNumTotalAccessRecordsTraversed;
  std::atomic_uint64_t mNumSkipAddingCurrentRecord; 
  int mAccessHistoryRecordThreshold;
};
