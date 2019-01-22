#include <dlfcn.h>
#include <dwarf.h>
#include <iostream>
#include <fcntl.h>
#include <queue>
#include <stack>
#include <stdio.h>
#include <string.h>
#include <elfutils/libdw.h>
#include <elfutils/known-dwarf.h>
#include "BPatch.h"
#include "BPatch_object.h"
#include "BPatch_addressSpace.h"
#include "BPatch_process.h"
#include "BPatch_binaryEdit.h"
#include "BPatch_point.h"
#include "BPatch_function.h"
#include "BPatch_flowGraph.h"
#include "BPatch_statement.h"
#include "Instruction.h"
#include "InstructionDecoder.h"
#include "slicing.h"
#include "Visitor.h"
using namespace std;
using namespace Dyninst;
using namespace SymtabAPI;
using namespace InstructionAPI;
using namespace ParseAPI;
using namespace DataflowAPI;
#define MAX_FILENAME_LEN 128
#define SKIP_LIB_SRC "/data-race/pkgs-src/"
#define SKIP_SRC "src/"
#define SKIP_OMPTRACE_LIB "libomptrace.so"
#define SKIP_DYNINST_LIB "libdyn"
#define SKIP_OMP_LIB "libomp.so"
#define SKIP_GCC_LIB "libgcc"
#define SKIP_LIBC "libc"    // should not skip
#define SKIP_LIBDL "libdl"  
#define SKIP_LIBSTDCPP "libstdc++"
#define SKIP_LIBPTHREAD "libpthread"
#define SKIP_LIBBOOST "libboost"
#define SKIP_LIBPARSE "libparse"
#define SKIP_LIBSTACK "libstack"
#define SKIP_LIBPATCH "libpatch"
#define SKIP_LIBDWARF "libdwarf"
#define SKIP_TCMALLOC "libtcmalloc"
#define SKIP_LIBELF "libelf"
#define SKIP_LD_LINUX "ld-linux"
#define SKIP_LIBM "libm"

#define NUMBER_SKIPPED 19

#define DEBUG
#define LINEMAP_SECTION_NAME ".linemap"
#define STRING_TABLE_NAME ".stringtable"
BPatch bpatch;

static uint64_t rodata_offset = 0;
static uint64_t rodata_upper = 0;

 // Different ways to perform instrumentation
typedef enum {
    CREATE,
    ATTACH,
    OPEN
} accessType_t;

static char g_skip_modules[NUMBER_SKIPPED][MAX_FILENAME_LEN] =
{
    SKIP_LIB_SRC,
    SKIP_SRC,
    SKIP_OMPTRACE_LIB,
    SKIP_DYNINST_LIB,
    SKIP_OMP_LIB,
    SKIP_GCC_LIB,
    SKIP_LIBC,
    SKIP_LIBDL, 
    SKIP_LIBSTDCPP,
    SKIP_LIBPTHREAD,
    SKIP_LIBBOOST,
    SKIP_LIBPARSE,
    SKIP_LIBSTACK,
    SKIP_LIBPATCH,
    SKIP_LIBDWARF,
    SKIP_TCMALLOC,
    SKIP_LIBELF,
    SKIP_LD_LINUX,
    SKIP_LIBM 
 };


#define FORBID_CNT 11
static char g_forbid_funcs[FORBID_CNT][MAX_FILENAME_LEN] = 
{  
    "_init",
    "_start",
    "__do_global_dtors_aux",
    "frame_dummy",
    "__clang_call_terminate",
    "__libc_csu_init",
    "__libc_csu_fini",
    "_fini",
    "__cxx_global_var_init",
    "chrono",
    "polybench"
    /*
    "frame_dummy",
    "__do_global_dtors_aux",
    "_start",
    "__libc_csu_fini",
    "_init",
    "_fini",
    "__libc_csu_init",
    "__clang_call_terminate",
    "__do_global_ctors_aux",
    "deregister_tm_clones",
    */
};

class PrintVisitor : public Visitor {
   public:
       PrintVisitor(uint64_t rv) { rip_value = rv; };
       ~PrintVisitor() {}; 
       virtual void visit(BinaryFunction* b) {
          cout << " pv: a binary function ";
          if (b->isAdd()) {
              cout << " is + "; 
          } else if (b->isMultiply()) {
              cout << " is mul ";
          } else if (b->isLeftShift()) {
              cout << " is left shift ";
          } else if (b->isRightArithmeticShift()) {
              cout << " is right arithmetic shift ";
          } else if (b->isAndResult()) {
              cout << " is and result ";
          } else if (b->isOrResult()) {
              cout << " is or result ";
          } else if (b->isRightLogicalShift()) {
              cout << " is right logical shift ";
          } else if (b->isRightRotate()) {
              cout << " is right locate ";
          }
       } 
       virtual void visit(Immediate* i) {
           cout << " pv: immediate: " << i->format(defaultStyle);
       }
       
