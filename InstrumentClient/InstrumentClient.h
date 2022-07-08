#pragma once
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

#include "BPatch.h"
#include "BPatch_addressSpace.h"
#include "BPatch_function.h"
#include "BPatch_point.h"
#include "BPatch_process.h"
#include "Symtab.h"

#define MODULE_NAME_LENGTH 128

namespace romp {
  class InstrumentClient {
    public:
      InstrumentClient(
              const std::string& sourceFileName,
              const std::string& programName, 
              const std::string& rompLibPath,
              std::shared_ptr<BPatch> bpatchPtr,
              const std::string& architecture,
              const std::string& moduleSuffix);
      void instrumentMemoryAccess();    
    private:
      std::unique_ptr<BPatch_addressSpace> initInstrumenter(const std::string& programName, const std::string& rompLibPath); 
      std::vector<BPatch_function*> getCheckAccessFuncs(const std::unique_ptr<BPatch_addressSpace>& addrSpacePtr);
      std::vector<BPatch_function*> getFunctionsVector(const std::unique_ptr<BPatch_addressSpace>& addrSpacePtr); 
      void instrumentMemoryAccessInternal(const std::unique_ptr<BPatch_addressSpace>& addrSpacePtr, std::vector<BPatch_function*>& funcVec);
      void insertSnippet(const std::unique_ptr<BPatch_addressSpace>& addrSpacePtr, const std::vector<BPatch_point*>* pointsVecPtr);
      bool hasHardwareLock(const Dyninst::InstructionAPI::Instruction& instruction, const std::string& arch);
      bool isCallInstruction(const Dyninst::InstructionAPI::Instruction& instruction);
      bool isThreadLocalStorageAccess(const Dyninst::InstructionAPI::Instruction& instruction); 
      void finishInstrumentation(const std::unique_ptr<BPatch_addressSpace>& addrSpacePtr); 
      void findAllOmpDirectiveLineNumbers();
      void findInstructionRanges();
      bool isInstructionForOmpDirective(const uint64_t instructionAddress);
    private:    
      std::unique_ptr<BPatch_addressSpace> mAddressSpacePtr;
      std::shared_ptr<BPatch> mBpatchPtr;
      std::vector<BPatch_function*> mCheckAccessFunctions;
      std::string mProgramName;
      std::string mSourceFileName;
      std::string mArchitecture;
      std::string mModuleSuffix;
      std::vector<int> mOmpDirectiveLineNumbers;
      std::unordered_map<int, std::vector<Dyninst::SymtabAPI::AddressRange> > mLineNumberInstructionRangeMap;
  };
}
