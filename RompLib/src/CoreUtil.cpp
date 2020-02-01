#include "CoreUtil.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>
#include <string>
#include <vector>

using namespace Dyninst;
using namespace SymtabAPI;

namespace romp {

/*
 * Called by `checkAccess`. This function prepares all information 
 * for data race detection algorithm. This function does best effort to 
 * retrieve all necessary info. But consumer is still responsible for checking 
 * if the data is actually set. Return false if core information such as 
 * task data is not available.
 */
bool prepareAllInfo(int& taskType, 
                    int& teamSize, 
                    int& threadNum, 
                    void*& curParRegionData,
                    void*& curThreadData,
                    AllTaskInfo& allTaskInfo) {
  if (!queryParallelInfo(0, teamSize, curParRegionData)) {
    RAW_DLOG(INFO, "parallel region is not setup yet");
    return false;
  }
  if (!queryAllTaskInfo(0, taskType, threadNum, allTaskInfo)) {
    RAW_DLOG(INFO, "task data info is not available");
    // it is necessary to have parallel region set up 
    return false;
  }
  queryOmpThreadInfo(curThreadData);
  return true;
}

/*
 * Report data race with line information. The function uses symtabAPI's 
 * api to get line information. It incurs quite large overhead because of 
 * the inefficiency of parsing debug information everytime for every 
 * instruction address. 
 */
void reportDataRaceWithLineInfo(const DataRaceInfo& info, Symtab* symtab) {
  auto instnPrev = reinterpret_cast<uint64_t>(info.instnAddrPrev);
  auto instnCur = reinterpret_cast<uint64_t>(info.instnAddrCur);
  std::vector<LineNoTuple> linesPrev;
  std::vector<LineNoTuple> linesCur;
  std::string prevFileName;
  auto prevLine = -1;
  int prevColumn = -1;
  std::string curFileName;
  auto curLine = -1;
  auto curColumn = -1;
  symtab->getSourceLines(linesPrev, instnPrev);
  if (linesPrev.empty()) {
    RAW_LOG(WARNING, "cannot get source line info for instn addr: %lx", instnPrev);
  } else {
    prevFileName = linesPrev[0].getFile();
    prevLine = linesPrev[0].getLine();
    prevColumn = linesPrev[0].getColumn();
  }
  symtab->getSourceLines(linesCur, instnCur);
  if (linesCur.empty()) {
    RAW_LOG(WARNING, "cannot get source line info for instn addr: %lx", instnCur);
  } else {
    curFileName = linesCur[0].getFile();
    curLine = linesCur[0].getLine();
    curColumn = linesCur[0].getColumn();
  }
  if (!linesCur.empty() && !linesPrev.empty()) {
    RAW_LOG(INFO, "data race found at mem addr: \
      %lx\n %s@[%lx]line:%d col:%d vs %s@[%lx]line:%d col:%d ", info.memAddr, 
          prevFileName.c_str(), instnPrev, prevLine, prevColumn, 
          curFileName.c_str(), instnCur, curLine, curColumn);
  } else if (!linesCur.empty()) {
    RAW_LOG(INFO, "data race found at mem addr: %lx\n %s@[%lx]line:%d col:%d", 
            info.memAddr, curFileName.c_str(), instnCur, curLine, curColumn);
  } else {
    RAW_LOG(INFO, "data race found at mem addr: %lx\n %s@[%lx]line:%d col:%d", 
            info.memAddr, prevFileName.c_str(), instnPrev, prevLine, prevColumn);
  }
}

void reportDataRace(void* instnAddrPrev, void* instnAddrCur, uint64_t memAddr) {
  RAW_LOG(INFO, "instn addr: %p vs instn addr: %p @ %p", 
          instnAddrPrev, instnAddrCur, (void*)memAddr);
} 

/*
 * Given the base address and chunk size, return the address of the last byte.
 */
void* computeAddressRangeEnd(void* baseAddr, size_t chunkSize) {
  auto rangeEnd = reinterpret_cast<uint64_t>(baseAddr) + chunkSize - 1;
  return reinterpret_cast<void*>(rangeEnd);
}

}