       virtual void visit(RegisterAST* r) {
           cout << " pv: register : " << r->format(defaultStyle);
            
        }

        virtual void visit(Dereference* d) {
            cout << " pv: dereference  ";
        }
   private:
        uint64_t rip_value;
};

class IPVisitor : public Visitor {
    public:
        IPVisitor() { 
            hasImm = false;
            isAdd = false; 
            isRip = false;
        };

        ~IPVisitor() {};

        virtual void visit(BinaryFunction* b) {
            isAdd = b->isAdd();
        };

        virtual void visit(Immediate* i) {
            hasImm = true; 
            imm = i->format(defaultStyle);
        };

        virtual void visit(RegisterAST* r) {
            if (r->format(defaultStyle) == "RIP") {
                isRip = true;
            }
        }

        virtual void visit(Dereference* d) {};

        string getImm() { return imm; }   

        bool isRipMemAccess() { return hasImm && isAdd && isRip; }    

    private:
        string imm;
        bool hasImm;
        bool isAdd;
        bool isRip;
};

class open_statement {
public:
  open_statement() { reset(); };
  Dwarf_Addr noAddress() { return (Dwarf_Addr) ~0; }
  bool uninitialized() {
    return start_addr == noAddress();
  };
  void reset() {
    string_table_index = -1;
    start_addr = noAddress();
    end_addr = noAddress();
    line_number = 0;
    column_number = 0;
  };
  bool sameFileLineColumn(const open_statement &rhs) {
    return ((string_table_index == rhs.string_table_index) &&
	    (line_number == rhs.line_number) &&
            (column_number == rhs.column_number));
  };
  void operator=(const open_statement &rhs) {
    string_table_index = rhs.string_table_index;
    start_addr = rhs.start_addr;
    end_addr = rhs.end_addr;
    line_number = rhs.line_number;
    column_number = rhs.column_number;
  };
public:
  Dwarf_Word string_table_index;
  Dwarf_Addr start_addr;
  Dwarf_Addr end_addr;
  int line_number;
  int column_number;
};


vector<string> stringTable;


typedef struct MyStatement {
    unsigned int fileIndex;
    unsigned int lineNo;
    unsigned int lineOffset;
    void* lowInclusiveAddr;
    void* highExclusiveAddr;
    MyStatement(int fi, int ln, int lo, void* la, void* ha) {
        fileIndex = (unsigned int)fi;
        lineNo = (unsigned int)ln;
        lineOffset = (unsigned int)lo;
        lowInclusiveAddr = la;
        highExclusiveAddr = ha;
    }
    void print() {
        cout << "file: " << stringTable[fileIndex] << " lineNo: " << lineNo << " colNo: " << lineOffset 
            << " low addr: " << lowInclusiveAddr << " high addr: " << highExclusiveAddr << endl;
    }
} MyStatement;

auto compare = [] (const MyStatement lhs, const MyStatement rhs) {
    return lhs.lowInclusiveAddr > rhs.lowInclusiveAddr;
};
        
priority_queue<MyStatement, vector<MyStatement>, decltype(compare)> lineInformation(compare);


// Attach, create, or open a file for rewriting
BPatch_addressSpace* 
StartInstrumenting(accessType_t accessType,
                   const char* name,
                   int pid,
                   const char* argv[]) 
{
    BPatch_addressSpace* handle = NULL;
    switch(accessType) {
        case CREATE:
            handle = bpatch.processCreate(name, argv);
            if (!handle) { 
                fprintf(stderr, "processCreate failed\n"); 
            }
            break;
        case ATTACH:
            handle = bpatch.processAttach(name, pid);
            if (!handle) { 
                fprintf(stderr, "processAttach failed\n"); 
            }
            break;
        case OPEN:
        // Open the binary file and all dependencies
            handle = bpatch.openBinary(name, true);
            if (!handle) { 
                fprintf(stderr, "openBinary failed\n"); 
            }
            break;
    }
    return handle;
 }

