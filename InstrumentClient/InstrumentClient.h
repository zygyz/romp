#pragma once
#include <memory>
#include <string>
#include <vector>

#include "BPatch.h"
#include "BPatch_addressSpace.h"
#include "BPatch_function.h"
#include "BPatch_point.h"
#include "BPatch_process.h"

#define MODULE_NAME_LENGTH 128

namespace romp {
  class InstrumentClient {
    public:
      InstrumentClient(
              const std::string& programName, 
              const std::string& rompLibPath,
              std::shared_ptr<BPatch> bpatchPtr,
              const std::string& arch,
              const std::string& modSuffix);
      void instrumentMemoryAccess();    
    private:
      std::unique_ptr<BPatch_addressSpace> initInstrumenter(
              const std::string& programName,
              const std::string& rompLibPath); 
      std::vector<BPatch_function*> getCheckAccessFuncs(
              const std::unique_ptr<BPatch_addressSpace>& addrSpacePtr);
      std::vector<BPatch_function*> getFunctionsVector(
              const std::unique_ptr<BPatch_addressSpace>& addrSpacePtr); 
      void instrumentMemoryAccessInternal(
              const std::unique_ptr<BPatch_addressSpace>& addrSpacePtr,
              std::vector<BPatch_function*>& funcVec);
      void insertSnippet(const std::unique_ptr<BPatch_addressSpace>& addrSpacePtr, 
                         const std::vector<BPatch_point*>* pointsVecPtr);
      bool hasHardwareLock(
              const Dyninst::InstructionAPI::Instruction& instruction,
              const std::string& arch);
      void finishInstrumentation(
              const std::unique_ptr<BPatch_addressSpace>& addrSpacePtr); 
    private:    
      std::unique_ptr<BPatch_addressSpace> addrSpacePtr_;
      std::shared_ptr<BPatch> bpatchPtr_;
      std::vector<BPatch_function*> checkAccessFuncs_;
      std::string programName_;
      std::string arch_;
      std::string modSuffix_;
  };
}
