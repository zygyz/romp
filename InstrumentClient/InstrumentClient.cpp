#include "InstrumentClient.h"

#include <glog/logging.h>

using namespace Dyninst;
using namespace romp;
using namespace std;

#define MATCH_LIB(buffer, target) \
      buffer.find(target) != string::npos

InstrumentClient::InstrumentClient(
        const string& programName, 
        const string& rompLibPath,
        shared_ptr<BPatch> bpatchPtr,
        const string& arch,
        const string& modSuffix) : m_bPatchPtr(move(bpatchPtr)), 
                                   m_programName(programName),
                                   m_architecture(arch),
                                   m_moduleSuffix(modSuffix) {
  m_addressSpacePtr = initInstrumenter(programName, rompLibPath);
  m_checkAccessFunctions = getCheckAccessFuncs(m_addressSpacePtr);
  if (m_checkAccessFunctions.size() == 0)  {
      LOG(FATAL) << "error empty m_checkAccessFunctions vector";
  }
  if (!m_checkAccessFunctions[0]) {
      LOG(FATAL) << "error empty first m_checkAccessFunctions element";
  }
  LOG(INFO) << "InstrumentClient initialized with arch: " << m_architecture;
}

unique_ptr<BPatch_addressSpace> 
InstrumentClient::initInstrumenter(
        const string& programName,
        const string& rompLibPath) {
  auto handle = m_bPatchPtr->openBinary(programName.c_str(), true);
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
    LOG(INFO) << "module name: " 
              << module->getFullName(nameBuffer, MODULE_NAME_LENGTH);
    if (module->isSharedLib()) { 
      //LOG(INFO) << "skip module: " << nameBuffer;
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
  auto functions = getFunctionsVector(m_addressSpacePtr);
  instrumentMemoryAccessInternal(m_addressSpacePtr, functions);
  finishInstrumentation(m_addressSpacePtr);
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
      LOG(WARNING) << "no load/store points for function " 
          << function->getName();    
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
InstrumentClient::hasHardwareLock(
        const InstructionAPI::Instruction& instruction,
        const std::string& arch) {
  if (arch == "x86") { 
      // check first byte of the instruction for x86 arch
    return reinterpret_cast<uint8_t>(instruction.rawByte(0)) == 0xf0;
  } 
  LOG(FATAL) << "unexpected architecture: " << arch;
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
    auto hardWareLock = hasHardwareLock(instruction, m_architecture);

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
    BPatch_funcCallExpr checkAccessCall(*(m_checkAccessFunctions[0]), funcArgs);
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
      m_bPatchPtr->waitForStatusChange();
    }
  } else if (appBin) {
    if (!appBin->writeFile((m_programName + m_moduleSuffix).c_str())) {
      LOG(FATAL) << "failed to write instrumented binary to file";
    }
  } 
}