void
MakeFunctionVector(BPatch_addressSpace* app, vector<BPatch_function*>& func_vec, const char* romp_path)
{
    if (app == nullptr)  {
        cerr << "BPatch_addressSpace* app is null " << endl;
        exit(1);
    }
    BPatch_image* app_image = app->getImage(); 
    if (app_image == nullptr) {
        cerr << "Cannot get image " << endl;
        exit(1);
    }
    vector<BPatch_module*>* modules = app_image->getModules(); 
    if (modules == nullptr) {
        cerr << "Cannot get modules" << endl;    
        exit(1);
    }
    int module_count = modules->size();    
#ifdef DEBUG
    fprintf(stdout, "Number of modules: %d\n", module_count);
#endif
    for (int i = 0; i < module_count; ++i) {
        bool skipped = false;
        char module_name[MAX_FILENAME_LEN]; 
        modules->at(i)->getFullName(module_name, MAX_FILENAME_LEN); 
#ifdef DEBUG
        fprintf(stdout, "module %d name: %s\n", i, module_name);
#endif
        /* I want to skip some modules */
        for (int j = 0; j < NUMBER_SKIPPED; ++j) {
            if (strstr(module_name, g_skip_modules[j]) != NULL) {
            //if (!strncmp(module_name, g_skip_modules[j], strlen(g_skip_modules[j]))) {
                skipped = true; 
                break;  
            }
        }
        if (strstr(module_name, romp_path) != NULL) {
            skipped = true;
        }
        if (!skipped) {
#ifdef DEBUG
            cout << "not skipped: " << module_name << endl;
#endif
            vector<BPatch_function*>* procedures = modules->at(i)->getProcedures();// also includes uninstrumentable functions
            for (unsigned int j = 0; j < procedures->size(); ++j) {
                printf("Procedure: %s\n", procedures->at(j)->getName().c_str());
                bool func_forbidden = false;
                for (int k = 0; k < FORBID_CNT; ++k) {
                    if (!strncmp(procedures->at(j)->getName().c_str(), g_forbid_funcs[k], strlen(g_forbid_funcs[k]))) {
                        func_forbidden = true; 
                        printf("Forbidden: %s\n", procedures->at(j)->getName().c_str());
                        break; 
                   }
                }
                if (func_forbidden)
                    continue;
                func_vec.push_back(procedures->at(j));      
            }
        }
    }  
}

void
PrintBytes(const void* object, size_t size)
{
    unsigned char * bytes = (unsigned char*)object;
    size_t i;
    printf("[ ");
    for (i = 0; i < size; ++i) {
        printf("%02x ", (unsigned)bytes[i]);
    }
    printf("]\n");
}

void
PrintPointInfo(BPatch_point* point)
{
    auto insn = point->getInsnAtPoint();  
    size_t inst_size = insn.size();
    const void* inst_raw = insn.ptr();
    if (insn.readsMemory()) {
        fprintf(stdout, "inst readsMemory: ");
        PrintBytes(inst_raw, inst_size);
    } 
}

#define DEBUG_DYNINST
#undef DEBUG_DYNINST
#ifdef DEBUG_DYNINST
const void* GetInstBaseAndSize(BPatch_point* point, size_t& size)
{
    auto inst_ptr = point->getInsnAtPoint();  
    if (inst_ptr == nullptr) {
        fprintf(stderr, "getInsnAtPoint() not implemented\n");
    } else {
        size = inst_ptr->size();
        return inst_ptr->ptr();
    }
    return NULL;
}
#endif

class ConstantPred : public Slicer::Predicates {
    public:
       virtual bool endAtPoint(Assignment::Ptr ap) {
           return ap->insn().writesMemory();
       } 

       virtual bool addPredecessor(AbsRegion reg) {
            if (reg.absloc().type() == Absloc::Register) {
                MachRegister r = reg.absloc().reg();
                return !r.isPC();
            }
            return true;
       }
};

void
AnalyzeMemAccess(void* instnaddr, vector<Assignment::Ptr>& assignments, vector<uint64_t>& memAddrs) 
{
   Assignment::Ptr memAssign;
   cout << "\n\n" << hex << instnaddr << dec << " num assignments: " << assignments.size() << endl;
   int assn = 0;
   for (auto ait = assignments.begin(); ait != assignments.end(); ++ait) {
      auto inputs = (*ait)->inputs();
      cout << assn++ << " num input " << inputs.size() << endl;
      for (auto input : inputs) {
          switch(input.absloc().type()) {
            case Absloc::Register:
                break;
            case Absloc::Stack:
                break;
            case Absloc::Heap:
                cout << "input is heap, addr: " << hex << input.absloc().addr() << dec << endl;
                memAddrs.push_back((uint64_t)input.absloc().addr());
                break;
            case Absloc::Unknown:
                break;
          }
      }
   }
}

