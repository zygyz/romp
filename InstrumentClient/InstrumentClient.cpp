#include "InstrumentClient.h"

#include "Register.h"

#include <glog/logging.h>
#include <iostream>

using namespace Dyninst;
using namespace romp;
using namespace std;

#define MATCH_LIB(buffer, target) \
      buffer.find(target) != string::npos

InstrumentClient::InstrumentClient(
        const string& sourceFileName,
        const string& programName, 
        const string& rompLibPath,
        shared_ptr<BPatch> bpatchPtr,
        const string& arch,
        const string& modSuffix) : mBpatchPtr(move(bpatchPtr)), 
                                   mSourceFileName(sourceFileName),
                                   mProgramName(programName),
                                   mArchitecture(arch),
                                   mModuleSuffix(modSuffix) {
  mAddressSpacePtr = initInstrumenter(programName, rompLibPath);
  mCheckAccessFunctions = getCheckAccessFuncs(mAddressSpacePtr);
  if (mCheckAccessFunctions.size() == 0)  {
      LOG(FATAL) << "error empty mCheckAccessFunctions vector";
  }
  if (!mCheckAccessFunctions[0]) {
      LOG(FATAL) << "error empty first mCheckAccessFunctions element";
  }
  LOG(INFO) << "InstrumentClient initialized with arch: " << mArchitecture;
}

unique_ptr<BPatch_addressSpace> 
InstrumentClient::initInstrumenter(
        const string& programName,
        const string& rompLibPath) {
  auto handle = mBpatchPtr->openBinary(programName.c_str(), true);
  if (!handle) {
    LOG(FATAL) << "cannot open binary: " << programName;    
  }
  unique_ptr<BPatch_addressSpace> ptr(handle);  
  // load romp library 
  if (!ptr->loadLibrary(rompLibPath.c_str())) {
    LOG(FATAL) << "cannot load romp library: " << rompLibPath;
  } else {
    LOG(INFO) << "loaded romp library at: " << rompLibPath;
  }
  return ptr;
}

/* 
 * Get the dyninst representation of the `checkAccess` function
 * defined in romp library code RompLib.cpp.
*/
vector<BPatch_function*>
InstrumentClient::getCheckAccessFuncs(
      const unique_ptr<BPatch_addressSpace>& addrSpacePtr) {
  if (!addrSpacePtr) {
    LOG(FATAL) << "null pointer";
   }
  auto appImage = addrSpacePtr->getImage();
  if (!appImage) {
    LOG(FATAL) << "cannot get image";
   }
   vector<BPatch_function*> checkAccessFuncs;
   appImage->findFunction("checkAccess", checkAccessFuncs);
  if (checkAccessFuncs.size() == 0) {
    LOG(FATAL) << "cannot find function `checkAccess` in romp lib";
  }
  return checkAccessFuncs;
}

/* 
 * Get dyninst representation of all all functions in the 
 * program being instrumented. Ideally, no function should 
 * be skipped.
 */ 
vector<BPatch_function*> 
InstrumentClient::getFunctionsVector(
        const unique_ptr<BPatch_addressSpace>& addrSpacePtr) {
  vector<BPatch_function*> funcVec;
  auto appImage = addrSpacePtr->getImage();
  if (!appImage) {
    LOG(FATAL) << "cannot get image";
  }
  auto appModules = appImage->getModules();
  if (!appModules) {
    LOG(FATAL) << "cannot get modules";
  }
  char nameBuffer[MODULE_NAME_LENGTH];
  for (const auto& module : *appModules) {
    LOG(INFO) << "module name: " << module->getFullName(nameBuffer, MODULE_NAME_LENGTH);
    if (module->isSharedLib()) { 
      continue;
    }
    auto procedures = module->getProcedures();
    for (const auto& procedure : *procedures) {
      funcVec.push_back(procedure);
    }
  }
  return funcVec;
}

/* 
 * Public interface for InstrumentClient, wraps the internal 
 * implementation of instrumentation of memory accesses
 */
void
InstrumentClient::instrumentMemoryAccess() {  
  findAllOmpDirectiveLineNumbers();  
  findInstructionRanges();
  auto functions = getFunctionsVector(mAddressSpacePtr);
  instrumentMemoryAccessInternal(mAddressSpacePtr, functions);
  finishInstrumentation(mAddressSpacePtr);
}

/*
 * Instrument memory accesses in each function by inserting the 
 * `checkAccess` function call 
 */ 
void
InstrumentClient::instrumentMemoryAccessInternal(
    const unique_ptr<BPatch_addressSpace>& addrSpacePtr,
    vector<BPatch_function*>& funcVec) {   
  BPatch_Set<BPatch_opCode> opcodes;
  opcodes.insert(BPatch_opLoad);
  opcodes.insert(BPatch_opStore);
  addrSpacePtr->beginInsertionSet();
  for (const auto& function : funcVec) {
    auto pointsVecPtr = function->findPoint(opcodes);
    if (!pointsVecPtr) {
      LOG(WARNING) << "no load/store points for function " << function->getName();    
      continue;
    } else if (pointsVecPtr->size() == 0) {
      LOG(WARNING) << "load/store points vector size is 0 for function " 
          << function->getName();
      continue;
    }
    insertSnippet(addrSpacePtr, pointsVecPtr);
  }
  if (!addrSpacePtr->finalizeInsertionSet(true)) {
    LOG(FATAL) << "error in batch insertion of snippets";
  }
}

