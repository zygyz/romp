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
  void printPerformanceCounters() const;
private:
  std::atomic_long mNumMemoryAccessInstrumentationCall;
  std::atomic_long mNumCheckAccessFunctionCall;
  std::atomic_long mNumAccessHistoryOverflow;
  std::atomic_long mNumAccessControlContention;
  std::atomic_long mNumAccessControlReadWriteContention;
  std::atomic_long mNumAccessControlWriteWriteContention;
  std::atomic_long mNumAccessControlWriteReadContention;
  int mAccessHistoryRecordThreshold;
};