bool
CreateAndInsertSnippet(BPatch_addressSpace* app,
                       vector<BPatch_point*>* points,
                       vector<BPatch_function*>& check_access_funcs, 
                       BPatch_function* cur_func)      
{
    if (points->size() == 0) {
        cerr << "no point to instrument " << endl;     
        return false;
    }
    int num_points = points->size();  
    //Here we iterate over the points 
    for (int i = 0; i < num_points; ++i) {
        const BPatch_memoryAccess* memory_access = points->at(i)->getMemoryAccess();  
        if (memory_access == NULL) {
            cerr << "memory_access is null " << endl;
            return false;
        }    

        auto instnAddr = points->at(i)->getAddress();
        auto insn = points->at(i)->getInsnAtPoint();
       // auto funcAtInstr = points->at(i)->getCalledFunction();
        auto parseAPIfunc = ParseAPI::convert(cur_func);
        BPatch_flowGraph* fg = cur_func->getCFG();             
        BPatch_basicBlock* bb = fg->findBlockByAddr((unsigned long)instnAddr);   
        auto parseAPIblock = ParseAPI::convert(bb);
        if (bb == NULL) {
            cerr << "cannot find basic block associated with the instn addr " << hex << instnAddr << dec << endl;
            return false;
        }
       
        int size_mach_instn = 0;
        string instn_raw_str = "";
        bool has_lock_prefix = false;  
        bool only_reads_rodata = false;
        if (insn.readsMemory() && !(insn.writesMemory())) { 
            // if the instruction reads memory and does not write memory
            AssignmentConverter ac(true, false);
            vector<Assignment::Ptr> assignments;
           //cout << "convert assignment " << endl;
            ac.convert(insn, (unsigned long)instnAddr, parseAPIfunc, parseAPIblock, assignments);
            vector<uint64_t> heapMemAddrs;
            AnalyzeMemAccess(instnAddr, assignments, heapMemAddrs);            
            if (heapMemAddrs.size() > 0) {
                bool out_of_range = false; 
                for (auto addr : heapMemAddrs) {
                  if (addr > rodata_upper || addr < rodata_offset) {
                      out_of_range = true;
                      break;
                  }
               }   
               if (!out_of_range) 
                   only_reads_rodata = true;
            } 
            unsigned char first_byte = insn.rawByte(0);
            if (first_byte == 0xf0) {
                has_lock_prefix = true;
            }
        } 

        if (only_reads_rodata) {
            cout << hex << "\ninstruction@" << instnAddr << dec << " only reads rodata " << endl;
            continue; 
        } else {
            cout << "reads data outside of rodata " << endl;
        }

        if (memory_access->isAPrefetch_NP()) { // This point is a prefetch
            continue;
        } 
        int flag = 0;

        if (memory_access->isALoad()) { // This point is a read.
            flag |= 1;
        } 

        if (memory_access->isAStore()) { // This point is a write.
            flag |= 2;
        } 

        if (flag == 0) {
            cerr << "Unknown memory access type " << points->at(i)->getCalledFunctionName() << endl;
            continue;
        }

        if (flag == 3) { // regulate the load/store to store
            flag = 2;
        }
        vector<BPatch_snippet*> args;      
        BPatch_snippet* address = new BPatch_effectiveAddressExpr();     
        args.push_back(address);
        BPatch_snippet* bytes = new BPatch_bytesAccessedExpr();
        args.push_back(bytes);
        BPatch_snippet* type =  new BPatch_constExpr(flag);
        args.push_back(type);

        vector<BPatch_register> regs;  
        app->getRegisters(regs);
        BPatch_snippet* instn_addr = new BPatch_constExpr(instnAddr);
        args.push_back(instn_addr);
        BPatch_snippet* has_hw_lock = new BPatch_constExpr(has_lock_prefix); 
         
        BPatch_funcCallExpr check_access_call(*(check_access_funcs[0]), args);  
          
        if (!app->insertSnippet(check_access_call, *(points->at(i)), BPatch_callBefore)) {
            cerr << "snippet insertion failed " << endl;
            return false;
        } 
    }
    return true;
}

int 
GetMemInstCount(BPatch_function* func)
{
    if (func == nullptr) {
        return 0;
    }
    int insns_access_memory = 0;
    BPatch_flowGraph* fg = func->getCFG();             
    set<BPatch_basicBlock*> blocks;
    fg->getAllBasicBlocks(blocks);
    for (auto block_iter = blocks.begin(); 
              block_iter != blocks.end();
              ++block_iter) {
        BPatch_basicBlock* block = *block_iter;
        vector<InstructionAPI::Instruction> insns;
        block->getInstructions(insns);
        for (auto insn_iter = insns.begin();
                  insn_iter != insns.end();
                  ++insn_iter) {
            InstructionAPI::Instruction insn = *insn_iter;
            if (insn.readsMemory() || insn.writesMemory()) {
                insns_access_memory++;
            }
        }
    }
    return insns_access_memory;
}
/* Description:
 *    Insert snippet to the load/store points of selected procedures to do the data race checking. 
 */
