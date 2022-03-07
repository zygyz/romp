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
  void bumpNumAccessControlReadWriteContention();
  void bumpNumAccessControlWriteWriteContention();
  void bumpNumAccessControlWriteReadContention();
  void bumpNumAccessHistorySkipRemoveRecords();
  void printPerformanceCounters() const;
private:
  std::atomic_uint64_t mNumMemoryAccessInstrumentationCall;
  std::atomic_uint64_t mNumCheckAccessFunctionCall;
  std::atomic_uint64_t mNumAccessHistoryOverflow;
  std::atomic_uint64_t mNumAccessControlContention;
  std::atomic_uint64_t mNumAccessControlReadWriteContention;
  std::atomic_uint64_t mNumAccessControlWriteWriteContention;
  std::atomic_uint64_t mNumAccessControlWriteReadContention;
  std::atomic_uint64_t mNumAccessHistorySkipRemoveRecords;
  int mAccessHistoryRecordThreshold;
};