/* 
 * Determine if the instruction contains a hardware lock for X86 architecture.
 * Could be modified for other architeture.
 */
bool
InstrumentClient::hasHardwareLock(const InstructionAPI::Instruction& instruction, const std::string& arch) {
  if (arch == "x86") { 
      // check first byte of the instruction for x86 arch
    return reinterpret_cast<uint8_t>(instruction.rawByte(0)) == 0xf0;
  } 
  LOG(FATAL) << "unexpected architecture: " << arch;
  return false;
}

bool InstrumentClient::isCallInstruction(const InstructionAPI::Instruction& instruction) {
  return instruction.getCategory() == Dyninst::InstructionAPI::c_CallInsn;
}

// return true if current executable is accessing thread local storage. 
bool InstrumentClient::isThreadLocalStorageAccess(const InstructionAPI::Instruction& instruction) {
  std::set<InstructionAPI::RegisterAST::Ptr> regsRead; 
  instruction.getReadSet(regsRead);   
  for (auto reg : regsRead) {
    if (reg->getID().name() == "x86_64::fs") {
      return true;
    }
  }
  return false; 
}
/*
 * Insert checkAccess code snippet to load/store point
 */
void
InstrumentClient::insertSnippet(
        const unique_ptr<BPatch_addressSpace>& addrSpacePtr,
        const vector<BPatch_point*>* pointsVecPtr) {
  if (!pointsVecPtr) {
    LOG(FATAL) << "null pointer";
  } 
  for (const auto& point : *pointsVecPtr) {
    auto memoryAccess = point->getMemoryAccess();
    if (!memoryAccess) {
      LOG(FATAL) << "null memory access";
    }
    
    if (memoryAccess->isAPrefetch_NP()) {
      LOG(INFO) << "current point is a prefetch, continue";
      continue;
    }  
    auto isWrite = true;
    // sometimes an access is both a read and a write 
    // for this case, treat it as a write
    if (memoryAccess->isAStore()) { // is a store
      isWrite = true; 
    } else if (memoryAccess->isALoad()) { // is a pure load
      isWrite = false;
    } else {
      LOG(WARNING) << "unknown memory access type in function: " 
                   << point->getCalledFunctionName();
      continue;
    }

    auto instructionAddress = point->getAddress();         
    auto instruction = point->getInsnAtPoint();
    if (isCallInstruction(instruction)) {
      continue;
    }
    auto isTLSAccess = isThreadLocalStorageAccess(instruction);
    auto hardWareLock = hasHardwareLock(instruction, mArchitecture);

    vector<BPatch_snippet*> funcArgs;
    // memory address 
    funcArgs.push_back(new BPatch_effectiveAddressExpr()); 
    // number of bytes accessed
    funcArgs.push_back(new BPatch_bytesAccessedExpr());    
    // address of instruction
    funcArgs.push_back(new BPatch_constExpr(instructionAddress)); 
    // instruction contains hardware lock or not
    funcArgs.push_back(new BPatch_constExpr(hardWareLock));
    // is write access or not
    funcArgs.push_back(new BPatch_constExpr(isWrite));
    // is TLS access or not
    funcArgs.push_back(new BPatch_constExpr(isTLSAccess));
    BPatch_funcCallExpr checkAccessCall(*(mCheckAccessFunctions[0]), funcArgs);

    if (!addrSpacePtr->insertSnippet(
                checkAccessCall, *point, BPatch_callBefore)) {
        LOG(FATAL) << "snippet insertion failed";
    }
  }
}

/* 
 * Some post instrumentation process. Slight modification 
 * from the example in dyninst manual
 */
void
InstrumentClient::finishInstrumentation(
        const unique_ptr<BPatch_addressSpace>& addrSpacePtr) {
  auto appProc = dynamic_cast<BPatch_process*>(addrSpacePtr.get());
  auto appBin = dynamic_cast<BPatch_binaryEdit*>(addrSpacePtr.get());
  if (appProc) {
    if (!appProc->continueExecution()) {
        LOG(WARNING) << "continue exeuction failed";
    }
    while (!appProc->isTerminated()) {
      mBpatchPtr->waitForStatusChange();
    }
  } else if (appBin) {
    if (!appBin->writeFile((mProgramName + mModuleSuffix).c_str())) {
      LOG(FATAL) << "failed to write instrumented binary to file";
    }
  } 
}

inline std::string execute(std::string command) {
  std::array<char, 128> buffer;
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
  if (!pipe) {
    throw std::runtime_error("popen() failed!");
  }
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }
  return result;
}

// use grep command to find line numbers of lines that contain openmp directive. 
void InstrumentClient::findAllOmpDirectiveLineNumbers() {
  std::string command = "grep -n '#pragma omp' " + mSourceFileName + " | grep -o '[0-9]\\+' "; 
  auto result = execute(command);
  std::string lineNumber;
  std::istringstream split(result);
  while (std::getline(split, lineNumber, '\n')) {
    mOmpDirectiveLineNumbers[std::stoi(lineNumber)] = std::vector<std::pair<Offset, Offset>>();
  }  
}

void InstrumentClient::findInstructionRanges() {
  SymtabAPI::Symtab *obj = nullptr;
  auto error = SymtabAPI::Symtab::openFile(obj, mProgramName);
  for (auto& item : mOmpDirectiveLineNumbers) {
    LOG(INFO) << "line num: " << item.first;

  }
}