bool
InstrumentMemoryAccesses(BPatch_addressSpace* app, const char* romp_path) 
{
    // First get a list of procedures in the address space.
    if (app == nullptr) {
        cerr << "address space nullptr " << endl;    
        return false;
    }
    vector<BPatch_function*> functions;
    MakeFunctionVector(app, functions, romp_path);
    if (functions.size() == 0) {
        cerr << "no functions for instrumentation " << endl;
        exit(1);     
    }
    if (!app->loadLibrary(romp_path)) {
        cerr << "Could not load library : " << romp_path << endl; 
        return false;
    }
    BPatch_image* app_image = app->getImage();
    vector<BPatch_function*> check_access_funcs;
    app_image->findFunction("CheckAccess", check_access_funcs);
    if (check_access_funcs.size() == 0) {
        cerr << "could not find CheckAccess " << endl; 
        return false;
    }
    /*This is one way to instrument memory access. Store and load together.
     * Another way is as Bill suggested, do this separatedly. Note that some instructions have both store/load op
    */ 
    BPatch_Set<BPatch_opCode> axs;
    axs.insert(BPatch_opLoad);
    axs.insert(BPatch_opStore);
    app->beginInsertionSet(); // Do batch insertion.
    for (auto func : functions) { // For each procedures that we are interested in.
        vector<BPatch_point*>* points = func->findPoint(axs); 
        char module_name[MAX_FILENAME_LEN]; 
        func->getModuleName(module_name, MAX_FILENAME_LEN);
        if (!points) {
            cerr <<  "No load/store points found for function " << func->getName() << " " << module_name << endl;
            continue;
        } else if (points->size() == 0) {
            cerr << "Load/Store points vector size is 0 for function " << func->getName() << " " << module_name << endl;
            continue;
        }
#ifdef DEBUG
        cout << "Func name: " << func->getName() << endl;    
#endif
        if (!CreateAndInsertSnippet(app, points, check_access_funcs, func)) {
            cerr << "Error in CreateAndInsertSnippet() " << endl;
            return false;
        } 
    } 
    if (!app->finalizeInsertionSet(true)) {
        cerr <<  "Error in batch inserting the snippets "  << endl;
        return false;
    }
    return true;
}

void 
FinishInstrumenting(BPatch_addressSpace* app, const char* newName)
{
    BPatch_process* appProc = dynamic_cast<BPatch_process*>(app);
    BPatch_binaryEdit* appBin = dynamic_cast<BPatch_binaryEdit*>(app);
    if (appProc) {
        if (!appProc->continueExecution()) {
            fprintf(stderr, "continueExecution failed\n");
        }
        while (!appProc->isTerminated()) {
            bpatch.waitForStatusChange();
        }
    } else if (appBin) {
        if (!appBin->writeFile(newName)) {
            fprintf(stderr, "writeFile failed\n");
        }
    }
}




/* schema: 
 *  
 *  |uint32_t: number of total records|uint64_t: 1st record low address inclusive | uint16_t: 1st filename index | uint16_t: 1st record line no | uint16_t: 1st record column no |uint64_t: 2nd record low address inclusive |....|
 */
void* serializeLineInformation(size_t& chunk_size) 
{
    uint32_t num_records = (uint32_t) lineInformation.size(); 
    size_t payload_size = sizeof(uint32_t) + (sizeof(uint64_t) + sizeof(uint16_t) * 3) * num_records;
    void* chunk = calloc(1, payload_size);
  
    if (chunk == NULL) {
        cerr << "calloc for line map information failed " << endl;
        exit(-1);
    }    
    
    chunk_size = payload_size;
    cout << "line information chunk size: " << payload_size << endl;

    memcpy(chunk, (char*)&num_records, sizeof(uint32_t)); // set the total number of line records 

    int offset = sizeof(uint32_t);
    while (!lineInformation.empty()) {
        auto stmt = lineInformation.top();
        lineInformation.pop();
        uint64_t lowInclusiveAddr = (uint64_t)stmt.lowInclusiveAddr;   
        uint16_t fileIndex = (uint16_t)stmt.fileIndex;
        uint16_t lineNo = (uint16_t) stmt.lineNo;
        uint16_t lineOffset = (uint16_t) stmt.lineOffset;
        memcpy((char*)chunk + offset, (char*)&lowInclusiveAddr, sizeof(uint64_t)); // pack the low address
        offset += sizeof(uint64_t); 
        memcpy((char*)chunk + offset, (char*)&fileIndex, sizeof(uint16_t));//pack the file index              
        offset += sizeof(uint16_t);
        memcpy((char*)chunk + offset, (char*)&lineNo, sizeof(uint16_t));//pack the line number 
        offset += sizeof(uint16_t);
        memcpy((char*)chunk + offset, (char*)&lineOffset, sizeof(uint16_t));//pack the line offset        
        offset += sizeof(uint16_t);
    }
    return chunk;
}


/*  schema
 *  |  uint16_t: num files | uint16_t: 1st filename offset | uint16_t: 1st fielname length |  uint16_t: 2nd filename offset | ... | uint16_t: last filename offset |
 *  uint16_t: last filename legnth | filename 1|filename 2|...| last filename |
 */
void* serializeStringTable(size_t& chunk_size)  // build the string table binary from the stringTable vector 
{
   uint16_t num_files = (uint16_t)stringTable.size();    
   size_t payload_size = 0;     
   vector<uint16_t> files_sizes;
   for (auto file : stringTable) {
       size_t file_size = file.size();
       payload_size += file_size;
       files_sizes.push_back((uint16_t)file_size);    
   }       
    
   payload_size += num_files; // include the '\0' terminator
   size_t header_size = 2 * (1 + num_files * 2); 
   size_t total_byte_size = payload_size + header_size;

   cout << "total byte size for string table: " << total_byte_size << endl;        
   void* chunk = calloc(1, total_byte_size);
   chunk_size = total_byte_size;

   if (chunk == NULL) {
        cerr << "calloc for string table failed " << endl;
        exit(-1);
   }

   memcpy(chunk, (char*)&num_files, sizeof(uint16_t)); // put the number of files into the header
   uint16_t offset = (uint16_t)header_size;
   for (int i = 0; i < num_files; ++i) { //put the offset into the header
      uint16_t current_file_size = (uint16_t)files_sizes[i];  
      memcpy((char*)chunk + 2 + i * 4, (char*)&offset, 2);
      memcpy((char*)chunk + 2 + i * 4 + 2, (char*)&current_file_size, 2); 
      cout << "current file size: " << current_file_size << " setting offset to " << offset << endl;
      offset += current_file_size + 1;
   }  
   //put the strings into the payload  
   offset = 0;
   for (auto file : stringTable) {
      auto file_str = file.c_str(); 
      size_t current_file_size = strlen(file_str);
      memcpy((char*)chunk + header_size + offset, file_str, current_file_size + 1);
      printf("file str: %s\n", file_str);
      offset += current_file_size + 1; 
   }
   return chunk;
}

void testLookupStringTable(void* chunk, int k, char* buf) 
{
    uint16_t num_files = 0;
    memcpy(&num_files, chunk, 2);     
    if (k >= num_files) {
        cerr << "error query " << k << " out of range" << endl;
        return;
    } 
    uint16_t offset = 0; 
    uint16_t filename_length = 0;
    size_t header_offset = (1 + k * 2) * 2;
    memcpy(&offset, (char*)chunk + header_offset, 2);
    memcpy(&filename_length, (char*)chunk + header_offset + 2, 2);
    memcpy(buf, (char*)chunk + offset, filename_length + 1);
}

void ParseDwarfLineMap(const char* filename) 
{
    cout << "Insert DWARF line map" << endl;   
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        printf("FAILURE: unable to open file: %s\n", filename);
        exit(-1);
    }
    Dwarf* dbg = dwarf_begin(fd, DWARF_C_READ);

    if (dbg) {
        cout << "dwarf_begin return success for file " << filename << endl;
        size_t cu_header_size;
        // lambda function to convert relative to absolute path
        stringTable.push_back("<Unknown file>");
        for (Dwarf_Off cu_off = 0, next_cu_off; 
             dwarf_nextcu(dbg, cu_off, &next_cu_off, &cu_header_size, NULL, NULL, NULL) == 0;
             cu_off = next_cu_off) {
            cout << "compilation unit header size: " << cu_header_size << endl;

            Dwarf_Off cu_die_off = cu_off + cu_header_size;
            Dwarf_Die cu_die;
            dwarf_offdie(dbg, cu_die_off, &cu_die);

            Dwarf_Lines * lineBuffer;
            size_t lineCount;
            int status = dwarf_getsrclines(&cu_die, &lineBuffer, &lineCount);

            if (status != 0) {
                cout << "no line in the line table, file: " << filename << endl;
                continue;
            }
        
            Dwarf_Files * files; 
            size_t offset = stringTable.size();  // the current size of string table is the offset 
            size_t filecount;
            status = dwarf_getsrcfiles(&cu_die, &files, &filecount);
            if (status != 0) {
                cout << "dwarf_getsrcfiles failed " << endl;
                continue;
            }

            Dwarf_Attribute attr;
            const char * comp_dir = dwarf_formstring( dwarf_attr(&cu_die, DW_AT_comp_dir, &attr) );
            string comp_dir_str( comp_dir ? comp_dir : "" );


            auto convert_to_absolute = [&comp_dir_str](const char * &filename) -> string
            {
                if(!filename) return "";
                string s_name(filename);
                // change to absolute if it's relative
                if (filename[0]!='/') {
                    s_name = comp_dir_str + "/" + s_name;
                }
                return s_name;
            };
        
            for(size_t i = 1; i < filecount; i++) {
                auto filename = dwarf_filesrc(files, i, nullptr, nullptr);
                if(!filename) continue;
                auto result = convert_to_absolute(filename);
                filename = result.c_str();
                stringTable.push_back(filename);
            }

            Offset baseAddr = 0;
            //
            Dwarf_Addr cu_high_pc = 0;
            dwarf_highpc(&cu_die, &cu_high_pc);

    /* Iterate over this CU's source lines. */
            open_statement current_line; 
            open_statement current_statement;
            for(size_t i = 0; i < lineCount; i++ ) {
                auto line = dwarf_onesrcline(lineBuffer, i); 
        /* Acquire the line number, address, source, and end of sequence flag. */
                status = dwarf_lineno(line, &current_statement.line_number);
                if ( status != 0 ) {
                    cerr << "dwarf_lineno failed" << endl;
                    continue;
                }
                status = dwarf_linecol(line, &current_statement.column_number);
                if ( status != 0 ) { current_statement.column_number = 0; }
                status = dwarf_lineaddr(line, &current_statement.start_addr);
                if ( status != 0 )
                {
                    cerr << "dwarf_lineaddr failed" << endl;
                    continue;
                }

                current_statement.start_addr += baseAddr;

                const char * file_name = dwarf_linesrc(line, NULL, NULL);
                if ( !file_name ) {
                    cerr << "dwarf_linesrc - empty name" << endl;
                    continue;
                }

        // search filename index
                string file_name_str(convert_to_absolute(file_name));
                int index = -1;
                for(size_t idx = offset; idx < stringTable.size(); ++idx) {
                    if(stringTable[idx] == file_name_str) {  
                        index = idx; 
                        break;
                    }
                }
                if( index == -1 ) {
                    cerr << "dwarf_linesrc didn't find index" << endl;
                    continue;
                }
                current_statement.string_table_index = index;
                //cout << "line num: " << current_statement.line_number << " col num: " << current_statement.column_number << endl;
                bool isEndOfSequence;
                status = dwarf_lineendsequence(line, &isEndOfSequence);
                if ( status != 0 ) {
                    cerr << "dwarf_lineendsequence failed" << endl;
                    continue;
                }
                if(i == lineCount - 1) {
                    isEndOfSequence = true;
                }
                bool isMyStatement;
                status = dwarf_linebeginstatement(line, &isMyStatement);
                if(status != 0) {
                    cerr << "dwarf_linebeginstatement failed" << endl;
                    continue;
                }
	            if (current_line.uninitialized()) {
	                current_line = current_statement;
	            } else {
	                current_line.end_addr = current_statement.start_addr;
	                if (!current_line.sameFileLineColumn(current_statement) ||
		                isEndOfSequence) {
                        MyStatement stmt(current_line.string_table_index, 
                                       current_line.line_number,
                                       current_line.column_number,
                                       (void*)current_line.start_addr, (void*)current_line.end_addr);
                        lineInformation.push(stmt);
		            current_line = current_statement;
	            }
	          }
	        if (isEndOfSequence) {
	            current_line.reset();
	        }
          } /* end iteration over source line entries. */
        }
    } else {
        printf("FAILURE: dwarf_begin failed\n");
    }
}

void
ShowAppThreadsInfo(accessType_t access_type,
                   BPatch_addressSpace* app)
{
    if (access_type != CREATE) 
        return;
    vector<BPatch_thread*> threads;
    BPatch_process* process = static_cast<BPatch_process*>(app);
    process->getThreads(threads);    
    for (auto thread : threads) {
        auto tid = thread->getTid();
        vector<BPatch_frame> stack;             
        thread->getCallStack(stack);
        fprintf(stdout, "thread id: %d\n", (int)tid);           
        for (auto frame : stack) {
            auto type = frame.getFrameType();
            switch(type) {
                case BPatch_frameNormal:
                   fprintf(stdout, "frame ptr: %p type: normal\n", frame.getFP()); 
                   break;
                case BPatch_frameSignal:
                   fprintf(stdout, "frame ptr: %p type: signal\n", frame.getFP()); 
                   break;
                case BPatch_frameTrampoline:
                   fprintf(stdout, "frame ptr: %p type: instrument \n", frame.getFP()); 
                   break;
            }
        }  
    }
}

void LookupReadOnlyRegion(
        const char* arg,
        BPatch_addressSpace* app)
{

    BPatch_image* app_image = app->getImage();
    if (app_image == NULL) {
        cerr << "getImage failed " << endl;
        exit(-1);     
    }    
    vector<BPatch_object*> objs;
    app_image->getObjects(objs);  
    cout << "objs num: " << objs.size() << endl;
    BPatch_object * main_program_object = nullptr;
    for (auto obj : objs) {
        if (strcmp(arg, obj->pathName().c_str()) == 0) {
            cout << "found main program object: " << obj->pathName() << endl;
            main_program_object = obj; 
            break;
        }
    }
    if (main_program_object == nullptr) {
        cerr << "cannot find the object for main program " << endl;     
        exit(-1);
    }
    SymtabAPI::Symtab* symtab = SymtabAPI::convert(main_program_object);
    Region* reg = nullptr;
    if (symtab->findRegion(reg, ".rodata")) {
        cout << "found .rodata!" << endl;
    }
    Offset offset = reg->getMemOffset();
    unsigned long size = reg->getMemSize();
    rodata_offset = (uint64_t)offset;
    rodata_upper = (uint64_t)(offset + size);
    cout << ".rodata offset: " << hex << "0x" << rodata_offset << ".rodata upper bound: 0x" << rodata_upper << dec << endl;
} 

void InsertParsedDebugSection(
        const char* arg, 
        void* string_table_chunk, 
        size_t string_table_chunk_size, 
        void* line_map_chunk, 
        size_t line_map_chunk_size, 
        BPatch_addressSpace* app) 
{
    BPatch_image* app_image = app->getImage();
    if (app_image == NULL) {
        cerr << "getImage failed " << endl;
        exit(-1);     
    }    
    vector<BPatch_object*> objs;
    app_image->getObjects(objs);  
    cout << "objs num: " << objs.size() << endl;
    BPatch_object * obj_to_insert = nullptr;
    for (auto obj : objs) {
        if (strcmp(arg, obj->pathName().c_str()) == 0) {
            cout << "found main program object: " << obj->pathName() << endl;
            obj_to_insert = obj; 
            break;
        }
    }
    if (obj_to_insert == nullptr) {
        cerr << "cannot find the object for main program " << endl;     
        exit(-1);
    }
    SymtabAPI::Symtab* symtab = SymtabAPI::convert(obj_to_insert);
    cout << "string table size: " << string_table_chunk_size << " line map size: " << line_map_chunk_size << endl;
    symtab->addRegion(0, string_table_chunk, string_table_chunk_size, STRING_TABLE_NAME, Region::RT_DATA, true);      
    symtab->addRegion(0, line_map_chunk, line_map_chunk_size, LINEMAP_SECTION_NAME, Region::RT_DATA, true);
}

int 
main(int argc, const char* argv[]) 
{
 // Set up information about the program to be instrumented
    if (argc < 2) {
        fprintf(stderr, "Error: insufficient argument\n Usage: ./omp_race_client your_program\n");
        return 1;
    }
    const char* prog_name = argv[1];
    const char* romp_path = getenv("ROMP_PATH");
    if (romp_path == NULL) {
        fprintf(stderr, "Error: environment variable ROMP_PATH not set \n export ROMP_PATH=/path/to/libomptrace.so before running me\n");
        return 1;
    } else {
        printf("romp_path: %s\n", romp_path);
    }
    int prog_pid = 42;  // HG2G: The meaning of life.
    const char* prog_argv[] = {argv[1], argv[2], argv[3], argv[4]}; 
    accessType_t mode = OPEN;
 // Create/attach/open a binary
    const clock_t begin_time = clock();
    BPatch_addressSpace* app = StartInstrumenting(mode, prog_name, prog_pid, prog_argv);
    if (!app) {
        fprintf(stderr, "StartInstrumenting failed\n");
        exit(1);
    }
    LookupReadOnlyRegion(argv[1], app);
    InstrumentMemoryAccesses(app, romp_path);
    ParseDwarfLineMap(argv[1]);
    for (auto file : stringTable) {
        cout << file << endl;
    }    
    size_t string_table_size = 0;
    void* string_table_chunk = serializeStringTable(string_table_size); // build the string table binary from the stringTable vector 
    cout << "string table chunk size: " << string_table_size << endl;
    size_t line_map_size = 0;   
    void* line_map_chunk = serializeLineInformation(line_map_size);
    cout << "line map chunk size: " << line_map_size << endl;

    InsertParsedDebugSection(argv[1], string_table_chunk, string_table_size, line_map_chunk, line_map_size, app);
    
    const char* prog_name_rewritten = "instrumented_app";
    cout << "Instrumentation time: " << float(clock() - begin_time)/ CLOCKS_PER_SEC << " sec" << endl;
    FinishInstrumenting(app, prog_name_rewritten);
    free(string_table_chunk); //free the string table chunk after rewritting
    free(line_map_chunk);     //free the linemap chunk after rewriting 
}
