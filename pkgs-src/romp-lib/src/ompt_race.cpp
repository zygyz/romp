#include <assert.h>
#include <chrono>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <gelf.h>
#include <mutex>
#include <omp.h>
#include <ompt.h>
#include <pthread.h>
#include <set>
#include <stack>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <signal.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>
#include "AccessHistory.h"
#include "ShadowMemory.cpp"
#include "TaskLabel.h"
#include "pfq-rwlock.h"
#include "whereami.h"
#define THREAD_TASK_NUM  16
#define FAST_MODE
//#define PERFORMANCE_INSPECT

using namespace std;

static bool global_race_found = false;

static bool global_verbose_output = false;
//#define DEBUG_WORKSHARE_LOOP
#ifdef DEBUG_WORKSHARE_LOOP
atomic<int> workshare_cnt;
#endif

#ifdef DEBUG_REDUCE 
atomic<int> reduction_cnt;
#endif

//#define DEBUG_ORDERED_SECTION
#ifdef DEBUG_ORDERED_SECTION
atomic<int> ordered_cnt;
#endif

static bool global_has_reduction = false;

typedef int (*ompt_callback_set_t)(
    ompt_callbacks_t which,
    ompt_callback_t callback
);    

typedef struct ompt_task_data_struct_range_s {
    void* start_of_struct;
    uint32_t size_of_struct;
} ompt_task_data_struct_range_t;

typedef struct task_dep_record_s {
    struct task_dep_record_s * next;
    void* task_data_ptr; 
    task_dep_record_s() {
        next = nullptr;
        task_data_ptr = nullptr;
    }
    task_dep_record_s(void* task_data_ptr_in) {
        next = nullptr;
        task_data_ptr = task_data_ptr_in;  
    }
} task_dep_record_t;

typedef struct TaskDependence {
    task_dep_record_t * input_dependences; 
    task_dep_record_t * output_dependences; // there exists an directed edge from every node in output_dependences to every node in input_dependences. This is a representation of dag 

    TaskDependence() {
        input_dependences = new task_dep_record_t();
        output_dependences = new task_dep_record_t(); 
    }    

    ~TaskDependence() {
       // release all the memory         
       while (input_dependences->next) {
           auto tmp = input_dependences->next;
           input_dependences->next = tmp->next;
           delete tmp;
       }  
       delete input_dependences;
       while (output_dependences->next) {
           auto tmp = output_dependences->next;
           output_dependences->next = tmp->next;
           delete tmp;
       }
       delete output_dependences;
    }

    void AddInputDependence(task_dep_record_t* neu_input_record) {
        auto cur_first = input_dependences->next;
        input_dependences->next = neu_input_record; // add new record to head 
        neu_input_record->next = cur_first; 
    }    

    void AddOutputDependence(task_dep_record_t* neu_output_record) {
        auto cur_first = output_dependences->next;
        output_dependences->next = neu_output_record;
        neu_output_record->next = cur_first;
    }
} TaskDependence;

typedef struct ParRegionData {
    mcs_lock_t par_reg_lock; // this is for protecting the dependency hashing 

    unordered_map<void*, TaskDependence*> dep_var_hash; 

    ParRegionData() {
        mcs_init(&par_reg_lock);    
    }

    ~ParRegionData() {
        for (auto p : dep_var_hash) {
            KN_TRACE(0, STDERR, 0, "deleting ", "dep node %d", dep_var_hash.size());
            delete p.second;
        }          
        dep_var_hash.clear();
       KN_TRACE(0, STDERR, 0, "delete ", "finished node", 0);
    }
} ParRegionData; 

using ParRegionDataPtr = std::shared_ptr<ParRegionData>;

typedef struct TaskData {
    LabelPtr label;
   // LabelPtr parent_label;//this eliminates the need for calling parent task query fucntion at the cost of one pointer size    
    LockSetPtr lockset;

    bool in_reduction;         

    vector<void*> children_explicit_task_data; // this is to record all direct children of type explicit [We can use linked list implementetion to reduce over head, but don't do this until hpctoolkit found it necessary]

    vector<void*>  var_output_dep; // record all the variable addresses that this task emit output dependency.[We can use linked list implementation to reduce over head, but don't do this until hpctoolkit found it necessary]  

    void* par_region_data_ptr; 

    bool is_explicit;
    
    void* start_of_td;

    uint64_t size_of_td; // NOTE: only contains private data part

    bool suspended;

    bool has_dep;

    bool is_completed; 

    TaskData() {
        label = nullptr; 
        lockset = nullptr;
        in_reduction = false;
        is_explicit = false;
        has_dep = false;
        par_region_data_ptr = nullptr; // record the pointer to the parallel region data
        start_of_td = nullptr;
        size_of_td = 0;
        suspended = false;
        is_completed = false;  
    }

    ~TaskData() {
        label.reset();
        lockset.reset();  
        children_explicit_task_data.clear();
        var_output_dep.clear();
    }
} TaskData;

using TaskDataPtr = std::shared_ptr<TaskData>;


typedef struct ThreadData {
    /* For a thread, stack private access should be stack_base_addr <= addr <= stack_top_addr
     * stack_top_addr = stack_base_addr + stack_size */
    void* stack_base_addr; 
    void* stack_top_addr;
    void* active_task_exit_frame; 
    void* lowest_accessed_addr;      
    
    void SetLowestAddr(void* addr) {
        if ((uint64_t)addr < (uint64_t)lowest_accessed_addr) 
            lowest_accessed_addr = addr;
    } 

    void ResetLowestAddr() {
        lowest_accessed_addr = (void*)0xffffffffffff;    
    }

    ThreadData() {
        stack_base_addr = nullptr;
        stack_top_addr = nullptr;  
        active_task_exit_frame = nullptr; 
        lowest_accessed_addr = (void*)0xffffffffffff;
    }

} ThreadData;

using ThreadDataPtr = std::shared_ptr<ThreadData>;

/*
ompt_interface_fn_t get_task_info_fn;
ompt_interface_fn_t get_thread_data_fn;
ompt_interface_fn_t get_parallel_info_fn;

static ompt_get_task_info_t ompt_get_task_info = NULL;
static ompt_get_thread_data_t ompt_get_thread_data = NULL;
static ompt_get_parallel_info_t ompt_get_parallel_info = NULL;
*/

static TaskDataPtr g_init_task_ptr = NULL; // the shared_ptr is only used to manage initial task. Only be destructed after whole program exits;
static bool ompt_initialized = false;
static FILE* gTraceFile;
static uint64_t g_thread_stack_size = 0;
static void* gStringTablePtr = NULL;
static void* gLineMapPtr = NULL;
static uint64_t gStringTableSizeInByte = 0;
static uint64_t gLineMapSizeInByte = 0;
static uint32_t gLineMapCellSize = sizeof(uint64_t) + sizeof(uint16_t) * 3; 

static ompt_set_callback_t ompt_set_callback;
static ompt_get_callback_t ompt_get_callback;
static ompt_get_state_t ompt_get_state;
static ompt_get_task_info_t ompt_get_task_info;
static ompt_get_thread_data_t ompt_get_thread_data;
static ompt_get_parallel_info_t ompt_get_parallel_info;
static ompt_get_unique_id_t ompt_get_unique_id;
//static ompt_get_num_procs_t ompt_get_num_procs;
static ompt_get_num_places_t ompt_get_num_places;
static ompt_get_place_proc_ids_t ompt_get_place_proc_ids;
static ompt_get_place_num_t ompt_get_place_num;
static ompt_get_partition_place_nums_t ompt_get_partition_place_nums;
static ompt_get_proc_id_t ompt_get_proc_id;
static ompt_enumerate_states_t ompt_enumerate_states;
static ompt_enumerate_mutex_impls_t ompt_enumerate_mutex_impls;

#ifdef PERFORMANCE_INSPECT
static unordered_map<uint64_t, uint64_t> gHist;
static mcs_lock_t gHistLock; // this is for protecting the dependency hashing 
#endif

/*
static unordered_map<uint64_t, pair<uint16_t, pair<uint16_t, uint16_t> > > gDebugInfoHash;
static pfq_rwlock_t  gHashLock;
*/


enum Direction
{
    LEFT_TO_RIGHT = 0, 
    RIGHT_TO_LEFT=1,
    PARALLEL=2, 
    SAME=3, 
    SHORTER_TO_LONGER=4, // ??
    LONGER_TO_SHORTER=5, // ?
    ERROR=6,// ??
    VOID=8
};

static inline int 
Reverse(Direction direction)
{
    if (direction == PARALLEL) return PARALLEL;
    return  1 - direction;
   //return (direction + 1)%2; // 0->1, 1->0
}

static inline bool
IsVoidTaskGroupLabel(TaskGroupPtr& tg)
{
    return tg->label[0] == 0;
}

#define SEG_CAST_32(PTR) \
    static_cast<LabelSegmentByte32*>((PTR)) \

#define SEG_CAST_16(PTR) \
    static_cast<LabelSegmentByte16*>((PTR)) \

#define PROTOCOL_CASE_REVERSE(ENCODE, FUNC, FROM_PTR, TO_PTR)\
    case (ENCODE):\
        result = Reverse(FUNC((TO_PTR), (FROM_PTR))); \
        break \

#define PROTOCOL_CASE(ENCODE, FUNC, FROM_PTR, TO_PTR) \
            case (ENCODE):                            \
               result = FUNC((FROM_PTR), (TO_PTR));   \
               break \


#define DEBUG_ENCODE_SWITCH 1000

#define CASE_DEL_HIST(ENCODE)                                   \
            case (ENCODE):                                      \
                remove_hist = true;                             \
                break                                         \

#define CASE_REPORT_RACE(ENCODE, ACCESS_TYPE, INSTN_ADDR, ANOTHER_INSTN_ADDR, ADDRESS) \
        case (ENCODE): \
             race_found = true;\
             ReportRace(ACCESS_TYPE, INSTN_ADDR, ANOTHER_INSTN_ADDR, ADDRESS); \
             break    \

#define CASE_NO_ADD_CURRENT(ENCODE)                             \
            case (ENCODE):                                      \
                skip_current_eligible = true;                   \
                break                                          \


#define CASE_NO_ADD_CURRENT_AND_RETURN(ENCODE)                  \
            case (ENCODE):                                      \
                return false                                   \

#define CASE_WARNING(ENCODE, HIST, CUR)  \
        case (ENCODE): \
            KA_TRACE(0, STDERR, 0, "warning", "cur->hist: %s->%s",CUR->ToString().c_str(), HIST->ToString().c_str()) \
            break \

#define RACE_ON_ME(MEM_STATE) \
            (((char)MEM_STATE & 0x80) == 0x80) \

#define SET_RACE_ON_ME(MEM_STATE) \
            ((char)MEM_STATE | 0x80) \

enum LOCKSET_RELATION_ENCODE
{
    HIST_NULL_CUR_NULL=0,
    HIST_NOT_NULL_CUR_NULL = 4,
    HIST_NULL_CUR_NOT_NULL = 2,
    HIST_CUR_HAS_COMMON = 1,
    HIST_INCLUDES_CUR = 3,
    CUR_INCLUDES_HIST = 5,
    DISJOINT = 7
};

enum ACCESS_TYPE_ENCODE
{
   HIST_READ_CUR_READ = 0, // 0b00
   HIST_READ_CUR_WRITE = 1 , // 0b01
   HIST_WRITE_CUR_READ = 2, // 0b10
   HIST_WRITE_CUR_WRITE = 3 // 0b11
};

enum PROTOCOL_CASE_ENCODE
{
    IMP_IMP = 0x0, // 0b0000
    IMP_EXP = 0x1, // 0b0001
    IMP_LOG = 0x2, // 0b0010
    EXP_IMP = 0x4, // 0b0100
    EXP_EXP = 0x5,// 0b0101 
    EXP_LOG = 0x6,// 0b0110
    LOG_IMP = 0x8,// 0b1000
    LOG_EXP = 0x9,// 0b1001
    LOG_LOG = 0xa, //0b1010
    NULL_IMP =0x10,   // 0b1 0000
    NULL_EXP = 0x11,  // 0b1 0001
    NULL_LOG = 0x12,  // 0b1 0010 
    IMP_NULL = 0x20,  // 0b10 0000
    EXP_NULL = 0x21,  // 0b10 0001
    LOG_NULL = 0x22  // 0b10 0010
};

enum ENCODE
{
    HIST_E_CUR_E_RR_HIST_HB_CUR = 0x00, // hist lockset is empty, current lockset is empty 
    HIST_E_CUR_E_RR_PARALLEL = 0x02, 
    HIST_E_CUR_E_RR_SAME = 0x03, 
    HIST_E_CUR_E_RR_CUR_HB_HIST = 0x01,

    HIST_E_CUR_E_RW_HIST_HB_CUR = 0x04,   
    HIST_E_CUR_E_RW_PARALLEL = 0x06,
    HIST_E_CUR_E_RW_SAME = 0x07,
    HIST_E_CUR_E_RW_CUR_HB_HIST = 0x05,

    HIST_E_CUR_E_WR_HIST_HB_CUR = 0x08,   
    HIST_E_CUR_E_WR_PARALLEL = 0x0a,
    HIST_E_CUR_E_WR_SAME = 0x0b,
    HIST_E_CUR_E_WR_CUR_HB_HIST = 0x09,

    HIST_E_CUR_E_WW_HIST_HB_CUR = 0x0c,   
    HIST_E_CUR_E_WW_PARALLEL = 0x0e,
    HIST_E_CUR_E_WW_SAME = 0x0f,
    HIST_E_CUR_E_WW_CUR_HB_HIST = 0x0d,

    HIST_NE_CUR_E_RR_HIST_HB_CUR = 0x40,
    HIST_NE_CUR_E_RR_PARALLEL = 0x42,
    HIST_NE_CUR_E_RR_SAME = 0x43,
    HIST_NE_CUR_E_RR_CUR_HB_HIST = 0x41,

    HIST_NE_CUR_E_RW_HIST_HB_CUR = 0x44,
    HIST_NE_CUR_E_RW_PARALLEL = 0x46,
    HIST_NE_CUR_E_RW_SAME = 0x47,
    HIST_NE_CUR_E_RW_CUR_HB_HIST = 0x45,

    HIST_NE_CUR_E_WR_HIST_HB_CUR = 0x48,
    HIST_NE_CUR_E_WR_PARALLEL = 0x4a,
    HIST_NE_CUR_E_WR_SAME = 0x4b,
    HIST_NE_CUR_E_WR_CUR_HB_HIST = 0x49,

    HIST_NE_CUR_E_WW_HIST_HB_CUR = 0x4c,
    HIST_NE_CUR_E_WW_PARALLEL = 0x4e,
    HIST_NE_CUR_E_WW_SAME = 0x4f,
    HIST_NE_CUR_E_WW_CUR_HB_HIST = 0x4d,

    HIST_E_CUR_NE_RR_HIST_HB_CUR = 0x20,
    HIST_E_CUR_NE_RR_PARALLEL = 0x22,
    HIST_E_CUR_NE_RR_SAME = 0x23,
    HIST_E_CUR_NE_RR_CUR_HB_HIST = 0x21,

    HIST_E_CUR_NE_RW_HIST_HB_CUR = 0x24,
    HIST_E_CUR_NE_RW_PARALLEL = 0x26,
    HIST_E_CUR_NE_RW_SAME = 0x27,
    HIST_E_CUR_NE_RW_CUR_HB_HIST = 0x25,

    HIST_E_CUR_NE_WR_HIST_HB_CUR = 0x28,
    HIST_E_CUR_NE_WR_PARALLEL = 0x2a,
    HIST_E_CUR_NE_WR_SAME = 0x2b,
    HIST_E_CUR_NE_WR_CUR_HB_HIST = 0x29,

    HIST_E_CUR_NE_WW_HIST_HB_CUR = 0x2c,
    HIST_E_CUR_NE_WW_PARALLEL = 0x2e,
    HIST_E_CUR_NE_WW_SAME = 0x2f,
    HIST_E_CUR_NE_WW_CUR_HB_HIST = 0x2d,

    COMMON_LOCK_RR_HIST_HB_CUR = 0x10,
    COMMON_LOCK_RR_PARALLEL = 0x12, 
    COMMON_LOCK_RR_SAME = 0x13,  
    COMMON_LOCK_RR_CUR_HB_HIST = 0x11,

    COMMON_LOCK_RW_HIST_HB_CUR = 0x14,
    COMMON_LOCK_RW_PARALLEL = 0x16, 
    COMMON_LOCK_RW_SAME = 0x17,  
    COMMON_LOCK_RW_CUR_HB_HIST = 0x15,

    COMMON_LOCK_WR_HIST_HB_CUR = 0x18,
    COMMON_LOCK_WR_PARALLEL = 0x1a, 
    COMMON_LOCK_WR_SAME = 0x1b,  
    COMMON_LOCK_WR_CUR_HB_HIST = 0x19,

    COMMON_LOCK_WW_HIST_HB_CUR = 0x1c,
    COMMON_LOCK_WW_PARALLEL = 0x1e, 
    COMMON_LOCK_WW_SAME = 0x1f,  
    COMMON_LOCK_WW_CUR_HB_HIST = 0x1d,

    HIST_CNTN_CUR_RR_HIST_HB_CUR = 0x30, // hist contains current 
    HIST_CNTN_CUR_RR_PARALLEL = 0x32, // hist contains current 
    HIST_CNTN_CUR_RR_SAME = 0x33, // hist contains current 
    HIST_CNTN_CUR_RR_CUR_HB_HIST = 0x31, // hist contains current 

    HIST_CNTN_CUR_RW_HIST_HB_CUR = 0x34, 
    HIST_CNTN_CUR_RW_PARALLEL = 0x36, 
    HIST_CNTN_CUR_RW_SAME = 0x37, 
    HIST_CNTN_CUR_RW_CUR_HB_HIST = 0x35, 

    HIST_CNTN_CUR_WR_HIST_HB_CUR = 0x38, 
    HIST_CNTN_CUR_WR_PARALLEL = 0x3a,  
    HIST_CNTN_CUR_WR_SAME = 0x3b, 
    HIST_CNTN_CUR_WR_CUR_HB_HIST = 0x39, 

    HIST_CNTN_CUR_WW_HIST_HB_CUR = 0x3c, 
    HIST_CNTN_CUR_WW_PARALLEL = 0x3e,  
    HIST_CNTN_CUR_WW_SAME = 0x3f, 
    HIST_CNTN_CUR_WW_CUR_HB_HIST = 0x3d, 

    CUR_CNTN_HIST_RR_HIST_HB_CUR = 0x50, // hist contains current 
    CUR_CNTN_HIST_RR_PARALLEL = 0x52, // hist contains current 
    CUR_CNTN_HIST_RR_SAME = 0x53, // hist contains current 
    CUR_CNTN_HIST_RR_CUR_HB_HIST = 0x51, // hist contains current 

    CUR_CNTN_HIST_RW_HIST_HB_CUR = 0x54, 
    CUR_CNTN_HIST_RW_PARALLEL = 0x56, 
    CUR_CNTN_HIST_RW_SAME = 0x57, 
    CUR_CNTN_HIST_RW_CUR_HB_HIST = 0x55, 

    CUR_CNTN_HIST_WR_HIST_HB_CUR = 0x58, 
    CUR_CNTN_HIST_WR_PARALLEL = 0x5a,  
    CUR_CNTN_HIST_WR_SAME = 0x5b, 
    CUR_CNTN_HIST_WR_CUR_HB_HIST = 0x59, 

    CUR_CNTN_HIST_WW_HIST_HB_CUR = 0x5c, 
    CUR_CNTN_HIST_WW_PARALLEL = 0x5e,  
    CUR_CNTN_HIST_WW_SAME = 0x5f, 
    CUR_CNTN_HIST_WW_CUR_HB_HIST = 0x5d, 

    DISJOINT_RR_HIST_HB_CUR = 0x70,
    DISJOINT_RR_PARALLEL = 0x72,
    DISJOINT_RR_SAME = 0x73,
    DISJOINT_RR_CUR_HB_HIST = 0x71, 

    DISJOINT_RW_HIST_HB_CUR = 0x74,
    DISJOINT_RW_PARALLEL = 0x76,
    DISJOINT_RW_SAME = 0x77,
    DISJOINT_RW_CUR_HB_HIST = 0x75, 

    DISJOINT_WR_HIST_HB_CUR = 0x78,
    DISJOINT_WR_PARALLEL = 0x7a,
    DISJOINT_WR_SAME = 0x7b,
    DISJOINT_WR_CUR_HB_HIST = 0x79, 

    DISJOINT_WW_HIST_HB_CUR = 0x7c,
    DISJOINT_WW_PARALLEL = 0x7e,
    DISJOINT_WW_SAME = 0x7f,
    DISJOINT_WW_CUR_HB_HIST = 0x7d
};


enum RaceType
{
    WW = 1,
    WR = 2,
    RW = 3,
    DYNAMIC_FOUND=4

};

enum DataSharingAttr
{
    THREAD_PRIVATE_BELOW_EXIT = 0, // 0b0
    THREAD_PRIVATE_ABOVE_EXIT = 2, // 0b10
    NON_THREAD_PRIVATE = 3, // 0b11
    EXPLICIT_TASK_PRIVATE = 4, // 0b100
    EXPLICIT_TASK_THREAD_PRIVATE = 5, // 0b101
   
};

/*  OMPT CALLBACK FUNCTIONS */

void
on_ompt_callback_mutex_acquired(
    ompt_mutex_kind_t kind,
    omp_wait_id_t wait_id,
    const void *codeptr)
{
    ompt_data_t task_data;
    ompt_data_t* task_data_t = &task_data;
    ompt_data_t** task_data_tt = &task_data_t;
   
    omp_frame_t frame;
    omp_frame_t* frame_t = &frame;
    omp_frame_t** frame_tt = &frame_t;

    int task_type;  
    if (!ompt_get_task_info(0, &task_type, task_data_tt, frame_tt, NULL, NULL)) { 
        KA_TRACE(0, STDERR, 0, "on_ompt_callback_mutex_acquired","canot get task info", 0);
        return; 
    }
 
    auto ptr = task_data_t->ptr;  
    //assert(ptr != nullptr); 
    TaskData* task_data_ptr = static_cast<TaskData*>(ptr);     

    if (kind == ompt_mutex_ordered) { // entering ordered section
#ifdef DEBUG_ORDERED_SECTION
        ordered_cnt++;          
#endif
        auto cur_label = task_data_ptr->label;
        auto modified_task_label = ModifyLabelAcquiredOrdered(cur_label);
        task_data_ptr->label = std::move(modified_task_label); // reset the label to be the new one
    }  else { // lock / nested lock / critical / atomic
        if (task_data_ptr->lockset == nullptr) {
            // first  time encounter 
            task_data_ptr->lockset = make_shared<LockSetInfo>(); 
        }
        task_data_ptr->lockset->add_lock(wait_id); // insert the lock to lockset. 
    }
}    

void 
on_ompt_callback_mutex_released(
    ompt_mutex_kind_t kind,
    omp_wait_id_t wait_id, 
    const void *codeptr)
{
    int task_type;    
    ompt_data_t task_data;
    ompt_data_t* task_data_t = &task_data;
    ompt_data_t** task_data_tt = &task_data_t;
   
    omp_frame_t frame;
    omp_frame_t* frame_t = &frame;
    omp_frame_t** frame_tt = &frame_t;

    if (!ompt_get_task_info(0, &task_type, task_data_tt, frame_tt, NULL, NULL)) { 
        KA_TRACE(0, STDERR, 0, "on_ompt_callback_mutex_released","cannot get current info", 0);
        return; 
    }
 
    auto ptr = task_data_t->ptr;  
    assert(ptr != nullptr); 
    auto task_data_ptr = static_cast<TaskData*>(ptr);     
    if (kind == ompt_mutex_ordered) {
#ifdef DEBUG_ORDERED_SECTION
        ordered_cnt++;          
#endif
        auto cur_label = task_data_ptr->label;
        auto modified_task_label = ModifyLabelReleaseOrdered(cur_label);
        task_data_ptr->label = std::move(modified_task_label); // reset the label to be the new one
    } else {
        task_data_ptr->lockset->remove_lock(wait_id);   
    }
} 

void
on_ompt_callback_parallel_begin(
    ompt_data_t *parent_task_data,
    const omp_frame_t* parent_frame,
    ompt_data_t *parallel_data,
    unsigned int requested_team_size, 
    ompt_invoker_t invoker, 
    const void* codeptr_ra) 
{
    auto par_ptr = new ParRegionData();
    parallel_data->ptr = (void*)par_ptr;
}


void
on_ompt_callback_parallel_end(
    ompt_data_t *parallel_data,
    ompt_data_t *task_data,
    ompt_invoker_t invoker,
    const void* codeptr_ra)
{
    auto ptr = parallel_data->ptr;
    //delete (ParRegionData*)ptr; //TODO : fix the seg fault when freeing 
}

static int
GetNumDataUnitForAccessHistory(AccessHistory * from_me)
{
    auto lock_ptr = &(from_me->lock);
    //pfq_rwlock_node_t me;
    mcs_node_t me;
    mcs_lock(lock_ptr, &me);
//    pfq_rwlock_read_lock(lock_ptr);
    int num_data_unit = 0;
    GET_NUM_DATA_UNIT_ACCESSED(from_me->mem_state, num_data_unit);
    mcs_unlock(lock_ptr, &me);
 //   pfq_rwlock_read_unlock(lock_ptr);
    return num_data_unit;
}

static void
MarkDataUnitUnAllocated(AccessHistory * from_me)
{
    auto lock_ptr = &(from_me->lock);
    mcs_node_t me;
    mcs_lock(lock_ptr, &me);
    //pfq_rwlock_node_t me;
   // pfq_rwlock_write_lock(lock_ptr, &me);
    SET_DATA_UNIT_DEALLOC(from_me->mem_state);     
  //  pfq_rwlock_write_unlock(lock_ptr, &me);
   mcs_unlock(lock_ptr, &me);
}

static void
RangeMarkDeallocation(void* lower_bound, void* upper_bound) 
{
    for (void* address = (void*)lower_bound; (uint64_t)address <= (uint64_t)upper_bound;  address = (void*)((uint64_t)address + 4)) {
        AccessHistory* access_history_base = GetAllocatedShadowBaseAddress<AccessHistory>(address);
        if (access_history_base == nullptr) {
            //KN_TRACE(0, STDERR, 0, "RangeMarkDeallocation","shadow page for address %p is not allocated", address);
            continue;
        }
        long first_slot_index = (long)(PAGE_OFFSET((uint64_t)address));
        AccessHistory* access_history = (AccessHistory*)access_history_base + first_slot_index;
        int num_data_unit = GetNumDataUnitForAccessHistory(&access_history[0]);
        if (num_data_unit == 0) {
             MarkDataUnitUnAllocated(&access_history[0]);
        } else {
            long last_slot_index = (long)(first_slot_index  + num_data_unit - 1);
            long overflow = last_slot_index - (long)NUM_ENTRY_SHADOW_PAGE + 1;
            if (overflow <= 0) {
                for (uint8_t i = 0; i < num_data_unit; ++i) {
                    MarkDataUnitUnAllocated(&access_history[i]);
                } 
            } else {
                for (uint8_t i = 0; i < num_data_unit - overflow; ++i) {
                    MarkDataUnitUnAllocated(&access_history[i]); 
                }
                access_history = GetAllocatedShadowBaseAddress<AccessHistory>((char*)address + num_data_unit);
                if (access_history == nullptr) {
                    KN_TRACE(0, STDERR, 0, "RangeMarkDeallocation","shadow page for address %p is not allocated", (void*)((char*)address + num_data_unit));
                } else {
                    for (uint8_t i = 0; i < overflow; ++i) {
                        MarkDataUnitUnAllocated(&access_history[i]);
                    } 
                }
            }
        }
    }   
}

void 
on_ompt_callback_task_schedule(
    ompt_data_t * first_task_data,
    ompt_task_status_t prior_task_status,
    ompt_data_t * second_task_data) {
    // think about two concurrent tasks. T1 and T2. T1 and T2 may be scheduled to the same thread, thus sharing thread stack
    // suppose there is no task dependency between these two tasks. 
    // 1. One possible schedule is that T1 is scheduled first, and then suspended and 
    // T2 get schedueld and executed. After T2 completes execution, T1 resumes execution, and if T1 is not tied, it may or may not be executed by the same 
    // thread. The point in this case, if T2 ever touches anything touched by T1 it is problematic. Because T2 and T1's task private access should not get overlapped in this case
    // i.e., T2 is below T1 and T1 get suspended to executed T2. 
    // 2. Another possible schedule is that T1 is scheduled first and finishes. Then T2 is scheduled onto the same thread thus reusing the thread stack for its own task private 
    // accesses. In this case, the thread stack variables touched by T1 should not be 'seen' by T2 although they share the same thread stack address. To solve this problem, mark 
    // the memory accesess done by T1 that falls in the '[exit-runtime-frame : lowest touched thread stack address]'  should be marked as unallocated. This could be done by storing the pointer to
    // TaskData at each access record, and in the TaskData, store the range of the thread stack access. 
    // 3. Another possible schedule is T1 and T2 get scheduled on different threads. Should not have this private thread stack variable overlapping problem. 
    auto first_task_ptr = first_task_data->ptr; 
    assert(first_task_ptr != nullptr);
    auto second_task_ptr = second_task_data->ptr;
    assert(second_task_ptr != nullptr);    
    auto first_task_data_ptr = static_cast<TaskData*>(first_task_ptr);
    auto second_task_data_ptr = static_cast<TaskData*>(second_task_ptr);
    
    ompt_data_t* thread_data_t = ompt_get_thread_data();//tells me which thread current task is scheudled on
    ThreadData* thread_data_ptr = nullptr; 
    if (thread_data_t == NULL) {
        KA_TRACE(0, STDERR, 0, "on_ompt_callback_task_schedule","ompt_get_thread_data() returns null", 0);
        return;
    } else {
        if (thread_data_t->ptr == nullptr) {
            KA_TRACE(0, STDERR, 0, "on_ompt_callback_task_schedule", "thread_data_t->ptr is null", 0);
            return;
        }
        thread_data_ptr = static_cast<ThreadData*>(thread_data_t->ptr); // cast to the thread data pointer
    }  

    if (prior_task_status == ompt_task_complete || prior_task_status == ompt_task_others) {
        // the thread is shifting to second task because first task is completed 
        // iterate over the shdow memory and mark the access records by the completed task as un allocated             
        auto upper_bound = thread_data_ptr->active_task_exit_frame;
        auto lower_bound = thread_data_ptr->lowest_accessed_addr;          
        /*
        KA_TRACE(0, STDERR, 0, "on_ompt_callback_task_schedule","prior task completed / suspended stack access upper bound: %p stack access lower bound: %p", 
                    thread_data_ptr->active_task_exit_frame, thread_data_ptr->lowest_accessed_addr);
        KA_TRACE(1000, STDOUT, 0, "on_ompt_callback_task_schedule","prior task type: %s cur task type: %s", 
                first_task_data_ptr->is_explicit? "exp" : "imp", 
                second_task_data_ptr->is_explicit? "exp" : "imp");
                */
        // should walk through the access history to mark address range in [lower_bound, upper_bound] as deallocated
        RangeMarkDeallocation(lower_bound, upper_bound);
        void* start_of_td = first_task_data_ptr->start_of_td;
        uint64_t size_of_td = first_task_data_ptr->size_of_td;      
        auto heap_upper_bound = (void*)((uint64_t)start_of_td + size_of_td); // mark unallocated for stack explicit task related memory 
        auto heap_lower_bound = start_of_td; 
        RangeMarkDeallocation(heap_lower_bound, heap_upper_bound); // mark unallocated for heap explicit task related memory 
    } 
      
    int task_type;    
    ompt_data_t task_data;
    ompt_data_t* task_data_t = &task_data;
    ompt_data_t** task_data_tt = &task_data_t;
   
    omp_frame_t frame;
    omp_frame_t* frame_t = &frame;
    omp_frame_t** frame_tt = &frame_t;

    if (!ompt_get_task_info(0, &task_type, task_data_tt, frame_tt, NULL, NULL)) { 
        KA_TRACE(0, STDERR, 0, "on_ompt_callback_task_schedule","ompt_get_task_info() returns null", 0);
        return; 
    }

    void* current_exit = frame_t->exit_frame;  // this is the upper bound of current task's thread stack private access 
    thread_data_ptr->active_task_exit_frame = current_exit;
    thread_data_ptr->lowest_accessed_addr = current_exit; // should stack access for this task below this pointer 
}

void
on_ompt_callback_task_dependences(
    ompt_data_t *task_data,
    const ompt_task_dependence_t * deps,
    int ndeps)
{
   KA_TRACE(0, STDOUT, 0, "on_ompt_task_dependences", "called", 0);           
    auto tsk_ptr = task_data->ptr;
    assert(tsk_ptr != nullptr);
    auto par_ptr = static_cast<TaskData*>(tsk_ptr)->par_region_data_ptr; 
    assert(par_ptr != nullptr); // if this task has dependence , it should record the par ptr
    auto par_region_ptr = static_cast<ParRegionData*>(par_ptr);
    auto lock_ptr = &(par_region_ptr->par_reg_lock);
    mcs_node_t me;
    mcs_lock(lock_ptr, &me);  //potential concurrent access to this parallel region data structure, use mutual exclusion
    for (int i = 0; i < ndeps; ++i) {
        auto var_addr = deps[i].variable_addr;
       if (par_region_ptr->dep_var_hash.find(var_addr) == par_region_ptr->dep_var_hash.end()) { // double checked locking, double check to see if some one else allocated the data
            par_region_ptr->dep_var_hash[var_addr] = new TaskDependence();
        }
        auto task_dep_record = new task_dep_record_t(tsk_ptr); // for each variable one should create a new record 
        switch(deps[i].dependence_flags) {
            case ompt_task_dependence_type_in:
                par_region_ptr->dep_var_hash[var_addr]->AddInputDependence(task_dep_record);
                break;
            case ompt_task_dependence_type_inout: // treat as output
                par_region_ptr->dep_var_hash[var_addr]->AddInputDependence(task_dep_record);
                par_region_ptr->dep_var_hash[var_addr]->AddOutputDependence(task_dep_record);
                static_cast<TaskData*>(tsk_ptr)->var_output_dep.push_back(var_addr); // if it is output dependency, also record inthe task data the variabel address 
                break;
            case ompt_task_dependence_type_out:
                par_region_ptr->dep_var_hash[var_addr]->AddInputDependence(task_dep_record);
                par_region_ptr->dep_var_hash[var_addr]->AddOutputDependence(task_dep_record);
                static_cast<TaskData*>(tsk_ptr)->var_output_dep.push_back(var_addr);
                break;
            default:
                assert(false);
        }
    } 
    mcs_unlock(lock_ptr, &me);
}

void 
on_ompt_callback_dispatch(
    ompt_data_t *parallel_data,
    ompt_data_t *task_data,
    ompt_dispatch_t kind,
    ompt_data_t instance)
{
    assert(task_data != nullptr);
#ifdef DEBUG_WORKSHARE_LOOP
    workshare_cnt++;
    assert(lo_id == hi_id);
#endif         
#ifdef DEBUG
    KA_TRACE(0, STDOUT, 0, "on_ompt_callback_dispatch", "called", 0);           
#endif
    auto ptr = task_data->ptr;  
    assert(ptr != nullptr); 
    auto task_data_ptr = static_cast<TaskData*>(ptr);     
    if (kind == ompt_dispatch_iteration) {
        auto cur_label = task_data_ptr->label;
        auto tail_ptr = SEG_CAST_16(cur_label->GetTail());
        auto label_type = GetLabelType(tail_ptr->v);//type of the label is indicated by the last segment  
        /*
        if (label_type != LOGICAL) {
            assert(label_type == LOGICAL); 
        }
        */
        LabelSegmentByte24* extended_segment = new LabelSegmentByte24(); // create the new label segment 
        SetLabelType(extended_segment->v, LOGICAL);
        SetOffsetSpan(extended_segment->v, 0, 1); 
        SetIterIndex(extended_segment->iter, instance.value);
        LabelCreateInfo create_info;  
        create_info.extended_segment = static_cast<void*>(extended_segment);
        create_info.to_copy = cur_label;  
        create_info.num_seg_no_copy = 1; // discard last segment
        auto new_task_label = CreateLabelDispatchNext(create_info);
        task_data_ptr->label = std::move(new_task_label); // reset the label to be the new one
#ifdef DEBUG_LABEL_CREATE
        auto new_label_str = task_data_ptr->label->ToString();
#endif
    } else if (kind == ompt_dispatch_section) {
        KA_TRACE(0, STDOUT, 0, "ompt_dispatch_section", "called", 0);           
    } 
}

void
on_ompt_callback_work(
    ompt_work_type_t wstype,
    ompt_scope_endpoint_t endpoint,
    ompt_data_t *parallel_data,
    ompt_data_t *task_data,
    uint64_t count,
    const void *codeptr_ra)
{
    assert(task_data != nullptr);
    auto ptr = task_data->ptr;
    if (ptr == nullptr) return;
    //assert(ptr != nullptr);
    auto task_data_ptr = static_cast<TaskData*>(ptr);
    auto cur_label = task_data_ptr->label;
    LabelCreateInfo create_info;
    create_info.to_copy = cur_label;
    if (wstype == ompt_work_loop) {
        if (endpoint == ompt_scope_begin) { // append the place holder segment
            create_info.num_seg_no_copy = 0;   // copy all
            LabelSegmentByte24* extended_segment = new LabelSegmentByte24();
            SetLabelType(extended_segment->v, LOGICAL);
            create_info.extended_segment = static_cast<void*>(extended_segment);
            auto new_task_label = CreateLabelLoopBegin(create_info);  
            task_data_ptr->label = std::move(new_task_label);    
        } else { // ompt_scope_end
#ifdef DEBUG_LABEL_CREATE 
            auto tail_ptr = SEG_CAST_16(cur_label->GetTail());
            auto label_type = GetLabelType(tail_ptr->v);     
            assert(label_type == LOGICAL);
#endif
    // create a new label by deleteing the last label segment   
            create_info.num_seg_no_copy = 1; // ignore the last segment
            auto new_task_label = CreateLabelLoopEnd(create_info);
            task_data_ptr->label = std::move(new_task_label);    
        }
    } else if (wstype == ompt_work_sections) {
        /* we need openmp runtime library support for ompt callback of section dispatch event to integrate sections into the romp labeling scheme. */
        /*
        if (endpoint == ompt_scope_begin) { // begin the section scope
            create_info.num_seg_no_copy = 0;   // copy all
            LabelSegmentByte24* extended_segment = new LabelSegmentByte24();
            SetLabelType(extended_segment->v, LOGICAL);
            SetSectionsHolderBit(extended_segment->v);
            create_info.extended_segment = static_cast<void*>(extended_segment);
            auto new_task_label = CreateLabelSectionsBegin(create_info);  
            task_data_ptr->label = std::move(new_task_label);    
        } else {
            create_info.num_seg_no_copy = 1;
            auto new_task_label = CreateLabelSectionsEnd(create_info);
            task_data_ptr->label = std::move(new_task_label);
        }
        */
    } 
}

inline void
NotifyExplicitChildsTaskWait(TaskData* task_data)
{
    // settign the taskwaited bit in direct explicit children            
    if (task_data->children_explicit_task_data.empty()) { //no explicit tasks 
        return; // should be the most common case
    } else {
        for (auto p : task_data->children_explicit_task_data) {
            // for each task data ptr, get the label and toggle the bit 
            auto task_ptr = (TaskData*)p;
            assert(task_ptr->label != nullptr); // safety first
            auto tail_ptr = SEG_CAST_16(task_ptr->label->GetTail()); 
            SetTaskWaitedFlag(tail_ptr->v);   // no the child explicit task knows that he is waited 
        }        
        task_data->children_explicit_task_data.clear(); // now we shold forget about the children explicit tasks because we have notified them.         
    }
}

void 
on_ompt_callback_sync_region(
    ompt_sync_region_kind_t kind,
    ompt_scope_endpoint_t endpoint,
    ompt_data_t *parallel_data,
    ompt_data_t *task_data,
    const void* codeptr_ra)
{
    if (!task_data || task_data->ptr == nullptr) {
        return;
    }

    if (kind == ompt_sync_region_barrier && endpoint == ompt_scope_end) {
        auto task_data_ptr = static_cast<TaskData*>(task_data->ptr);
        task_data_ptr->label = std::move(ModifyLabelBarrier(task_data_ptr->label));
        return;
    }

    if (kind == ompt_sync_region_taskwait && endpoint == ompt_scope_end) {
        auto task_data_ptr = static_cast<TaskData*>(task_data->ptr);
        task_data_ptr->label = std::move(ModifyLabelTaskWait(task_data_ptr->label));
        // need to notify the direct explicit children tasks of task wait sync pointer encountered       
        NotifyExplicitChildsTaskWait(task_data_ptr);  
        return;
    }  

    if (kind == ompt_sync_region_taskgroup && endpoint == ompt_scope_begin) {
        auto task_data_ptr = static_cast<TaskData*>(task_data->ptr); 
        task_data_ptr->label = std::move(ModifyLabelTaskGroupBegin(task_data_ptr->label));
        return;
    } 

    if (kind == ompt_sync_region_taskgroup && endpoint == ompt_scope_end) {
        auto task_data_ptr = static_cast<TaskData*>(task_data->ptr); 
        task_data_ptr->label = std::move(ModifyLabelTaskGroupEnd(task_data_ptr->label));
        return;
    } 
}  

void
on_ompt_callback_reduction(
    ompt_sync_region_kind_t kind,
    ompt_scope_endpoint_t endpoint,
    ompt_data_t *parallel_data,
    ompt_data_t *task_data,
    const void *codeptr_ra)
{
    auto task_data_ptr = static_cast<TaskData*>(task_data->ptr); 
    if (endpoint == ompt_scope_begin) {
        task_data_ptr->in_reduction = true; 
        global_has_reduction = true;
    } else {
        task_data_ptr->in_reduction = false;
    }

} 
    
void 
on_ompt_callback_thread_begin(
    ompt_thread_type_t thread_type,
    ompt_data_t* thread_data)
{
    if (thread_data == nullptr) {
        return;
    }   
    auto new_thread_data = new ThreadData();
    if (thread_type == ompt_thread_initial) {
        int s;
        pthread_attr_t attr;
        s = pthread_getattr_np(pthread_self(), &attr); 
        if (s != 0) {
        }      
        size_t stacksize;
        void* stackaddr;       
        s = pthread_attr_getstack(&attr, &stackaddr, &stacksize);  
        if (s != 0) {
        } 
        new_thread_data->stack_base_addr = stackaddr;
        new_thread_data->stack_top_addr = (void*)((uint64_t)stackaddr + (uint64_t)stacksize);
    }
    thread_data->ptr = static_cast<void*>(new_thread_data);
}  

void
on_ompt_callback_thread_end(
    ompt_data_t * thread_data)
{
    if (thread_data == nullptr) {
        return;
    }     
    if (thread_data->ptr != NULL) {
        delete static_cast<ThreadData*>(thread_data->ptr);
        thread_data->ptr = NULL;
    }     
} 

void 
on_ompt_callback_task_create(
    ompt_data_t * parent_task_data,
    const omp_frame_t *parent_frame,
    ompt_data_t* new_task_data,
    int type,
    int has_dependences,
    const void* codeptr_ra)
{
    if (type == ompt_task_initial) { // have ompt_task_initial is very good here.
        g_init_task_ptr = make_shared<TaskData>(); // only called once should never be racing
        auto label = make_shared<Label>();      
        auto seg = new LabelSegmentByte16();        
        SetOffsetSpan(seg->v, 0, 1); 
        SetLabelType(seg->v, IMPLICIT);
        label->Append(static_cast<void*>(seg)); 
        //auto lockset = make_shared<LockSetInfo>();     
        g_init_task_ptr->label = std::move(label);   
        //g_init_task_ptr->lockset = std::move(lockset);
        new_task_data->ptr = static_cast<void*>(g_init_task_ptr.get());
        return;
    } 

    if (type == ompt_task_explicit) {
        // explicit task is being created       
        // First create a new task label for the new task 
        // Then modify the parent task label by adding one to the task create count         
        auto parent_task_data_ptr = static_cast<TaskData*>(parent_task_data->ptr);

        LabelSegmentByte16* extended_segment = new LabelSegmentByte16();    
        SetLabelType(extended_segment->v, EXPLICIT);
        SetOffsetSpan(extended_segment->v, 0, 1);    
        LabelCreateInfo create_info;
        create_info.num_seg_no_copy = 0;
        create_info.extended_segment = static_cast<void*>(extended_segment);
        auto parent_label = parent_task_data_ptr->label;      
        create_info.to_copy = parent_label;

        auto new_task_label = CreateLabelExplicitTaskFork(create_info);
        auto new_task_data_ptr = new TaskData(); // the memory allocated for the task to hold the shared pointer to the label 
        new_task_data_ptr->is_explicit = true;
        if (has_dependences) new_task_data_ptr->has_dep = true;
        new_task_data_ptr->label = std::move(new_task_label);        
        new_task_data->ptr = static_cast<void*>(new_task_data_ptr);  // store the new task label here
        // then modify the parent task label             
        parent_task_data_ptr->label = std::move(ModifyParentLabelExplicitTaskFork(parent_label));
        parent_task_data_ptr->children_explicit_task_data.push_back((void*)new_task_data_ptr);// remember the child explicit task pointer 

        const ompt_task_data_struct_range_t* range_t = (ompt_task_data_struct_range_t*)(codeptr_ra);  // record the range of private access
        new_task_data_ptr->start_of_td  = range_t->start_of_struct;
        new_task_data_ptr->size_of_td = (uint64_t)(range_t->size_of_struct);

        if (has_dependences) { // only store the parallel region task data pointer if current task has dependences
            int team_size;
            ompt_data_t parallel_data;
            ompt_data_t* parallel_data_t = &parallel_data;
            ompt_data_t** parallel_data_tt = &parallel_data_t;
            if (!ompt_get_parallel_info(0, parallel_data_tt, &team_size)) {
                return; 
            }
            auto parallel_data_ptr = parallel_data_t->ptr;
            if (parallel_data_ptr == nullptr) {
                return;
            }
            new_task_data_ptr->par_region_data_ptr = parallel_data_ptr;
        }
        return;
    }        
}


void
on_ompt_callback_implicit_task(
    ompt_scope_endpoint_t endpoint,
    ompt_data_t *parallel_data,
    ompt_data_t *task_data,
    unsigned int team_size,
    unsigned int thread_num)
{

    if (endpoint == ompt_scope_begin) {
//        KA_TRACE(0, STDERR, 0, "on_ompt_callback_implicit_task","begin team_size: %d  thread_num:% d", team_size, thread_num);
        if (team_size == 1) return; // serialized parallel region, ignore
        int parent_task_type;    
        ompt_data_t parent_data;
        ompt_data_t* parent_data_t = &parent_data;
        ompt_data_t** parent_data_tt = &parent_data_t;

        ompt_data_t* thread_data_t = ompt_get_thread_data();
        if (thread_data_t == NULL) {
            KA_TRACE(0, STDERR, 0, "on_ompt_callback_implicit_task","ompt_get_thread_data() returns null", 0);
        } else {
            auto thread_data_ptr = static_cast<ThreadData*>(thread_data_t->ptr);  
            if (thread_data_ptr->stack_top_addr == nullptr && thread_data_ptr->stack_base_addr == nullptr) {
                int s;
                pthread_attr_t attr;
                s = pthread_getattr_np(pthread_self(), &attr); 
                if (s != 0) {
                   KA_TRACE(0, STDERR, 0, "on_ompt_callback_implicit_task","pthread_getattr_np() returns error", 0);
                }      
                size_t stacksize;
                void* stackaddr;       
                s = pthread_attr_getstack(&attr, &stackaddr, &stacksize);  
                if (s != 0) {
                   KA_TRACE(0, STDERR, 0, "on_ompt_callback_implicit_task","pthread_attr_getstack() returns null", 0);
                } 
                thread_data_ptr->stack_base_addr = stackaddr;
                thread_data_ptr->stack_top_addr = (void*)((uint64_t)stackaddr + (uint64_t)stacksize);
            } 
        }
        if (ompt_get_task_info(1, &parent_task_type, parent_data_tt, NULL, NULL, NULL)) { // fetching the parent task info
            auto task_data_ptr = static_cast<TaskData*>(parent_data_t->ptr);
            LabelSegmentByte16* extended_segment = new LabelSegmentByte16();  
            SetLabelType(extended_segment->v, IMPLICIT);
            SetOffsetSpan(extended_segment->v, thread_num, team_size);    

            LabelCreateInfo create_info;
            create_info.num_seg_no_copy = 0;
            create_info.extended_segment = static_cast<void*>(extended_segment);
            create_info.to_copy = task_data_ptr->label;

            auto new_task_label = CreateLabelAfterFork(create_info);
            auto new_task_data_ptr = new TaskData(); // the memory allocated for the task to hold the shared pointer to the label 
            //TODO: now the memory is leaking. It would be preferrable that task_data->ptr is deallocated.
            new_task_data_ptr->label = std::move(new_task_label);        
            //new_task_data_ptr->parent_label = task_data_ptr->label; //just store a pointer to the parent label will be much easier for looking up. 
//#define DEBUG_LABEL_CREATE
            task_data->ptr = static_cast<void*>(new_task_data_ptr);
            /*
            assert(parallel_data->ptr);
            auto par_reg_ptr = static_cast<ParRegionData*>(parallel_data->ptr);
            auto lock_ptr = &(par_reg_ptr->par_lock);
            mcs_node_t me;
            mcs_lock(lock_ptr, &me); 
            par_reg_ptr->implicit_tasks.push_back(new_task_data_ptr);
            KA_TRACE(1000, STDOUT, 0, "DEBUG","implicit task callback test, push back task pointer %p %d", parallel_data->ptr, ((ParRegionData*)(parallel_data->ptr))->implicit_tasks.size());
            mcs_unlock(lock_ptr, &me);
            */
        } else {
            assert(false);
        }
    } else { // ompt_scope_end 
        // implicit task is ending, modify the parent task's label. And only one child task should tell the parent to update otherwise race will be 
        // generated.
        auto ptr = task_data->ptr;   
        if (!ptr) {
            return;
        } 
        auto task_data_ptr = static_cast<TaskData*>(ptr);  
        if (thread_num != 0) {//only thread 0 does modification of parent label.
//#define DEBUG_TASK_DATA_DELETION
            delete task_data_ptr;        
            task_data_ptr = nullptr;
            return; 
        }
        auto current_label = task_data_ptr->label;
        int parent_task_type;    
        ompt_data_t parent_data;
        ompt_data_t* parent_data_t = &parent_data;
        ompt_data_t** parent_data_tt = &parent_data_t;
        if (ompt_get_task_info(1, &parent_task_type, parent_data_tt, NULL, NULL, NULL)) {
            auto parent_task_data_ptr = static_cast<TaskData*>(parent_data_t->ptr);
            auto parent_label = parent_task_data_ptr->label;
            if (parent_label == nullptr) {
                return;
            }
            parent_task_data_ptr->label = std::move(ModifyParentLabelImplicitTaskEnd(parent_label, current_label));
        } else {
        }
        delete task_data_ptr;        
        task_data_ptr = nullptr;
    }
} 

/* 
 * return value: 
 *     0b00  : left->right;( hist -> cur) 
 *     0b01  : parallel
 *     0b10  : same
 *     0b11  : right->left;  ( cur -> hist)
 *     0b100 : error
 */
static Direction
HappensBeforeImplicitOnly(LabelPtr& from, LabelPtr& to) 
{
   // pure offset-span labeling 
    auto from_head = from->GetHead();
    auto to_head = to->GetHead();
    auto from_ptr = from_head->next;
    auto to_ptr = to_head->next;       
    while (from_ptr && to_ptr && SameSegment(from_ptr, to_ptr)) {
        from_ptr = from->GetNext(from_ptr);
        to_ptr = to->GetNext(to_ptr);
    }
    if (from_ptr == nullptr && to_ptr != nullptr) {
        return LEFT_TO_RIGHT;
        //return (char)0;
    } 
    if (to_ptr == nullptr && from_ptr != nullptr) {
        return RIGHT_TO_LEFT;
        //return (char)3;
    }
    if (from_ptr == nullptr && to_ptr == nullptr) { 
        return SAME;
        //return (char)2;     
    }
    // both from_ptr and to_ptr are not void 
    auto from_seg16_ptr = SEG_CAST_16(from_ptr);
    auto to_seg16_ptr = SEG_CAST_16(to_ptr);
    int from_offset, from_span;
    int to_offset, to_span;
    GetOffsetSpan(from_seg16_ptr->v, from_offset, from_span);
    GetOffsetSpan(to_seg16_ptr->v, to_offset, to_span);
    assert(from_span == to_span);
    /*
    if (from_span != to_span) {
        KA_TRACE(1000, STDOUT, 0, "HappensBeforeImplicitOnly", "from: %s to: %s", from->ToString().c_str(), to->ToString().c_str());  
    }
    */
    auto span = from_span;
    if ((from_offset % span) == (to_offset % span)) {
        if (from_offset < to_offset) {
           // return (char)0;
            return LEFT_TO_RIGHT;
        } else if (from_offset > to_offset) {

 //           return (char)3;
            return RIGHT_TO_LEFT;
        } else {
           // return (char)4;             
           return ERROR;
        }
    }
#ifdef DEBUG_HB
#endif
    return PARALLEL;
}


static inline Direction
CompareTaskGroupLabel(TaskGroupPtr& s_shorter, TaskGroupPtr& s_longer)
{
    auto len_shorter = s_shorter->label[0];
    auto len_longer = s_longer->label[0];
//    int iter_shorter = 1;
//    int iter_longer = 1;  
    int i = 1;
    while (i <= len_shorter && s_shorter->label[i] == s_longer->label[i]) {
        i++;
    }
    if (i == len_shorter) {
        KA_TRACE(0, STDERR, 0, "CompareTaskGroupLabel", "unxpected case", 0);  
        assert(false);
    }
    if (s_shorter->label[i] < s_longer->label[i]) {
        return SHORTER_TO_LONGER;
    } else {
        return LONGER_TO_SHORTER;
    }
    /*
    while (s_shorter->label[iter_shorter] == s_longer->label[iter_longer]) {
        iter_shorter++; 
        iter_longer++; 
    } 
    if (s_shorter->label[iter_shorter] < s_longer->label[iter_longer]) {
        return SHORTER_TO_LONGER;
    } else {
        return LONGER_TO_SHORTER;
    }
    */
}

static inline bool
IsTaskGroupPrefix(TaskGroupPtr& s_shorter, TaskGroupPtr& s_longer)
{    
    int len_shorter = s_shorter->label[0];
    int len_longer = s_longer->label[0];
    for (int i = 1; i <= len_shorter; ++i) {
        if (s_shorter->label[i] != s_longer->label[i]) 
            return false;
    }
    return true;
}

// premise is that the taskgroup labels of two segments are not the same, and the 
// happens-before relation could be inferred by comparing the taskgroup label. 
static inline Direction
TaskGroupOrder(TaskGroupPtr& from_tg, TaskGroupPtr& to_tg) 
{
    assert(from_tg != nullptr && to_tg != nullptr);  
    bool from_is_short = false;
    TaskGroupPtr s_longer = nullptr;
    TaskGroupPtr s_shorter = nullptr;
    assert(from_tg->label[0] != 0 && to_tg->label[0] != 0);
    if (from_tg->label[0] < to_tg->label[0]) {
        s_longer = to_tg;
        s_shorter = from_tg;
        from_is_short = true;
    } else {
        s_longer = from_tg;
        s_shorter = to_tg;
    }
    if (IsTaskGroupPrefix(s_shorter, s_longer)) {  // contains the void situation 
        // one task group is inside another taskgroup, so whichever one has smaller age count happpens before the other
        if (from_tg->age_count < to_tg->age_count) {
            return LEFT_TO_RIGHT;
        } else if (from_tg->age_count > to_tg->age_count) {
            return RIGHT_TO_LEFT;
        } else {
            KA_TRACE(0, STDERR, 0, "TakGroupOrder", "unexpected case 0", 0);  
            assert(false);         
        }
    } else {
        auto direction = CompareTaskGroupLabel(s_shorter, s_longer);
        if (direction == SHORTER_TO_LONGER) {
            if (from_is_short) {
                return LEFT_TO_RIGHT;
            } else {
                return RIGHT_TO_LEFT;
            }
        } else {
            if (from_is_short) {
                return RIGHT_TO_LEFT;
            } else {
                return LEFT_TO_RIGHT;             
            }
        }   
    }
    KA_TRACE(0, STDERR, 0, "TakGroupOrder", "unexpected case 1", 0);  
    assert(false);
    return ERROR;
}

//helper function for check00
static inline Direction
Check00Helper(LabelSegmentByte16* from, LabelSegmentByte16* to)
{
    auto from_taskwait = GetTaskWait(from->v);     
    auto to_taskwait = GetTaskWait(to->v);
    auto from_taskcreate = GetTaskCreate(from->v);    
    auto to_taskcreate = GetTaskCreate(to->v);  
    auto from_loopcnt = GetLoopCnt(from->v);
    auto to_loopcnt = GetLoopCnt(to->v);   
    assert(!(from_taskwait == to_taskwait && from_taskcreate == to_taskcreate 
             && from_loopcnt == to_loopcnt));
    if (from_taskwait <= to_taskwait && from_taskcreate <= to_taskcreate && 
        from_loopcnt <= to_loopcnt) {
        return LEFT_TO_RIGHT; 
    } else if (from_taskwait >= to_taskwait && from_taskcreate >= to_taskcreate && 
        from_loopcnt >= to_loopcnt) {
        return RIGHT_TO_LEFT;
    } else {
        KA_TRACE(0, STDERR, 0, "check00", "unexpected case 0", 0);  
        assert(false);
    }                 
    assert(false);
    return ERROR;
}

// Check00 checks when both labels are the same implicit task.
static inline Direction
Check00(void* from_seg, void* to_seg, int from_offset, int to_offset)
{
    auto from_seg16_ptr = SEG_CAST_16(from_seg);         
    auto to_seg16_ptr = SEG_CAST_16(to_seg);
    auto from_seg_type = GetSegmentType(from_seg16_ptr->v);    
    auto to_seg_type = GetSegmentType(to_seg16_ptr->v); 

    if (from_offset < to_offset) { // Barrier is implied
        return LEFT_TO_RIGHT; 
    } else if (from_offset > to_offset) {
        return RIGHT_TO_LEFT; 
    } else { // from_offset == to_offset
        auto from_tg = GetTaskGroupPtr(from_seg);  
        auto to_tg = GetTaskGroupPtr(to_seg);  
        if (from_tg == nullptr && to_tg == nullptr) {
            // this means that no taskgroup is actually involved, should be the most common case
            // just use the taskwait, taskcreate and loop cnt to do the judgemnt 
            // keep in mind that from_seg and to_seg field are not totally the same.
            return Check00Helper(from_seg16_ptr, to_seg16_ptr);            
        }  else if (SameTaskGroup(from_tg, to_tg)) {
            // this means both tasks are in the taskgroup area and are in the same taskgroup level  
            // only the label is the same. the age count might be differnt 
            // if the age count differ, it means that there exists inner taskgroup. And the larger age count implies later. If age count is the same, check taskwait, taskcreate, looopcnt, at least one of them must be different, otherwise all the fields would be the same. 
            auto from_age = from_tg->age_count;   
            auto to_age = to_tg->age_count; 
            if (from_age == to_age) {
                return Check00Helper(from_seg16_ptr, to_seg16_ptr);            
            }      
            if (from_age < to_age) {
                return LEFT_TO_RIGHT;
            } 
            if (from_age > to_age) {
                return RIGHT_TO_LEFT;
            }
        } else { // The task group label is not the same | either one is nullptr
            if (from_tg == nullptr) {
                return LEFT_TO_RIGHT;
            }  
            if (to_tg == nullptr) {
                return RIGHT_TO_LEFT;
            }
            if (IsVoidTaskGroupLabel(from_tg) || IsVoidTaskGroupLabel(to_tg)) {
            // if either one of the task group label is void    
                auto from_age = from_tg->age_count;   
                auto to_age = to_tg->age_count; 
                if (from_age == to_age) {
                    return Check00Helper(from_seg16_ptr, to_seg16_ptr);            
                }      
                if (from_age < to_age) {
                    return LEFT_TO_RIGHT;
                } 
                if (from_age > to_age) {
                    return RIGHT_TO_LEFT;
                }
            }
            return TaskGroupOrder(from_tg, to_tg);      
        }
    }
    KA_TRACE(0, STDERR, 0, "check00", "unexpected case 1", 0);  
    assert(false);
    return ERROR;
}

static inline Direction
Check01(LabelSegmentByte16* from_seg, LabelSegmentByte16* to_seg)
{   
    // the from_seg and to_seg are the two segments that first differ 
//    KN_TRACE(0, STDERR, 0, "Check01", "called", 0); 
    int from_offset, to_offset;
    int from_span, to_span;

    GetOffsetSpan(from_seg->v, from_offset, from_span);
    GetOffsetSpan(to_seg->v, to_offset, to_span);
    if (from_offset < to_offset) {
        return LEFT_TO_RIGHT;
    }  else if (from_offset > to_offset) {
        return RIGHT_TO_LEFT; 
    } else {
        KA_TRACE(0, STDERR, 0, "check01", "unexpected case 0", 0);  
        assert(false);
    }
    assert(false);
    return ERROR;
}

static inline void
CollectSinkNodes(TaskDependence* dependence, stack<void*>& todo)
{
    if (dependence == nullptr)
        return;
    auto sink_node = dependence->input_dependences->next;
    while (sink_node) { // collect all the node in the input dependence list
        todo.push(sink_node->task_data_ptr);
        sink_node = sink_node->next;
    } 
}

static inline bool
FoundDependenceOnVar(void* par_region_data_ptr, void* var_addr, TaskData*& source, TaskData*& dest)
{
    auto par_ptr = static_cast<ParRegionData*>(par_region_data_ptr);
    auto lock_ptr = &(par_ptr->par_reg_lock);
    auto hash = par_ptr->dep_var_hash;
    mcs_node_t me;
    assert(hash.find(var_addr) != hash.end()); // the var_addr should have been recorded in the hash table.
    mcs_lock(lock_ptr, &me);  // now that we need to traverse the dependece graph, better protect it with mutual exclusion 
    bool found = false;
    auto dependence = hash[var_addr]; 
    // now use DFS to search the path. note that source must be in the dependence->output_dependences.
    unordered_map<void*,int> visited;
    visited[source]++;
    stack<void*> todo;  // stores the TaskData* 
    CollectSinkNodes(dependence, todo);  
    while (!todo.empty()) {
        auto node = todo.top(); 
        if (visited[node] > 0) {
            todo.pop();
            continue;
        }
        if (node == dest) { // we found the dst
            found = true; 
            break;
        } // if the current node is not the dest node   
        visited[node]++;
        todo.pop(); 
        auto task_ptr = static_cast<TaskData*>(node);  // now find the adjacent nodes of this node  by looking at its output dep var
        for (auto var : task_ptr->var_output_dep) {
            // each var is the output dep
            auto dep = hash[var];   
            CollectSinkNodes(dep, todo); // put all the adjacent nodes to the stack 
        }
    }
    mcs_unlock(lock_ptr, &me);
    return found;
}


static inline Direction
CheckTaskDependency(TaskData* hist_task_ptr, TaskData* cur_task_ptr)
{
    assert(hist_task_ptr != nullptr && cur_task_ptr!= nullptr); 
    assert(hist_task_ptr->par_region_data_ptr != nullptr && cur_task_ptr->par_region_data_ptr != nullptr && 
           hist_task_ptr->par_region_data_ptr == cur_task_ptr->par_region_data_ptr);
    // first let's try if hist_task_ptr -> cur_task_ptr 
    // this means that hist_task_ptr 's output dep var should not be empty 
    // and we should traverse the in-out edges to see if there exists a path between hist_task_ptr and cur_task_ptr
    // first determine if two explicit tasks are in the same parallle region    
    auto par_data_ptr = hist_task_ptr->par_region_data_ptr;

    if (hist_task_ptr->var_output_dep.size() == 0 && cur_task_ptr->var_output_dep.size() == 0) { // if hist_task_ptr 
        return PARALLEL;
    }
    if (hist_task_ptr->var_output_dep.size() > 0) { // we try to see if hist -> cur 
        for (auto var : hist_task_ptr->var_output_dep) {
            if (FoundDependenceOnVar(par_data_ptr, var, hist_task_ptr, cur_task_ptr)) {
                return LEFT_TO_RIGHT;
            }     
        } 
        // if no found 
    } else {
        for (auto var : cur_task_ptr->var_output_dep) {
            if (FoundDependenceOnVar(par_data_ptr, var, cur_task_ptr, hist_task_ptr)) {
                return ERROR; // that is current task happens before history task, which is very unlinkely, because if the task dependency is enforced, how come an access already recorded coms after the current access
            }
        } 
    }
    return PARALLEL;
}

static inline bool
CheckTaskWaitChain(LabelSegmentByte16* seg)
{
    auto seg_current_ptr = seg;
    auto seg_next_ptr = SEG_CAST_16(seg->next);
    while (seg_next_ptr && GetLabelType(seg_next_ptr->v) == EXPLICIT) { 
        // because explicit task cannot directly fork logical task, so if next segment is not explicit then there must exist one implicit and implicit task
        // implies parallel region which means that inner strucutre must finish
        auto current_seg_tg_ptr = GetTaskGroupPtr((void*)seg_current_ptr);
        if (current_seg_tg_ptr != nullptr && !IsVoidTaskGroupLabel(current_seg_tg_ptr)) {
            return true; //taskgroup will also guarantee finish
        } 
        // no taskgroup scope is involved check taskwait 
        // check if the taskwaited flag bit is set for next segment,  if it is not set, the taskwait chain is broken
        if (!GetTaskWaitedFlag(seg_next_ptr->v))  // that means the child explicit task is not taskwaited
            return false;    
        seg_current_ptr = seg_next_ptr;
        seg_next_ptr = SEG_CAST_16(seg_next_ptr->next);
    }
    return true;
}

static inline Direction
CheckTaskWait(LabelSegmentByte16* from_seg, LabelSegmentByte16* to_seg)
{
    // The calling context of this function is: 
    // a. from_seg and to_seg are the first different segments in each labels, 
    // b. TaskOf(from_seg) and TaskOf(to_seg) is the same implicit/explicit task, but at different stages 
    // This function is called when one of the following cases is true 
    // 1. from_seg->next is explicit task and to_seg->next is explicit task. And taskgroup information is not able to judge the relation
    // 2. from_seg->next is implicit task and to_seg->next is explicit task. And taskgroup information is not able to judge the relation, and the from_seg->next is created after the creation of to_seg->next. If no takwait inserted in between, parallel 
    // 3. from_seg->next is null, and to_seg->next is explicit task. And taskgroup information is not able to judge the relation
    //    , and TaskOf(to_seg->next) is created first, we need to check if this explicit task is waited 
    // 4. from_seg->next is logical task and to_seg->next is explicit task. And taskgroup information is not able to judge the relation , and TaskOf(to_seg->next) is created first , we need to check if this explicit task is waited 
    //
    // So generally we need to check if the chain of tasks whose taskwait count is smaller is taskwaited

    auto lw_from = true;
    LabelSegmentByte16*  l_lw = nullptr;
    LabelSegmentByte16*  l_sw = nullptr;      
    auto from_taskwait_cnt = GetTaskWait(from_seg->v);
    auto to_taskwait_cnt = GetTaskWait(to_seg->v);         
    if (from_taskwait_cnt == to_taskwait_cnt) {//no taskwait parallel
        return PARALLEL;
    } else if (from_taskwait_cnt > to_taskwait_cnt) {
        l_lw = from_seg; // label with larger taskwait 
        l_sw = to_seg;   // label with smaller taskwait     
    } else {
        l_lw = to_seg;
        l_sw = from_seg;  
        lw_from = false;
    }  

    //auto from_next_ptr = static_cast<LabelSegmentByte16*>(from_seg->next);  
    auto to_next_ptr = static_cast<LabelSegmentByte16*>(to_seg->next);
    LabelSegmentByte16* tw_seg = nullptr;                  
    if (to_next_ptr == nullptr) {
        KA_TRACE(0, STDERR, 0, "CheckTaskWait", "unexpected case 0-0", 0);  
        assert(false);
    }
    //if (from_next_ptr == nullptr) { // case 3
    //    KA_TRACE(0, STDERR, 0, "CheckTaskWait", "case 3 ", 0);  
        /*
        if (to_next_ptr == nullptr) {
            KA_TRACE(0, STDERR, 0, "CheckTaskWait", "unexpected case 0-1", 0);  
            assert(false);
        }
        */
        /*
        auto to_next_task_type = GetLabelType(to_next_ptr->v);
        if (to_next_task_type != EXPLICIT) {
            KA_TRACE(0, STDERR, 0, "CheckTaskWait", "unexpected case 0-2", 0);  
            assert(false);
        }
        */
    /*
        if (from_taskwait_cnt < to_taskwait_cnt) {
            KA_TRACE(0, STDERR, 0, "CheckTaskWait", "unexpected case 0-3", 0);  
            assert(false);
        }   
        */
        // check the taskwait chain, because now we know the explicit task is first created and there exists at least one taskwait clause 
     //   tw_seg = SEG_CAST_16(to_next_ptr);
   // }  
    tw_seg = SEG_CAST_16(l_sw->next);
    /*
    auto from_next_task_type = GetLabelType(from_next_ptr->v);
    auto to_next_task_type = GetLabelType(to_next_ptr->v);  
    if (from_next_task_type == EXPLICIT && to_next_task_type == EXPLICIT) { // case 1
        KA_TRACE(0, STDERR, 0, "CheckTaskWait", "case 1 ", 0);  
        tw_seg = SEG_CAST_16(l_sw->next); // check the taskwait chain for the one with smaller taskwait count 
    } else if (from_next_task_type == IMPLICIT && to_next_task_type == EXPLICIT) { // case 2
        KA_TRACE(0, STDERR, 0, "CheckTaskWait", "case 2 ", 0);  
        if (l_sw != to_seg) { // the smaller taskwait should be related with the explicit task 
            KA_TRACE(0, STDERR, 0, "CheckTaskWait", "unexpected case 1-0", 0);  
            assert(false);
        } 
        tw_seg = SEG_CAST_16(l_sw->next); 
    } else if (from_next_task_type == LOGICAL && to_next_task_type == EXPLICIT) { // case 4
        KA_TRACE(0, STDERR, 0, "CheckTaskWait", "case 4 ", 0);  
        if (l_sw != to_seg) {
            KA_TRACE(0, STDERR, 0, "CheckTaskWait", "unexpected case 1-1", 0);  
            assert(false);
        } 
        tw_seg = SEG_CAST_16(l_sw->next);
    } else {
        KA_TRACE(0, STDERR, 0, "CheckTaskWait", "unexpected case 2-0", 0);  
        assert(false);
    }
    */
    if (CheckTaskWaitChain(tw_seg)) {
        if (!lw_from)  // the larger taskait is to 
            return LEFT_TO_RIGHT;
        return RIGHT_TO_LEFT;
    } 
    return PARALLEL;  
    //auto from_next_task_type = GetLabelType(from_next_ptr->v);
    //auto to_next_task_type = GetLabelType(to_next_ptr->v);       
    // if the label with smaller taskwait is last segment
    /*
    if (l_sw->next == nullptr) { // 
        KA_TRACE(0, STDERR, 0, "CheckTaskWait", "unexpected case 0", 0);  
        assert(false); 
    }
    auto l_sw_next_ptr = static_cast<LabelSegmentByte16*>(l_sw->next);            
    auto l_sw_next_task_type = GetLabelType(l_sw_next_ptr->v);         
    LabelSegmentByte16* tw_seg = nullptr;                  
    if (l_sw_next_task_type == IMPLICIT) {//in this case, a parallel region is created and finished before the taskwait clause
        if (lw_from) { // label with larger is from 
            return RIGHT_TO_LEFT;
        } else {
            return LEFT_TO_RIGHT;
        }
    } else if (l_sw_next_task_type == EXPLICIT) {
        tw_seg = SEG_CAST_16(l_sw->next);
    } else if (l_sw_next_task_type == LOGICAL) { // next one is logical
        if (l_sw_next_ptr->next == nullptr ||
            GetLabelType(SEG_CAST_16(l_sw_next_ptr->next)) == IMPLICIT) {
            // next of next one is implicit or nullptr, that ok
            if (!lw_from) return LEFT_TO_RIGHT;
            return RIGHT_TO_LEFT; 
        } 
        // then next of next is explicit , do more check
        tw_seg = SEG_CAST_16(l_sw_next_ptr->next);         
    }
    if (CheckTaskWaitChain(tw_seg)) {
        if (!lw_from) return LEFT_TO_RIGHT;    
        return RIGHT_TO_LEFT;
    }
    */
}

/*
static inline Direction
CheckTaskGroup(LabelSegmentByte16* from_seg, LabelSegmentByte16* to_seg)
{
    //regarding the same taskgroup test, it is done on the common parent task   
    auto from_tg = GetTaskGroupPtr(from_seg);
    auto to_tg = GetTaskGroupPtr(to_seg);       
    if (from_tg == nullptr && to_tg == nullptr) {
        // no taskgroup construct recorded        
        return CheckTaskWait(from_seg, to_seg); 
    }
    if (SameTaskGroup(from_tg, to_tg)) { // both not null 
        return CheckTaskWait(from_seg, to_seg);
    } else if (from_tg != nullptr && to_tg != nullptr) { // not in the same taskgroup , both not null 
        return TaskGroupOrder(from_tg, to_tg);                              
    } else if (from_tg == nullptr) { // to_tg is not null, this implies that from -> to,otherwise the from_tg should be null 
        KA_TRACE(0, STDERR, 0, "CheckTaskGroup", "from_tg is null and to_tg is not null", 0);  
        
    }
}  
*/

// This is the checking routine for both next segments being explicit task type
static inline Direction
Check02(LabelSegmentByte16* from_seg, LabelSegmentByte16* to_seg)
{
    // because they have same parent task, and they are two explicit tasks, taskgroup/taskwait is the sync. rel. that needs to be checked.
    //KN_TRACE(0, STDERR, 0, "Check02", "called", 0); 
    auto from_tg = GetTaskGroupPtr(from_seg);
    auto to_tg = GetTaskGroupPtr(to_seg);
    if (from_tg == nullptr && to_tg == nullptr) {
        return CheckTaskWait(from_seg, to_seg); 
    } else if (from_tg != nullptr && to_tg != nullptr) {
        if (SameTaskGroup(from_tg, to_tg)) {
            return CheckTaskWait(from_seg, to_seg);
        } else {
           if (IsVoidTaskGroupLabel(from_tg)) {
                auto from_task_create = GetTaskCreate(from_seg->v);    
                auto to_task_create = GetTaskCreate(to_seg->v);                   
                if (from_task_create < to_task_create) { // the TaskOf(from_seg->next) is created first
                    return CheckTaskWait(from_seg, to_seg);
                } else {
                    return RIGHT_TO_LEFT;
                }
           } else if (IsVoidTaskGroupLabel(to_tg)) {
                auto from_task_create = GetTaskCreate(from_seg->v);    
                auto to_task_create = GetTaskCreate(to_seg->v);                   
                if (from_task_create < to_task_create) { // the TaskOf(from_seg->next) is created first
                    return LEFT_TO_RIGHT;
                } else {
                    return CheckTaskWait(from_seg, to_seg);
                }
           } else {
                return TaskGroupOrder(from_tg, to_tg);
           }
        }
    } else if (from_tg == nullptr && to_tg != nullptr) {
        if (IsVoidTaskGroupLabel(to_tg)) {
            return CheckTaskWait(from_seg,to_seg);
        }
        auto from_task_create = GetTaskCreate(from_seg->v);    
        auto to_task_create = GetTaskCreate(to_seg->v);                   
        if (from_task_create < to_task_create) { // the TaskOf(from_seg->next) is created first
            return CheckTaskWait(from_seg, to_seg);
        } else {
            return RIGHT_TO_LEFT;
        }
    } else {
        if (IsVoidTaskGroupLabel(from_tg)) {
            return CheckTaskWait(from_seg, to_seg);
        } 
        auto from_task_create = GetTaskCreate(from_seg->v);    
        auto to_task_create = GetTaskCreate(to_seg->v);                   
        if (from_task_create < to_task_create) { // the TaskOf(from_seg->next) is created first
            return LEFT_TO_RIGHT;
        } else {
            return CheckTaskWait(from_seg, to_seg);
        }
    }
    assert(false);
    return ERROR;
}

static inline Direction
Check03(LabelSegmentByte16* from_seg, LabelSegmentByte16* to_seg)
{
 // This is the case for from_seg->next being implicit task label segment 
 // and to_seg->next being explicit task label segment
//    KN_TRACE(0, STDERR, 0, "Check03", "called", 0); 
    int to_offset, to_span;   
    int from_offset, from_span;
    GetOffsetSpan(from_seg->v, from_offset, from_span);
    GetOffsetSpan(to_seg->v, to_offset, to_span);
    if (from_offset < to_offset) {//the implicit task is in a parallel region that precedes the creation of explicit task.
        return LEFT_TO_RIGHT;  
    } else { // otherwise the explicit task is created before the parallel region that contains the implicit task.
        auto from_tg = GetTaskGroupPtr(from_seg);
        auto to_tg = GetTaskGroupPtr(to_seg);
        if (from_tg == nullptr && to_tg == nullptr) {
            return CheckTaskWait(from_seg, to_seg);
        } else if (SameTaskGroup(from_tg, to_tg)) { // both not null and same (include both void situation) 
            return CheckTaskWait(from_seg, to_seg);
        } else if (from_tg != nullptr && to_tg != nullptr) { // to -> from
            if (IsVoidTaskGroupLabel(to_tg)) { // to is void, from is not void 
                return CheckTaskWait(from_seg, to_seg);
            }
            return RIGHT_TO_LEFT; 
        } else if (to_tg == nullptr) { // from_tg != nullptr
            return CheckTaskWait(from_seg, to_seg);    
        } else { // from_tg = nullptr, to_tg != nullptr
            assert(false); // this case is impossible 
        }
    }
}

static inline Direction
Check04(LabelSegmentByte16* from_seg, LabelSegmentByte16* to_seg)
{
    // This is the case for from_seg->next being nullptr and to_seg->next being explicit task
    // The idea is to check the taskcreate counter reading      
//    KN_TRACE(0, STDERR, 0, "Check04", "called", 0); 
    auto from_task_create = GetTaskCreate(from_seg->v);    
    auto to_task_create = GetTaskCreate(to_seg->v);                   
    if (from_task_create <= to_task_create) { // the explicit task is created after the TaskOf(from_seg) 
        return LEFT_TO_RIGHT;
    } else { // the explicit task is created first, further check the taskgroup 
        auto from_tg = GetTaskGroupPtr(from_seg);
        auto to_tg = GetTaskGroupPtr(to_seg);
        if (from_seg == nullptr && to_seg == nullptr) {
            return CheckTaskWait(from_seg, to_seg);
        } else if (SameTaskGroup(from_tg, to_tg)) {
            return CheckTaskWait(from_seg, to_seg);
        } else if (from_tg != nullptr && to_tg != nullptr) {
            if (IsVoidTaskGroupLabel(to_tg)) {
                return CheckTaskWait(from_seg, to_seg);
            }
            return RIGHT_TO_LEFT; //  
        } else if (to_tg == nullptr) {
            return CheckTaskWait(from_seg, to_seg);
        } else {
            assert(false);
        }
    }
}

static inline Direction
Check05(LabelSegmentByte16* from_seg, LabelSegmentByte16* to_seg)
{
    // This is the case for from_seg->next being nullptr and to_seg->next being implicit task
//    KN_TRACE(0, STDERR, 0, "Check05", "called", 0); 
    int from_offset, to_offset;
    int from_span, to_span;
    GetOffsetSpan(from_seg->v, from_offset, from_span);
    GetOffsetSpan(to_seg->v, to_offset, to_span);  
    if (from_offset <= to_offset) {
        return LEFT_TO_RIGHT;
    } else {
        return RIGHT_TO_LEFT;
    } 
}

static inline Direction
Check06(LabelSegmentByte16* from_seg, LabelSegmentByte16* to_seg)
{
    // This is the case for from_seg->next being nullptr and to_seg->next being logical task 
/*   
    int from_offset, to_offset;
    int from_span, to_span;
    GetOffsetSpan(from_seg->v, from_offset, from_span);
    GetOffsetSpan(to_seg->v, to_offset, to_span);    
    if (from_offset < to_offset) { // we know that the from_seg and to_seg has at least one parallel region in between
        return LEFT_TO_RIGHT;
    }
    */
//    KN_TRACE(0, STDERR, 0, "Check06", "called", 0); 
    auto from_loop_cnt = GetLoopCnt(from_seg->v);        
    auto to_loop_cnt = GetLoopCnt(to_seg->v);
//    KN_TRACE(0, STDERR, 0, "check06", "from lc %d to lc %d", from_loop_cnt, to_loop_cnt);
    if (from_loop_cnt <= to_loop_cnt) { // the workshare loop in to_seg is after the from_seg
        return LEFT_TO_RIGHT;
    }   // otherwise, it implies that nowait clause is on the first workshare loop and it forms race condition even if logical iteration is mapped on same thread  
 //   KN_TRACE(0, STDERR, 0, "check06", "from lc %d to lc %d parallel!",from_loop_cnt, to_loop_cnt );
   // now that from_offset == to_offset and from_loop_cnt > to_loop_cnt            
   // this means that the workshare loop in to_seg is created before the from_seg
    /*
    auto to_next_ptr = SEG_CAST_16(to_seg->next);
    auto to_next_next_ptr = SEG_CAST_16(to_next_ptr->next);
    if (to_next_next_ptr == nullptr || 
        GetLabelType(to_next_next_ptr->v) == IMPLICIT) {  
        // if the next segment of to_seg->next (which is a logical segment) is nullptr or implicit task label segment
        //return RIGHT_TO_LEFT;   
        return PARALLEL;   
    }        
    // then check the    
    auto to_seg_tg_ptr = GetTaskGroupPtr((void*)to_seg);
    if (to_seg_tg_ptr != nullptr && !IsVoidTaskGroupLabel(to_seg_tg_ptr)) {
        return RIGHT_TO_LEFT ; //taskgroup will guarantee finish
    } 
    // if no taskgroup info, check the taskwait chain
    if (CheckTaskWaitChain(to_next_ptr)) { // check the taskwait chain staring from the next of next
        return RIGHT_TO_LEFT;  
    }        
    */
    return PARALLEL;
}

static inline int
ExitRank(int phase)
{
    if (phase % 2 == 0) return phase;
    else return phase - 1;
    //return phase - phase % 2; // if it is 
}

static inline int
EnterRank(int phase)
{
    if (phase % 2 == 0) return phase;
    return phase  + 1;
    //return phase + phase % 2;
}

static inline bool
InFinish(void* seg)
{
//    KN_TRACE(1000, STDERR, 0, "in finish", "0", 0);
    //This routine checks if the remaining segments are in a finish closure   
    auto seg_16_ptr = SEG_CAST_16(seg);    
    if (seg_16_ptr->next == nullptr ||
        GetLabelType(SEG_CAST_16(seg_16_ptr->next)->v) == IMPLICIT) {
        return true;
    }
    auto seg_tg_ptr = GetTaskGroupPtr(seg);
    if (seg_tg_ptr != nullptr && !IsVoidTaskGroupLabel(seg_tg_ptr)) {
        return true; // we have taskgroup 
    }       
    // Then check taskwait chain  
    if (CheckTaskWaitChain(SEG_CAST_16(seg))) {
        return true;
    }  
    return false;
}



static inline Direction
CheckOrderedSection(void* from_seg_next, void* to_seg_next)
{
    // here from_seg_next and to_seg_next are both logical label segments.
    bool left_is_from = true;    
    auto from_iter = GetIterValue(from_seg_next);
    auto to_iter = GetIterValue(to_seg_next); 
    if (from_iter == 0 && to_iter == 0) { // iter holder 
        return PARALLEL; 
    }
    assert(from_iter != to_iter); // assert that the two logical tasks are different
    void* s_left = nullptr;
    void* s_right = nullptr;
    if (from_iter < to_iter) {
        s_left = from_seg_next;
        s_right = to_seg_next; 
    }else {
        s_left = to_seg_next;
        s_right = from_seg_next;    
        left_is_from = false;
    }  
    auto s_left_phase = GetPhase(SEG_CAST_16(s_left)->v);
    auto s_right_phase = GetPhase(SEG_CAST_16(s_right)->v);                
    /*
    if (from_iter == 0 && to_iter == 0 && s_left_phase == 0 && s_right_phase == 0) { //quick rule out the no ordered section case
        return PARALLEL;
    }
    */
    //KA_TRACE(0, STDERR, 0, "CheckOrderedSection", "from_iter: %d to_iter: %d s_left_phase: %d s_right_phase: %d", from_iter, to_iter, s_left_phase, s_right_phase);  
    if (ExitRank(s_left_phase) < EnterRank(s_right_phase)) {
        if (InFinish(s_left) && InFinish(s_right)) {
            return left_is_from? LEFT_TO_RIGHT: RIGHT_TO_LEFT; 
        }
    }   
    return PARALLEL;
}
    
static inline Direction
Check07(LabelSegmentByte16* from_seg, LabelSegmentByte16* to_seg)
{
//    KN_TRACE(0, STDERR, 0, "Check07", "called", 0); 
    //This is the case for from_seg->next and to_seg->next being both logical task 
    auto from_loop_cnt = GetLoopCnt(from_seg->v);
    auto to_loop_cnt = GetLoopCnt(to_seg->v);      
    assert(from_loop_cnt != to_loop_cnt); 
    /*
    if (from_loop_cnt == to_loop_cnt) {
        return CheckOrderedSection(from_seg->next, to_seg->next);        
    } 
    */
    return PARALLEL;
    /*
    else if (from_loop_cnt > to_loop_cnt) { // the two logical tasks are not in the same workshare loop
        l_s = (void*)to_seg; 
        l_l = (void*)from_seg;  
    } else {
        l_s = (void*)from_seg;
        l_l = (void*)to_seg;   
        loop_cnt_smaller_is_from_seg = true;    
    } 
    if (InFinish(SEG_CAST_16(l_s)->next)) { // the pointer passed is the logical label segment 
        return loop_cnt_smaller_is_from_seg? LEFT_TO_RIGHT : RIGHT_TO_LEFT;   
    }
    // if that logical label segment is not in finish structure  
    // finally we want to check the taskgroup structure  
    auto l_s_tg = GetTaskGroupPtr(l_s);
    auto l_l_tg = GetTaskGroupPtr(l_l);    
    if (l_s_tg == nullptr || IsVoidTaskGroupLabel(l_s_tg)) {
        return PARALLEL;
    } 
    if (l_l_tg != nullptr) {
        if (SameTaskGroup(l_s_tg, l_l_tg)) {
            return PARALLEL;
        } else if (IsVoidTaskGroupLabel(l_l_tg)) {
            return loop_cnt_smaller_is_from_seg ? LEFT_TO_RIGHT : RIGHT_TO_LEFT;
        } else {
            return TaskGroupOrder(l_s_tg, l_l_tg);
        }
    } else { // l_l_tg == nullptr
        return loop_cnt_smaller_is_from_seg ? LEFT_TO_RIGHT : RIGHT_TO_LEFT;
    }
    */
/*   
    if (l_s_tg == nullptr) 
        return PARALLEL;
    if (l_l_tg != nullptr)  {
        if (SameTaskGroup(l_s_tg, l_l_tg)) { // if two logical segment are in the same taskgroup level, taskgroup sync does not work with them
            return PARALLEL; 
        } else if (IsVoidTaskGroupLabel(l_s_tg) == true) {// if l_s_tg is void 
            return PARALLEL;
        } else if (IsVoidTaskGroupLabel(l_l_tg) == true) {// l_l_tg is void, l_s_tg is not void
            return loop_cnt_smaller_is_from_seg ? LEFT_TO_RIGHT : RIGHT_TO_LEFT;
        } else {
            return TaskGroupOrder(l_s_tg, l_l_tg);
        }
    } else { //l_s_tg != nullptr && l_l_tg == nullptr
    // in this case , the taskgroup construct containing the l_s segment would make the synchronization
        if (IsVoidTaskGroupLabel(l_s_tg) == true) {
            return PARALLEL;
        }  
        return loop_cnt_smaller_is_from_seg ? LEFT_TO_RIGHT : RIGHT_TO_LEFT;
    }
    */
}

static inline Direction
Check08(LabelSegmentByte16* from_seg, LabelSegmentByte16* to_seg)
{
    //This is the case when from_seg->next is logical label segment, to_seg->next is implicit label segment
//    KN_TRACE(0, STDERR, 0, "Check08", "called", 0); 
    /*
    int from_offset, from_span;
    int to_offset, to_span;
    GetOffsetSpan(from_seg->v, from_offset, from_span);
    GetOffsetSpan(to_seg->v, to_offset, to_span);
    if (to_offset < from_offset) { // it means that the parallel region containing implicit task TaskOf(to_seg->next) is before the logical task 
        return RIGHT_TO_LEFT;
    }
    */
    return PARALLEL;
    // otherwise , we need to check the tsakwait chain and taskgroup info
   /*
    if (InFinish(from_seg->next)) { 
        return LEFT_TO_RIGHT;    
    } else { // the from_seg->next is not in the finish strucutre then we last check if from->next contains taskgroup 
        auto from_tg = GetTaskGroupPtr((void*)from_seg);
        if (from_tg != nullptr && !IsVoidTaskGroupLabel(from_tg)) {
            return LEFT_TO_RIGHT;
        } else {
            return PARALLEL;
        }
    }
    assert(false);
    return ERROR; // should ever reach here
    */
}

static inline Direction
Check09(LabelSegmentByte16* from_seg, LabelSegmentByte16* to_seg)
{
   // This is the case when from_seg->next is logical task and to_seg->next is explicit task
   // First check the taskcreate count
//    KN_TRACE(0, STDERR, 0, "Check09", "called", 0); 
    //auto from_task_create = GetTaskCreate(from_seg->v);        
   // auto to_task_create = GetTaskCreate(to_seg->v); 
   // if (from_task_create <= to_task_create) { // This means that the explicit task segment is created after the logical segment being created      
        return PARALLEL;  
        // then first check if from_seg->next (logical task label segment) is in a finish structure
         /*
        if (InFinish(from_seg->next)) {
            return LEFT_TO_RIGHT;   
        } else { // from_seg->next is not in the finish structure
            // check from_seg if it has taskgroup structure 
            auto from_tg_ptr = GetTaskGroupPtr(from_seg);
            if (from_tg_ptr != nullptr && !IsVoidTaskGroupLabel(from_tg_ptr)) {
                return LEFT_TO_RIGHT; // in this case, the taskgroup structure at from_seg does the sync 
            } else {
                return PARALLEL; 
            }      
        }
        */
        /*
    } else { // this means that explicit task is created before the logical task, then invoke checktaskgroup function
       //return CheckTaskGroup(from_seg, to_seg); 
        auto from_tg = GetTaskGroupPtr(from_seg);
        auto to_tg = GetTaskGroupPtr(to_seg);
        if (from_seg == nullptr && to_seg == nullptr) {
            return CheckTaskWait(from_seg, to_seg);
        } else if (SameTaskGroup(from_tg, to_tg)) {
            return CheckTaskWait(from_seg, to_seg);
        } else if (from_tg != nullptr && to_tg != nullptr) {
            return RIGHT_TO_LEFT; 
        } else if (to_tg == nullptr) {
            return CheckTaskWait(from_seg, to_seg);
        } else {
            assert(false);
        }
    }
    assert(false); // should never reach here
    return ERROR;
    */
}

static inline Direction
Check10(LabelSegmentByte16* from_seg, LabelSegmentByte16* to_seg)
{
    //This is the case when both from_seg and to_seg are implicit task label segments, and they are not the same implicit task   
    //First thing to be clear is that these two implicit tasks could not be in differnet parallel regions becuase otherwiese the previous sergments already should have differed.  
    //Then what we need to do is to check if next segment of these two implicit task label segments are logical segments. If they are both logical segments, we further check if they are in the same workshare loop, if they are in the same workshare loop, check if ordered section relation exists. Otherwise, there must be nowait clause worksahre loop    
//    KN_TRACE(0, STDERR, 0, "Check10", "called", 0); 
    auto from_next_ptr = SEG_CAST_16(from_seg->next);
    auto to_next_ptr = SEG_CAST_16(to_seg->next);      

    if (from_next_ptr == nullptr || to_next_ptr  == nullptr) {
        return PARALLEL; // 
    }

    if (GetLabelType(from_next_ptr->v) == LOGICAL && GetLabelType(to_next_ptr->v) == LOGICAL) { 
        // if next label segments are both logical and they have the same loop cnt, we further check the ordered section 
        // becuase if a workshare has *no* nowait, implicit barrier is implied, then the offset,span v      
//        KN_TRACE(0, STDERR, 0, "Check10", "both logical " ,0); 
        auto from_loop_cnt = GetLoopCnt(from_seg->v);
        auto to_loop_cnt = GetLoopCnt(to_seg->v);   
        if (from_loop_cnt == to_loop_cnt) {
           
            return CheckOrderedSection(from_next_ptr, to_next_ptr);    
        } else {
            return PARALLEL;
        }
    }
    return PARALLEL;
}
   
static inline Direction
Check11(LabelSegmentByte16* from_seg, LabelSegmentByte16* to_seg)
{
//    KN_TRACE(0, STDERR, 0, "Check11", "called", 0); 
   //both from_seg and to_seg are logical label segment   
    auto from_loop_cnt = GetLoopCnt(from_seg->v);
    auto to_loop_cnt = GetLoopCnt(to_seg->v);    
    assert(from_loop_cnt == to_loop_cnt);  
    return CheckOrderedSection(from_seg, to_seg);
}

inline char
CompileProtocolCase(LabelSegmentByte16* from_next_seg_ptr, LabelSegmentByte16* to_next_seg_ptr)
{
    //premise is that not both segments are null 
    char ret = 0;
    if (from_next_seg_ptr != nullptr && to_next_seg_ptr != nullptr) {
        auto from_next_tsk_type = GetLabelType(from_next_seg_ptr->v);
        auto to_next_tsk_type = GetLabelType(to_next_seg_ptr->v);
        // use bit manipulation to avoid unnecessary branching
        // 3*3=9 combinations, use 4 bits  to encode (just concatenate from and to values)
        // from(2bit)-to(2bit)
        ret |= ((char)from_next_tsk_type) << 2;
        ret |= (char)to_next_tsk_type;        
    } else if (from_next_seg_ptr == nullptr) {
        ret |= 0x10; // setting the fifth pos to indicate that next of from seg is nullptr
        ret |= GetLabelType(to_next_seg_ptr->v);
    } else { // to_next_seg16_ptr is nullptr
        ret |= 0x20; // setting the sixth pos to indicate taht next of to seg is nullptr
        ret |= GetLabelType(from_next_seg_ptr->v);      
    }
    return ret;
}

static bool
SameReductionRegion(void*& from, void*& to)
{
    //check the two task labels to determine if they are in the same reduction region  
    //The criteria for being in the same reduction region is that they are in the same inner most parallel region       
    // from and to are the two label segments that are first different. This is fetched from happens-before. 
    auto from_seg16_ptr = SEG_CAST_16(from);          
    auto to_seg16_ptr = SEG_CAST_16(to);
    auto from_next_ptr = from_seg16_ptr->next;
    auto to_next_ptr = to_seg16_ptr->next;    
    auto from_task_type = GetLabelType(from_seg16_ptr->v);                     
    auto to_task_type = GetLabelType(to_seg16_ptr->v);
    if (from_task_type == IMPLICIT && to_task_type == IMPLICIT) {
        int to_offset, to_span;
        int from_offset, from_span;
        GetOffsetSpan(from_seg16_ptr->v, from_offset, from_span);
        GetOffsetSpan(to_seg16_ptr->v, to_offset, to_span);
        if (from_offset % from_span != to_offset % to_span) {
            if (from_next_ptr == nullptr && to_next_ptr == nullptr) {
                return true; // at the inner most implicit task parallel region 
            } else if (from_next_ptr != nullptr && to_next_ptr != nullptr) { // both next are not nullptr 
                auto from_next_tsk_type = GetLabelType(SEG_CAST_16(from_next_ptr)->v); 
                auto to_next_tsk_type = GetLabelType(SEG_CAST_16(to_next_ptr)->v);
                if (from_next_tsk_type == LOGICAL && to_next_tsk_type == LOGICAL && 
                    SEG_CAST_16(from_next_ptr)->next == nullptr && SEG_CAST_16(to_next_ptr)->next == nullptr &&
                    GetLoopCnt(SEG_CAST_16(from_next_ptr)->v) == GetLoopCnt(SEG_CAST_16(to_next_ptr)->v)) {
                    return true;
                } 
            } else if (from_next_ptr == nullptr && to_next_ptr != nullptr) {
                auto to_next_tsk_type = GetLabelType(SEG_CAST_16(to_next_ptr)->v);   
                if (to_next_tsk_type == LOGICAL && SEG_CAST_16(to_next_ptr)->next == nullptr && 
                    HasIterHolder(SEG_CAST_16(to_next_ptr)->v)) { // the logical segment place holder does not mean it is in the iteration yet, it is just for convenicence of label management
                    return true; 
                }
            } else if (from_next_ptr != nullptr && to_next_ptr == nullptr) {
                auto from_next_tsk_type = GetLabelType(SEG_CAST_16(from_next_ptr)->v);   
                if (from_next_tsk_type == LOGICAL && SEG_CAST_16(from_next_ptr)->next == nullptr && 
                    HasIterHolder(SEG_CAST_16(from_next_ptr)->v)) {
                    return true; 
                }
            }
        }
    } else if (from_task_type == LOGICAL && to_task_type == LOGICAL && from_next_ptr == nullptr && to_next_ptr == nullptr
            && GetLoopCnt(from_seg16_ptr->v) == GetLoopCnt(to_seg16_ptr->v)) {
        return true;
    } 
    return false;
}


// This is the complete happens before relation judging procedure (can be reduced to reachability solver)
static int 
HappensBefore(LabelPtr& from, LabelPtr& to, TaskData* hist_task_ptr, TaskData* cur_task_ptr, //bool& logical_mapped_to_same_task, 
              void*& from_seg_diff, void*& to_seg_diff
#ifdef DEBUG_EXPLICIT_TASK
        , int task_type    
#endif
        ) 
{
    //KN_TRACE(0, STDERR, 0, "Happensbefore", "from%s to%s" , from->ToString().c_str(), to->ToString().c_str()); 
    //logical_mapped_to_same_task = false;
    // first compare the labels until finding the different ones  
    auto from_head = from->GetHead();
    auto to_head = to->GetHead();
    auto from_ptr = from_head->next;
    auto to_ptr = to_head->next;       
    while (from_ptr && to_ptr && SameSegment(from_ptr, to_ptr)) { 
        from_ptr = from->GetNext(from_ptr);//keep rolling if segments are the same
        to_ptr = to->GetNext(to_ptr);
    }
    if (from_ptr == nullptr && to_ptr != nullptr) { // from label is prefix of to label
        return LEFT_TO_RIGHT;
    } 
    if (to_ptr == nullptr && from_ptr != nullptr) { // to label is prefix of from label
        return RIGHT_TO_LEFT;
    }
    if (from_ptr == nullptr && to_ptr == nullptr) { // same label
        return SAME;
    }
    // both from_ptr and to_ptr are not void 
    // and their fields are not the same    
    auto from_seg16_ptr = SEG_CAST_16(from_ptr);
    auto to_seg16_ptr = SEG_CAST_16(to_ptr);
    from_seg_diff = from_ptr;
    to_seg_diff = to_ptr;
    //Get the task type:
    auto from_task_type = GetLabelType(from_seg16_ptr->v);                     
    auto to_task_type = GetLabelType(to_seg16_ptr->v);
    int from_offset, from_span;
    int result = VOID;
    assert(from_task_type == to_task_type);
    int to_offset, to_span;   
    GetOffsetSpan(from_seg16_ptr->v, from_offset, from_span);
    GetOffsetSpan(to_seg16_ptr->v, to_offset, to_span);
    assert(from_span == to_span); // if both implicit task, should be in the same parallel region, if both explicit/logical task, should both be 1 
    if (from_offset % from_span == to_offset % to_span) { // then they are the same task.(either implicit or explicit) 
        if (from_task_type == LOGICAL) {
            auto from_iter = GetIterValue(from_seg16_ptr);
            auto to_iter = GetIterValue(to_seg16_ptr); 
            if (from_iter != to_iter) { // if not the same logical task, assert that they are in the same work-sharing loop
                result = Check11(from_seg16_ptr, to_seg16_ptr);
            } 
        }   
        if (result == VOID) {  
            auto from_next_ptr = from_seg16_ptr->next;
            auto to_next_ptr = to_seg16_ptr->next;  
            if (from_next_ptr == nullptr && to_next_ptr == nullptr) { // The from and to seg are the last 
                result = Check00(from_ptr, to_ptr, from_offset, to_offset); // check rest of the fields in this segment and consecutive segments if necessary
            } else if (from_next_ptr != nullptr && to_next_ptr != nullptr) { //both segments has next segment
                // both segment has successors 
                auto from_next_seg16_ptr = SEG_CAST_16(from_next_ptr);                      
                auto to_next_seg16_ptr = SEG_CAST_16(to_next_ptr);
                auto cond_code = CompileProtocolCase(from_next_seg16_ptr, to_next_seg16_ptr);
                switch(cond_code) {
                    PROTOCOL_CASE(IMP_IMP, Check01, from_seg16_ptr, to_seg16_ptr);   
                    PROTOCOL_CASE(IMP_EXP, Check03, from_seg16_ptr, to_seg16_ptr);      
                    PROTOCOL_CASE_REVERSE(IMP_LOG, Check08, from_seg16_ptr, to_seg16_ptr);
                    PROTOCOL_CASE_REVERSE(EXP_IMP, Check03, from_seg16_ptr, to_seg16_ptr);
                    PROTOCOL_CASE(EXP_EXP, Check02, from_seg16_ptr, to_seg16_ptr); 
                    PROTOCOL_CASE_REVERSE(EXP_LOG, Check09, from_seg16_ptr, to_seg16_ptr);
                    PROTOCOL_CASE(LOG_IMP, Check08, from_seg16_ptr, to_seg16_ptr);
                    PROTOCOL_CASE(LOG_EXP, Check09, from_seg16_ptr, to_seg16_ptr);                                          
                    PROTOCOL_CASE(LOG_LOG, Check07, from_seg16_ptr, to_seg16_ptr); 
                    default:
                        KA_TRACE(0, STDERR, 0, "HappensBefore", "unexpected case 0", 0);  
                        assert(false);
                }
            } else if (from_next_ptr != nullptr && to_next_ptr == nullptr) {// to seg does not have next segment
                auto from_next_seg16_ptr = SEG_CAST_16(from_next_ptr);    
                auto cond_code = CompileProtocolCase(from_next_seg16_ptr, nullptr);
                switch(cond_code) {
                    PROTOCOL_CASE_REVERSE(EXP_NULL, Check04, from_seg16_ptr, to_seg16_ptr);
                    PROTOCOL_CASE_REVERSE(IMP_NULL, Check05, from_seg16_ptr, to_seg16_ptr);
                    PROTOCOL_CASE_REVERSE(LOG_NULL, Check06, from_seg16_ptr, to_seg16_ptr);
                    default:
                        KA_TRACE(0, STDERR, 0, "HappensBefore", "unexpected case 1", 0);  
                        assert(false);
                }
            } else {//from_next_ptr == nullptr && to_next_ptr != nullptr
                auto to_next_seg16_ptr = SEG_CAST_16(to_next_ptr);           
                auto cond_code = CompileProtocolCase(nullptr, to_next_seg16_ptr); 
                switch(cond_code) {
                    PROTOCOL_CASE(NULL_EXP, Check04, from_seg16_ptr, to_seg16_ptr); 
                    PROTOCOL_CASE(NULL_IMP, Check05, from_seg16_ptr, to_seg16_ptr);
                    PROTOCOL_CASE(NULL_LOG, Check06, from_seg16_ptr, to_seg16_ptr);
                    default:
                        KA_TRACE(0, STDERR, 0, "HappensBefore", "unexpected case 2", 0);  
                        assert(false);
                }
            } 
        }
    } else { // the implicit tasks are not the same task , 
        result = Check10(from_seg16_ptr, to_seg16_ptr);
            //KA_TRACE(0, STDERR, 0, "check10 result", "%s %d", result == PARALLEL?"IS PAR":"NO PAR", result);  
    }
    if (result == PARALLEL && cur_task_ptr->is_explicit && hist_task_ptr->is_explicit &&
             cur_task_ptr->has_dep && hist_task_ptr->has_dep) { // check for additional dependencies only if both tasks are explicit and has dependency 
           KA_TRACE(0, STDERR, 0, "check task dep", "hist %p cur %p", hist_task_ptr, cur_task_ptr);
        result = CheckTaskDependency(hist_task_ptr, cur_task_ptr);
          // KA_TRACE(0, STDERR, 0, "check task dep", "finished hist %p cur %p", hist_task_ptr, cur_task_ptr);
    }
    return result;
}

/* Requires:  index: index into the filename string table; buf: buffer for filename 
 *  Returns:  nothing
 */
void lookupStringTable(int index, char* buf)
{
    uint16_t num_files = 0;
    memcpy(&num_files, gStringTablePtr, sizeof(uint16_t));
    if (index >= num_files) {
        KA_TRACE(0, STDERR, 0, "lookupStringTable", "ERROR: query index %d out of range", index);
        return;
    } 
    uint16_t offset = 0;
    uint16_t filename_length = 0;
    size_t header_offset = (1 + index * sizeof(uint16_t)) * 2;
    memcpy(&offset, (char*)gStringTablePtr + header_offset, sizeof(uint16_t));
    memcpy(&filename_length, (char*)gStringTablePtr + header_offset + 2, sizeof(uint16_t));
    memcpy(buf, (char*)gStringTablePtr + offset, filename_length + 1);    
}


/*
 * Requires: index: index into the linemap 
 *  Returns: the instruction address in uint64_t type 
 *     Note: helper function for lookupLineInformation
 */
inline uint64_t getInstrAddrAt(uint32_t offset)
{
    uint64_t addr = 0;  
    memcpy(&addr, (char*)gLineMapPtr + offset, sizeof(uint64_t));
    return addr;
}

inline uint16_t getFileIndexAt(uint32_t offset)
{
    uint16_t file_index = 0;  
    memcpy(&file_index, (char*)gLineMapPtr + offset + sizeof(uint64_t), sizeof(uint64_t));
    return file_index;
}

inline uint16_t getLineNoAt(uint32_t offset)
{
    uint16_t lineno = 0;  
    memcpy(&lineno, (char*)gLineMapPtr + offset + sizeof(uint64_t) + sizeof(uint16_t), sizeof(uint64_t));
    return lineno;
}

inline uint16_t getColNoAt(uint32_t offset)
{
    uint16_t colno = 0;  
    memcpy(&colno, (char*)gLineMapPtr + offset + sizeof(uint64_t) + sizeof(uint16_t) + sizeof(uint16_t), sizeof(uint64_t));
    return colno;
}

/* Requires: instruction address 
 *  Returns: the corresponding index into the filename string table
 * lookup the .linemap section for the linemap associated with instrucation address: instr_addr
 */
inline uint16_t lookupLineInformation(uint64_t instr_addr, uint16_t& line_no, uint16_t& column_no) 
{
    uint32_t num_records = 0;
    memcpy(&num_records, gLineMapPtr, sizeof(uint32_t));
    uint32_t start = 0;
    uint32_t end = num_records - 1;
    while (start + 1 < end) { // binary search 
        uint32_t mid = start + (end - start) / 2;
        uint32_t mid_offset = sizeof(uint32_t) + mid * gLineMapCellSize;
        uint64_t addr = getInstrAddrAt(mid_offset);
        if (instr_addr == addr) {
            line_no = getLineNoAt(mid_offset);
            column_no = getColNoAt(mid_offset);
            return getFileIndexAt(mid_offset);
        } else if (instr_addr < addr) {
            end = mid;
        } else {
            start = mid; 
        }
    }
    uint32_t start_offset = sizeof(uint32_t) + start * gLineMapCellSize;   
    uint32_t end_offset = sizeof(uint32_t) + end * gLineMapCellSize;
    uint64_t start_addr = getInstrAddrAt(start_offset);
    uint64_t end_addr = getInstrAddrAt(end_offset);
    if (instr_addr >= start_addr && instr_addr < end_addr) {
        line_no = getLineNoAt(start_offset);
        column_no = getColNoAt(start_offset);
        return getFileIndexAt(start_offset);
    } else if (instr_addr == end_addr) {
        line_no = getLineNoAt(end_offset);
        column_no = getColNoAt(end_offset);
        return getFileIndexAt(end_offset);
    }  
    line_no = 0;
    column_no = 0;
    return 0;
}

/* type 1: WW
 * type 2: WR
 * type 3: RW
 */
static void
ReportRaceDynFound(int type, uint64_t instn_addr)
{
    if (global_verbose_output)  {
        uint16_t cur_line_no, cur_column_no, cur_file_index;
        char cur_buf[512];
        cur_file_index = lookupLineInformation(instn_addr, cur_line_no, cur_column_no);
        lookupStringTable(cur_file_index, cur_buf);
        KN_TRACE(0, STDERR, 0, "Race Found", "instn addr: %p type: %d file:%s at line:%d column:%d race" , 
             instn_addr, type, cur_buf, cur_line_no, cur_column_no); 
    }
    global_race_found = true;
}



static void
ReportRace(int type, uint64_t instn_addr, 
         uint64_t  another_instn_addr, void* address) 
{
    if (global_verbose_output) {
        uint16_t cur_line_no, cur_column_no, cur_file_index;
        uint16_t other_line_no, other_column_no, other_file_index;
        char cur_buf[512];
        char other_buf[512];
        cur_file_index = lookupLineInformation(instn_addr, cur_line_no, cur_column_no);
        other_file_index = lookupLineInformation(another_instn_addr, other_line_no, other_column_no);
        lookupStringTable(cur_file_index, cur_buf);
        lookupStringTable(other_file_index, other_buf);
        switch (type) {
            case RW:
                KN_TRACE(0, STDERR, 0, "Race Found(RW)", "data race @memory address: %p -> {Previous Read(instruction address: %p @ file: %s ; line: %d ; column: %d) -  Current Write(instruction address: %p @ file: %s ; line: %d ; column: %d)}\n",
                    address, another_instn_addr, other_buf, other_line_no, other_column_no, instn_addr, cur_buf, cur_line_no, cur_column_no);
                break;
            case WR:
                KN_TRACE(0, STDERR, 0, "Race Found(WR)", "data race @memory address: %p -> {Previous Write(instruction address: %p @ file: %s ; line: %d ; column: %d) -  Current Read(instruction address: %p @ file: %s ; line: %d ; column: %d)}\n",
                    address, another_instn_addr, other_buf, other_line_no, other_column_no, instn_addr, cur_buf, cur_line_no, cur_column_no);
                break;
            case WW:
                KN_TRACE(0, STDERR, 0, "Race Found(WW)", "data race @memory address: %p -> {Previous Write(instruction address: %p @ file: %s ; line: %d ; column: %d) -  Current Write(instruction address: %p @ file: %s ; line: %d ; column: %d)}\n",
                    address, another_instn_addr, other_buf, other_line_no, other_column_no, instn_addr, cur_buf, cur_line_no, cur_column_no);
                break;
            default:
                break;
        }
    }
    global_race_found = true;
}

/*
 * using cache version
static void
ReportRace(int type, uint64_t instn_addr, 
         uint64_t  another_instn_addr, void* address) 
{
    uint16_t cur_line_no, cur_column_no, cur_file_index;
    uint16_t other_line_no, other_column_no, other_file_index;
    bool cur_in_hash = false;
    bool other_in_hash = false;
    char cur_buf[512];
    char other_buf[512];
    pfq_rwlock_read_lock(&gHashLock);
    if (gDebugInfoHash.find(instn_addr) != gDebugInfoHash.end()) {
       cur_in_hash = true; 
    }
    if (gDebugInfoHash.find(another_instn_addr) != gDebugInfoHash.end()) {
       other_in_hash = true;
    }
    pfq_rwlock_read_unlock(&gHashLock);
    pfq_rwlock_node_t me;
    if (cur_in_hash) {
        auto val = gDebugInfoHash[instn_addr];
        cur_file_index = val.first;
        cur_line_no = val.second.first;
        cur_column_no = val.second.second;
    } else {
        cur_file_index = lookupLineInformation(instn_addr, cur_line_no, cur_column_no);
        auto val = make_pair(cur_file_index, make_pair(cur_line_no, cur_column_no));
        pfq_rwlock_write_lock(&gHashLock, &me);
        gDebugInfoHash[instn_addr] = val;
        pfq_rwlock_write_unlock(&gHashLock, &me); 
    }
    if (other_in_hash) {
        auto val = gDebugInfoHash[another_instn_addr];
        other_file_index = val.first;
        other_line_no = val.second.first;
        other_column_no = val.second.second;
    } else  {
        other_file_index = lookupLineInformation(another_instn_addr, other_line_no, other_column_no);
        auto val = make_pair(other_file_index, make_pair(other_line_no, other_column_no));
        pfq_rwlock_write_lock(&gHashLock, &me);
        gDebugInfoHash[another_instn_addr] = val;
        pfq_rwlock_write_unlock(&gHashLock, &me); 
    }
    lookupStringTable(cur_file_index, cur_buf);
    lookupStringTable(other_file_index, other_buf);
    KN_TRACE(0, STDERR, 0, "Race Found", "mem addr:%p instn addr: %p type: %d file:%s at line:%d column:%d races instn addr: %p file:%s at line:%d column:%d" , 
            address, instn_addr, type, cur_buf, cur_line_no, cur_column_no, another_instn_addr, other_buf, other_line_no, other_column_no);
    global_race_found = true;
}
*/

/* Return true if this current record sould be added to the history 
 *
 * Meanwhile, the parameter operation holds the operation to deal with the history:
 * 1. If the memory address is in private state and remains in private state, replace the old one
 * 2. If the memory address is in private state and become shared 
 * */
/*  return code: 
*    0b0000: both lockset are null
 *    0b0100: hist!=null, cur=null
 *    0b0010: hist=null, cur != null
 *    0b0001: has common lock and both cur and hist are not null
 *    0b0011: hist contains cur and both are not null 
 *    0b0101: cur contains hist and both are not null 
 *    0b0111: disjoint and bot are not null 
 */  
inline char
CompileLockSetRelation(LockSetPtr& hist_lockset, LockSetPtr& cur_lockset)
{
    char ret = HIST_NULL_CUR_NULL; 
    if (hist_lockset == nullptr && cur_lockset == nullptr) {
        ret = HIST_NULL_CUR_NULL;
    } else {
        auto num_hist_locks = -1;
        auto num_cur_locks = -1;
        if (hist_lockset) 
            num_hist_locks = hist_lockset->num_locks;
        if (cur_lockset) 
            num_cur_locks = cur_lockset->num_locks;
        if (num_hist_locks <= 0 && num_cur_locks <= 0) { 
            ret = HIST_NULL_CUR_NULL;
        } else if (num_hist_locks > 0 && num_cur_locks <= 0) {
            ret = HIST_NOT_NULL_CUR_NULL;
        } else if (num_hist_locks <= 0 && num_cur_locks > 0) {
            ret = HIST_NULL_CUR_NOT_NULL;
        } else {
            if(Includes(hist_lockset, cur_lockset)) {//both are not null
                ret = HIST_INCLUDES_CUR;
            } else if (Includes(cur_lockset, hist_lockset)) {
                ret = CUR_INCLUDES_HIST;
            } else { 
                bool has_common = false;
                if (num_hist_locks == min(num_hist_locks, num_cur_locks))
                    has_common = HasCommon(cur_lockset, hist_lockset);
                else
                    has_common = HasCommon(hist_lockset, cur_lockset);
                if (has_common)
                    ret = HIST_CUR_HAS_COMMON;
                else
                    ret = DISJOINT;
            }
        }
    }
    return ret;
}

inline char
CompileAccessType(int & hist_type, int & cur_type)
{
    // input is read: 01 write 10:
    // output is a char using 2 lowest bits hist_type|cur_type -- read is 0, write is 1
    char cur_v = (char)((long)cur_type >> 1);
    char ret = (char)((hist_type & 0x2) | cur_v);
    return ret;
}


// Criteria for redution: if the current task is in reduction, and the history task that is chekced against is in the same 'phase', it is not a race
// if the current task is not in reduction, it is simply a race .. 
inline char
CompileConditions(char& direction,  LockSetPtr& hist_lockset, LockSetPtr& cur_lockset, 
                  int hist_type, int cur_type, bool hist_in_reduction, bool cur_in_reduction, // bool logical_mapped_to_same_task
                  bool hist_instn_hw_lock, bool cur_instn_hw_lock,
                   void* from_seg, void* to_seg //from_seg and to_seg are the segments that first differ from HappensBefore
                  
#ifdef DEBUG_REDUCE
            , LabelPtr from_label
            , LabelPtr to_label
#endif
                  )
{
    //  access_type | lockset_type | direct 
    //assert(direction != ERROR); // if error, some thing really bad happened
    auto direct_code = direction;
    auto access_type_code = CompileAccessType(hist_type, cur_type);
    auto lockset_code = CompileLockSetRelation(hist_lockset, cur_lockset);
    char ret = direct_code;
    ret |= access_type_code << 2;
    ret |= lockset_code  << 4; // already takes up 7 bit of 8 bits char
    if (direction == PARALLEL && access_type_code != HIST_READ_CUR_READ &&
        !(lockset_code == HIST_CUR_HAS_COMMON || lockset_code == HIST_INCLUDES_CUR || lockset_code == CUR_INCLUDES_HIST)) {
        //KN_TRACE(0, STDERR, 0, "CompileConditions", "global has reduction %s cur in reduction %s ", global_has_reduction?"yes":"no", cur_in_reduction?"yes":"no");
        if (global_has_reduction && cur_in_reduction) { // we only further check reduction if there is reduction in the program
            //if (SameReductionRegion(from_seg, to_seg)) {
                ret |= 0x80;   // mask the data race report  
            //} 
        } else if (hist_instn_hw_lock && cur_instn_hw_lock) {
            ret |= 0x80;
        }
    }
    return ret;
}

static inline bool 
ReadShadowDataAndCheck(
        AccessHistory* from_me, LabelPtr& label, LockSetPtr& lockset, 
        int& current_access_type, DataSharingAttr& data_sharing, 
        char& next_state, //, vector<void*>& redundant_records
        void* address,
        TaskData* cur_task_ptr
#ifdef DEBUG_EXPLICIT_TASK
        , int task_type
        , void* address
#endif
       , uint64_t instn_addr 
       , bool cur_instn_hw_lock
#ifdef PERFORMANCE_INSPECT
       , int i 
#endif
        ) {     
    assert(from_me->access_records != nullptr); 
    auto current_state = from_me->mem_state;
#ifdef FAST_MODE
    if (RACE_ON_ME(current_state)) { 
        next_state = current_state; // preserve the state that marks this address as racing
        return false;   // do not add shadow 
    }
#endif
    bool current_slot_deallocated = false;
    GET_DATA_UNIT_DEALLOC(current_state, current_slot_deallocated); 
    if (current_slot_deallocated) { // 
        next_state = (char)0;
        delete from_me->access_records; // clear the access records on this data unit (if race condition, should have been found by now)
        from_me->access_records = new RecordsListImp(); // create a new RecordsListImp
        return true; // we should add current to the access history
    }

    auto list = from_me->access_records; // get the list of access records 
     
    next_state = char((char)current_state | ((((char)data_sharing) >> 1) & 0x1)); //  
    if ((next_state & 0x1) == PRIVATE) { // store the latest private one 
        // just delete one the spot 
        void* head = (void*)list->getHead();
        from_me->access_records->deleteNodeAfter(head);
        return true;
    }
    auto skip_current_eligible = false;
    // traverse the linked list 
    auto head = list->getHead();
    if (head->next == nullptr)  // no record in the list
        return true;
    void* cursor = head->next;
    void* prev = (void*)head;
    bool remove_hist = false;  
    bool race_found = false;
#ifdef PERFORMANCE_INSPECT 
    int access_record_count = 0;
#endif
    while (cursor) { // for a specific address
#ifdef PERFORMANCE_INSPECT
        access_record_count += 1;
#endif
        remove_hist = false;
        auto prefix_ptr = (Prefix*)cursor;
        auto hist_label = prefix_ptr->label;

        auto another_instn_addr = (uint64_t)(prefix_ptr->instn_addr);
        LockSetPtr hist_lockset = nullptr;
        auto hist_task_ptr = static_cast<TaskData*>(prefix_ptr->task_ptr);
        if (GetAccessRecordType(prefix_ptr->access_type)) { // byte48
            auto ar48_ptr = static_cast<AR48*>(cursor);
            hist_lockset = ar48_ptr->lockset;
            /*
#ifdef PERFORMANCE_INSPECT
        KA_TRACE(0, STDOUT, 0, "PT", "accessing addr: %p slot: %d id: %d ar: %s", address, i, access_record_count, ar48_ptr->ToString().c_str());
#endif
*/
        }
            /*
        else {
#ifdef PERFORMANCE_INSPECT
        KA_TRACE(0, STDOUT, 0, "PT", "accessing addr: %p slot: %d id: %d ar: %s", address, i, access_record_count, (static_cast<AR32*>(cursor))->ToString().c_str());
#endif
        }    
        */
        auto hist_access_type = GetMemoryAccessType(prefix_ptr->access_type);
        auto cur_in_reduction = cur_task_ptr->in_reduction;
        auto hist_in_reduction = InReduction(prefix_ptr->access_type);
        auto hist_instn_hw_lock = HasLockPrefix(prefix_ptr->access_type); 

        //auto hist_is_explicit_thread_private = IsExpTaskThreadPrivate(prefix_ptr->access_type);
        //auto hist_is_explicit_task_completed = static_cast<TaskData*>(prefix_ptr->task_ptr)->is_completed; 
        //auto hist_is_explicit_task_suspended = static_cast<TaskData*>(prefix_ptr->task_ptr)->suspended;
        /*
        KA_TRACE(1000, STDERR, 0, "ReadShadowDataAndCheck", "addr: %p hist is explicit thread private: %s hist task explicit completed or suspended(%p): %s ", address, hist_is_explicit_thread_private? "true" : "false", 
                 prefix_ptr->task_ptr, (hist_is_explicit_task_completed) ? "true" : "false" );
                 */
        //bool logical_mapped_to_same_task = false;
        void* from_seg = nullptr;
        void* to_seg = nullptr;
        auto direction = (char)HappensBefore(hist_label, label, hist_task_ptr, cur_task_ptr,// logical_mapped_to_same_task, 
                from_seg, to_seg
#ifdef DEBUG_EXPLICIT_TASK
                ,task_type
#endif
                );
        auto cond_code = CompileConditions(direction,  hist_lockset, lockset, hist_access_type, current_access_type, 
                                                       hist_in_reduction, cur_in_reduction,  //logical_mapped_to_same_task, 
                                                       hist_instn_hw_lock, cur_instn_hw_lock,
                                                       from_seg, to_seg
#ifdef DEBUG_REDUCE
            ,hist_label, label
#endif
            );
        if (direction == SAME) {
           // KA_TRACE(0, STDERR, 0, "PERF ", "cond code: %x address: %p", cond_code, address);
            switch(cond_code) {
                CASE_NO_ADD_CURRENT_AND_RETURN(HIST_E_CUR_E_RR_SAME);     
                CASE_NO_ADD_CURRENT_AND_RETURN(HIST_E_CUR_E_WW_SAME);
                CASE_NO_ADD_CURRENT_AND_RETURN(HIST_E_CUR_E_WR_SAME);
                CASE_DEL_HIST(HIST_E_CUR_E_RW_SAME); // delete history

                CASE_NO_ADD_CURRENT_AND_RETURN(CUR_CNTN_HIST_RR_SAME);                    
                CASE_NO_ADD_CURRENT_AND_RETURN(CUR_CNTN_HIST_WW_SAME);                             
                CASE_NO_ADD_CURRENT_AND_RETURN(CUR_CNTN_HIST_WR_SAME); 
            // CUR_CNTN_HIST_RW_SAME: this case, we should add current to access history while preserve history record

                CASE_NO_ADD_CURRENT_AND_RETURN(HIST_E_CUR_NE_RR_SAME);
                CASE_NO_ADD_CURRENT_AND_RETURN(HIST_E_CUR_NE_WW_SAME);
                CASE_NO_ADD_CURRENT_AND_RETURN(HIST_E_CUR_NE_WR_SAME);    
            //HIST_E_CUR_NE_RW_SAME: this case, we should add current to access history while preserve history record  
            
                CASE_DEL_HIST(HIST_CNTN_CUR_WW_SAME); 
                CASE_DEL_HIST(HIST_CNTN_CUR_RR_SAME);
                CASE_DEL_HIST(HIST_CNTN_CUR_RW_SAME);
            // HIST_CNTN_CUR_WR_SAME: this case, we should add curernt to access history while preserve history record 

                CASE_DEL_HIST(HIST_NE_CUR_E_RR_SAME);
                CASE_DEL_HIST(HIST_NE_CUR_E_RW_SAME);
                CASE_DEL_HIST(HIST_NE_CUR_E_WW_SAME);   
            // HIST_NE_CUR_E_WR_SAME: this case, we should add current to access history while preserve histroy record 
            }
        }
        switch(cond_code) {
            // report race
            CASE_REPORT_RACE(HIST_E_CUR_E_RW_PARALLEL, RW,  instn_addr, another_instn_addr, address);
            CASE_REPORT_RACE(HIST_E_CUR_E_WR_PARALLEL, RW,  instn_addr, another_instn_addr, address);
            CASE_REPORT_RACE(HIST_E_CUR_E_WW_PARALLEL, WW,  instn_addr, another_instn_addr, address);
            CASE_REPORT_RACE(HIST_NE_CUR_E_RW_PARALLEL, RW, instn_addr, another_instn_addr, address);
            CASE_REPORT_RACE(HIST_NE_CUR_E_WR_PARALLEL, WR, instn_addr, another_instn_addr, address);    
            CASE_REPORT_RACE(HIST_NE_CUR_E_WW_PARALLEL, WW, instn_addr, another_instn_addr, address);
            CASE_REPORT_RACE(HIST_E_CUR_NE_RW_PARALLEL, RW, instn_addr, another_instn_addr, address);
            CASE_REPORT_RACE(HIST_E_CUR_NE_WR_PARALLEL, WR, instn_addr, another_instn_addr, address);
            CASE_REPORT_RACE(HIST_E_CUR_NE_WW_PARALLEL, WW, instn_addr, another_instn_addr, address);
            CASE_REPORT_RACE(DISJOINT_RW_PARALLEL, RW,      instn_addr, another_instn_addr, address);
            CASE_REPORT_RACE(DISJOINT_WR_PARALLEL, WR,      instn_addr, another_instn_addr, address);
            CASE_REPORT_RACE(DISJOINT_WW_PARALLEL, WW,      instn_addr, another_instn_addr, address);
            default:
               cond_code &= 0x7f; // erase the highest bit, reduction is orthogonal feature, don't confuse with lockset 
               break; 
        }
        /*
        if (race_found) {
            KA_TRACE(0, STDERR, 0, "ReadShadowDataAndCheck", "hist label: %s cur label: %s",  hist_label->ToString().c_str(), label->ToString().c_str());
        }
        */
        if (!race_found) { // race not found, then consider the following conditions
        switch(cond_code) {
            // discard hist 
            CASE_DEL_HIST(HIST_E_CUR_E_RR_HIST_HB_CUR);
            CASE_DEL_HIST(HIST_E_CUR_E_RW_HIST_HB_CUR);
            CASE_DEL_HIST(HIST_E_CUR_E_WW_HIST_HB_CUR); 
            CASE_DEL_HIST(HIST_NE_CUR_E_RR_HIST_HB_CUR);    
            CASE_DEL_HIST(HIST_NE_CUR_E_RW_HIST_HB_CUR);
            CASE_DEL_HIST(HIST_NE_CUR_E_WW_HIST_HB_CUR); 
          //  CASE_DEL_HIST(HIST_E_CUR_NE_RW_SAME);    // ?? why this case is qualified for delete hist 
            CASE_DEL_HIST(HIST_CNTN_CUR_RR_HIST_HB_CUR);
            CASE_DEL_HIST(HIST_CNTN_CUR_RW_HIST_HB_CUR); 
            CASE_DEL_HIST(HIST_CNTN_CUR_WW_HIST_HB_CUR); 
            // These cases are cur happens before hist, 
            // if any happens, something wrong with my protocol is happening ... ...
            CASE_WARNING(HIST_E_CUR_E_RR_CUR_HB_HIST, hist_label, label); // 0x03
            CASE_WARNING(HIST_E_CUR_E_RW_CUR_HB_HIST, hist_label, label); // 0x07
            CASE_WARNING(HIST_E_CUR_E_WR_CUR_HB_HIST, hist_label, label); // 0x0b
            CASE_WARNING(HIST_NE_CUR_E_RR_CUR_HB_HIST, hist_label, label); //0x23
            CASE_WARNING(HIST_NE_CUR_E_RW_CUR_HB_HIST, hist_label, label); //0x43
            CASE_WARNING(HIST_NE_CUR_E_WR_CUR_HB_HIST, hist_label, label); //0x47
            CASE_WARNING(HIST_NE_CUR_E_WW_CUR_HB_HIST, hist_label, label); //0x4b
            CASE_WARNING(HIST_E_CUR_NE_RR_CUR_HB_HIST, hist_label, label); //0x4f
            CASE_WARNING(HIST_E_CUR_NE_RW_CUR_HB_HIST, hist_label, label); //0x23
            CASE_WARNING(HIST_E_CUR_NE_WR_CUR_HB_HIST, hist_label, label); //0x
            CASE_WARNING(HIST_E_CUR_NE_WW_CUR_HB_HIST, hist_label, label);
            CASE_WARNING(COMMON_LOCK_RR_CUR_HB_HIST, hist_label, label);
            CASE_WARNING(COMMON_LOCK_RW_CUR_HB_HIST, hist_label, label);
            CASE_WARNING(COMMON_LOCK_WR_CUR_HB_HIST, hist_label, label);
            CASE_WARNING(COMMON_LOCK_WW_CUR_HB_HIST, hist_label, label);
            CASE_WARNING(HIST_CNTN_CUR_RR_CUR_HB_HIST, hist_label, label);
            CASE_WARNING(HIST_CNTN_CUR_RW_CUR_HB_HIST, hist_label, label);
            CASE_WARNING(HIST_CNTN_CUR_WR_CUR_HB_HIST, hist_label, label);
            CASE_WARNING(HIST_CNTN_CUR_WW_CUR_HB_HIST, hist_label, label);
            CASE_WARNING(CUR_CNTN_HIST_RR_CUR_HB_HIST, hist_label, label);
            CASE_WARNING(CUR_CNTN_HIST_RW_CUR_HB_HIST, hist_label, label);
            CASE_WARNING(CUR_CNTN_HIST_WR_CUR_HB_HIST, hist_label, label);
            CASE_WARNING(CUR_CNTN_HIST_WW_CUR_HB_HIST, hist_label, label);
            CASE_WARNING(DISJOINT_RR_CUR_HB_HIST, hist_label, label);
            CASE_WARNING(DISJOINT_RW_CUR_HB_HIST, hist_label, label);
            CASE_WARNING(DISJOINT_WR_CUR_HB_HIST, hist_label, label);
            CASE_WARNING(DISJOINT_WW_CUR_HB_HIST, hist_label, label);
            default:
               break; 
        }  
        }
#ifdef FAST_MODE
        if (race_found) { // clean up this access record and break
            //from_me->access_records.reset(); // delete this access record // mutual exclusion guarantees only deleted once
            delete from_me->access_records;
            next_state = SET_RACE_ON_ME(next_state); // toggle the higest bit
            return false;
        }  
#endif
        if (remove_hist) { // remove the node pointed to by cursor

            cursor = ((Prefix*)cursor)->next; 
            from_me->access_records->deleteNodeAfter(prev);
        } else {
            prev = cursor;
            cursor = ((Prefix*)cursor)->next;
        }
    } 
#ifdef PERFORMANCE_INSPECT
    mcs_node_t me;
    mcs_lock(&gHistLock, &me);
    gHist[(uint64_t)access_record_count]++;
    //KA_TRACE(0, STDOUT, 0, "PERF STAT", "addr: %p access record count: %d\n", address, access_record_count);
    mcs_unlock(&gHistLock, &me);
#endif
    return !skip_current_eligible;
}

static inline void
WriteShadowMemory(AccessHistory* update_me, void* new_record_node, char next_state, RecordsListImp* access_records, int data_unit_accessed) // vector<void*>& to_delete) 
{
    SET_NUM_DATA_UNIT_ACCESSED(update_me->mem_state, data_unit_accessed); // set the number of 
    if (update_me->access_records == nullptr && access_records != nullptr) {
        update_me->access_records = access_records;
    }
    //assert(update_me != nullptr && update_me->access_records != nullptr);
    update_me->mem_state = next_state; //first update the state               
    if (new_record_node != nullptr) {
        update_me->access_records->pushFront(new_record_node);
//#define DEBUG_SHADOW
#ifdef DEBUG_SHADOW
        if (((Prefix*)new_record_node)->label->GetCnt() == 3) {
            KA_TRACE(0, STDOUT, 0, "ADDED RECORD: ", "%p %s %d\n", update_me, ((Prefix*)new_record_node)->label->ToString().c_str(), update_me->access_records->getNumRecord());
        }
#endif
    }
}


static void
ExecuteCheckProtocol(AccessHistory * from_me, 
                     TaskData* cur_task_data, 
                     LabelPtr& current_label, 
                     LockSetPtr& current_lockset, 
                     int flag,  
                     DataSharingAttr& data_sharing,
                     void* address
#ifdef DEBUG_EXPLICIT_TASK
                    ,int task_type
#endif
                    ,uint64_t instn_addr 
                    ,int data_unit_accessed
                    ,bool hw_lock 
#ifdef PERFORMANCE_INSPECT 
                    ,int i
#endif
       )
{         
    /* About setting the data_unit_accessed in mem_state: if a memory address is associated with a stack access, and it private to a single task, 
     * when the task completes, mark the access_history related with this address as deallocated at TaskSchedule callback. The data_unit_accessed is 
     * needed at that point */
    //KA_TRACE(0, STDOUT, 0, "ExecuteCheckProtocol", "addr: %p label: %s\n", address, current_label->ToString().c_str());
    auto lock_ptr = &(from_me->lock); // switch from mcs_lock to pfq_rwlock
    mcs_node_t me;
    //pfq_rwlock_node_t me;
     
    bool lock_acquired = false;
    //if (flag == READ) return;
    if (!mcs_trylock(lock_ptr, &me)) {
        if (flag == WRITE) {
           ReportRaceDynFound(DYNAMIC_FOUND, instn_addr);
          // somehow should setup the mem_state here   
#ifdef FAST_MODE
           from_me->mem_state |= 0x80; // no worry about race condition, everybody is setting the same one   
#endif
           return; 
        }  
        //KA_TRACE(0, STDOUT, 0, "ExecuteCheckProtocol", "lock contention: instn addr: %p address:%p type: %s\n",(void*)instn_addr, address, (flag == READ)? "read" : "write");
    } else {
        lock_acquired = true;
    }  
   // GET_NUM_DATA_UNIT_ACCESSED(from_me->mem_state, num_data_unit);
    //GET_NUM_DATA_UNIT_ACCESSED(from_me->mem_state, num_data_unit);
    //KA_TRACE(0, STDOUT, 0, "CheckAccess: ", "num_data_unit set to: %d expected: %d", num_data_unit, data_unit_accessed);

    int current_access_type = flag;  // 1 is read, 2 is write
    char next_state;       
    void* new_record_node = nullptr;
    if (!lock_acquired) 
        mcs_lock(lock_ptr, &me); 
      
    bool add_to_shadow = true;
    
    /*
    pfq_rwlock_write_lock(lock_ptr, &me);
    SET_NUM_DATA_UNIT_ACCESSED(from_me->mem_state, data_unit_accessed); // set the number of 
    pfq_rwlock_write_unlock(lock_ptr, &me); 
    */

    RecordsListImp* access_records = nullptr;
    bool access_records_is_null = false;

   // pfq_rwlock_read_lock(lock_ptr);
    if (from_me->access_records == nullptr) {
        access_records_is_null = true;
    }
   // pfq_rwlock_read_unlock(lock_ptr);

    if (access_records_is_null) {
        access_records = new RecordsListImp();
        next_state = (((char)data_sharing) >> 1) & 0x1;  // if is 0x1, shared , if 0x0 private
    } else {
    //    pfq_rwlock_write_lock(lock_ptr, &me);
        add_to_shadow = ReadShadowDataAndCheck(from_me, current_label, current_lockset,
                current_access_type, data_sharing, next_state,  address, cur_task_data
#ifdef DEBUG_EXPLICIT_TASK
                , task_type
#endif
                , instn_addr 
                , hw_lock
#ifdef PERFORMANCE_INSPECT
                ,i
#endif
                ); 
     //   pfq_rwlock_write_unlock(lock_ptr, &me); 
    }  
    new_record_node = nullptr;
    assert(current_label != nullptr);
    if (add_to_shadow) {
        if (current_lockset == nullptr) {
            auto ar = new AR32((char)current_access_type, current_label, (void*)cur_task_data, instn_addr);
            if (cur_task_data->in_reduction) {
                SetInReductionBit(ar->access_type);
            }
            if (hw_lock) {
                SetHasLockPrefixBit(ar->access_type);
            }
            /*
            if (data_sharing == EXPLICIT_TASK_THREAD_PRIVATE || data_sharing == EXPLICIT_TASK_PRIVATE) {
                KA_TRACE(0, STDERR, 0, "CheckAccess: ", "setting exp task thread %p %d", address, data_sharing);
                SetExpTskThreadPrivateBit(ar->access_type);
            }
            */
            new_record_node = static_cast<void*>(ar);
        } else {
            auto ar = new AR48((char)current_access_type, current_label, current_lockset, (void*)cur_task_data, instn_addr); 
            if (cur_task_data->in_reduction) {
                SetInReductionBit(ar->access_type);
            }
            if (hw_lock) {
                SetHasLockPrefixBit(ar->access_type);
            }
            /*
            if (data_sharing == EXPLICIT_TASK_THREAD_PRIVATE || data_sharing == EXPLICIT_TASK_PRIVATE) {
                SetExpTskThreadPrivateBit(ar->access_type);
            }
            */
            new_record_node = static_cast<void*>(ar);
        }
    } 
    //pfq_rwlock_write_lock(lock_ptr, &me);

    WriteShadowMemory(from_me, new_record_node, next_state, access_records, data_unit_accessed);
   // pfq_rwlock_write_unlock(lock_ptr, &me); 
    mcs_unlock(lock_ptr, &me);
    return;
}



extern "C" {

void* processElf(const char* section_name, int fd, uint64_t& section_size) 
{
    if (elf_version(EV_CURRENT) == EV_NONE) {
        KA_TRACE(0, STDERR, 0, "process elf", "elf library initialization failed", 0);
        exit(-1);
    }        
    Elf *e = NULL;
    if ((e = elf_begin(fd, ELF_C_READ, NULL)) == NULL) {
        KA_TRACE(0, STDERR, 0, "process elf", "elf_begin() failed", 0);
        exit(-1);                
    }
    if (elf_kind(e) != ELF_K_ELF) {
        KA_TRACE(0, STDERR, 0, "process elf", "not an elf object", 0);
        exit(-1);                
    }
    size_t shstrndx;
    if (elf_getshdrstrndx(e, &shstrndx) != 0) {
        KA_TRACE(0, STDERR, 0, "process elf", "elf_getshdrstrndx() failed", 0);
        exit(-1);                
    }

    Elf_Scn * scn; 
    Elf_Data* data;
    GElf_Shdr shdr;   
    char* name;
    scn = NULL;
     
    while ((scn = elf_nextscn(e, scn)) != NULL) {
        if (gelf_getshdr(scn, &shdr) != &shdr) {
            KA_TRACE(0, STDERR, 0, "process elf", "getshdr() failed", 0);
            exit(-1);
        }
        if ((name  = elf_strptr(e, shstrndx, shdr.sh_name)) == NULL)  {
            KA_TRACE(0, STDERR, 0, "process elf", "elf_strptr() failed", 0);
            exit(-1);
        }
        if (strcmp(name, section_name) == 0) {
            KA_TRACE(0, STDOUT, 0, "process elf", "found section", 0);
            data = elf_getdata(scn, data);
            if (data == NULL) {
                KA_TRACE(0, STDERR, 0, "process elf", "getdata() failed for section", 0);
                exit(-1);
            }
            section_size = data->d_size;
            KA_TRACE(0, STDOUT, 0, "process elf", "section: %s address: %p size: %u", name, data->d_buf, section_size);
            return data->d_buf;
        } 
    }
    return NULL;
}


#define register_callback_t(name, type)                       \
do{                                                           \
  type f_##name = &on_##name;                                 \
  if (ompt_set_callback(name, (ompt_callback_t)f_##name) ==   \
      ompt_set_never)                                         \
    printf("0: Could not register callback '" #name "'\n");   \
}while(0)

#define register_callback(name) register_callback_t(name, name##_t)

int ompt_initialize(
  ompt_function_lookup_t lookup,
  ompt_data_t *tool_data)
{
  const char* verbose  = getenv("ROMP_VERBOSE");
  if (strstr(verbose, "on") != NULL) {
        global_verbose_output = true;    
  }
  ompt_set_callback = (ompt_set_callback_t) lookup("ompt_set_callback");
  ompt_get_callback = (ompt_get_callback_t) lookup("ompt_get_callback");
  ompt_get_state = (ompt_get_state_t) lookup("ompt_get_state");
  ompt_get_task_info = (ompt_get_task_info_t) lookup("ompt_get_task_info");
  ompt_get_thread_data = (ompt_get_thread_data_t) lookup("ompt_get_thread_data");
  ompt_get_parallel_info = (ompt_get_parallel_info_t) lookup("ompt_get_parallel_info");
  ompt_get_unique_id = (ompt_get_unique_id_t) lookup("ompt_get_unique_id");

  ompt_get_num_places = (ompt_get_num_places_t) lookup("ompt_get_num_places");
  ompt_get_place_proc_ids = (ompt_get_place_proc_ids_t) lookup("ompt_get_place_proc_ids");
  ompt_get_place_num = (ompt_get_place_num_t) lookup("ompt_get_place_num");
  ompt_get_partition_place_nums = (ompt_get_partition_place_nums_t) lookup("ompt_get_partition_place_nums");
  ompt_get_proc_id = (ompt_get_proc_id_t) lookup("ompt_get_proc_id");
  ompt_enumerate_states = (ompt_enumerate_states_t) lookup("ompt_enumerate_states");
  ompt_enumerate_mutex_impls = (ompt_enumerate_mutex_impls_t) lookup("ompt_enumerate_mutex_impls");

  register_callback_t(ompt_callback_mutex_acquired, ompt_callback_mutex_t); 
  register_callback_t(ompt_callback_mutex_released, ompt_callback_mutex_t);
  //register_callback(ompt_callback_nest_lock);
  register_callback(ompt_callback_sync_region); 
  register_callback(ompt_callback_implicit_task); // on_ompt_callback_implicit_task
  register_callback(ompt_callback_work);//on_ompt_callback_work
// register_callback(ompt_callback_master); 
  register_callback(ompt_callback_parallel_begin);//on_ompt_callback_parallel_begin
  register_callback(ompt_callback_parallel_end); // on_ompt_callback_parallel_end
  register_callback(ompt_callback_task_create);//on_ompt_callback_task_create
  register_callback(ompt_callback_task_schedule);// on_ompt_callback_task_schedule
  register_callback(ompt_callback_task_dependences);//on_ompt_task_dependences
 // register_callback(ompt_callback_task_dependence);
  register_callback(ompt_callback_thread_begin);//on_ompt_callback_thread_begin->on_ompt_callback_thread_begin
  register_callback(ompt_callback_thread_end); //on_ompt_callback_thread_end
  register_callback(ompt_callback_dispatch);  
  register_callback_t(ompt_callback_reduction, ompt_callback_sync_region_t);
  printf("0: NULL_POINTER=%p\n", (void*)NULL);
  ompt_initialized = true;      
  return 1; //success
}

void ompt_finalize(ompt_data_t *tool_data)
{
    KA_TRACE(0, STDERR, 0, "ompt finalizer", "called", 0);   
    if (global_race_found) {
        KA_TRACE(0, STDERR, 0, "ompt finalizer", "race found!", 0);   
    }
    else {
        KA_TRACE(0, STDERR, 0, "ompt finalizer", "race not found!", 0);   
    }
#ifdef DEBUG_WORKSHARE_LOOP
    KA_TRACE(0, STDERR, 0, "mpt finalizer", "workshare cnt: %d", workshare_cnt.load());   
#endif

#ifdef DEBUG_ORDERED_SECTION
    KA_TRACE(0, STDERR, 0, "ompt finalizer", "ordered section cnt: %d", ordered_cnt.load());   
#endif

#ifdef DEBUG_REDUCE
    KA_TRACE(0, STDERR, 0, "ompt finalizer", "reduction: %d", reduction_cnt.load());   
#endif

#ifdef PERFORMANCE_INSPECT
    for (auto rec : gHist) {
        printf("access record length: %lu-- %lu times\n", rec.first, rec.second);
    }
#endif
}

ompt_start_tool_result_t* ompt_start_tool(
  unsigned int omp_version,
  const char *runtime_version)
{
    /* finding .linemap and .stringtable section in binary */
    int length = wai_getExecutablePath(NULL, 0, NULL);
    char* path = (char*)malloc(length + 1);
    wai_getExecutablePath(path, length, NULL);
    path[length] = '\0';
    //KA_TRACE(0, STDOUT, 0, "process elf", "executable length: %d %s", length, path);
    int fd;
    if ((fd = open(path, O_RDONLY, 0)) < 0) {
        KA_TRACE(0, STDERR, 0, "process elf", "open %s failed", path);
        //exit(-1);                
    }  
    gStringTablePtr = processElf(".stringtable", fd, gStringTableSizeInByte);   
    if (gStringTablePtr == NULL) {
        KA_TRACE(0, STDERR, 0, "process elf", "get .stringtable data pionter failed", 0);
        //exit(-1);                
    }      
    gLineMapPtr = processElf(".linemap", fd, gLineMapSizeInByte);      
    if (gLineMapPtr == NULL) {
        KA_TRACE(0, STDERR, 0, "process elf", "get .linemap data pionter failed", 0);
        //exit(-1);                
    }   
    //KA_TRACE(0, STDOUT, 0, "process elf", ".stringtable size: %d .linemap size: %d", gStringTableSizeInByte, gLineMapSizeInByte);
    static ompt_start_tool_result_t ompt_start_tool_result = {&ompt_initialize,&ompt_finalize, 0};
    return &ompt_start_tool_result;
}




/* Description: 
 *    The driver function that does check to every byte. The function first get the task id of the current exeucting task. 
 */
void 
CheckAccess(void* address,
            uint32_t bytes_accessed,
            int flag,
            uint64_t instn_addr,
            bool hw_lock
            )
{
    if (!ompt_initialized) {
        return; 
    }   
#ifdef WORD_LEVEL 
    if (bytes_accessed < 4)  // skip checking memroy access smaller than word granularity
        return;
#endif
    
    int task_type;    
    ompt_data_t task_data;
    ompt_data_t* task_data_t = &task_data;
    ompt_data_t** task_data_tt = &task_data_t;
   
    omp_frame_t frame;
    omp_frame_t* frame_t = &frame;
    omp_frame_t** frame_tt = &frame_t;

    if (!ompt_get_task_info(0, &task_type, task_data_tt, frame_tt, NULL, NULL)) { 
        return; 
    }
 
    TaskData* task_data_ptr = nullptr;
    task_data_ptr = static_cast<TaskData*>(task_data_t->ptr);     

    if (task_data_ptr == nullptr) {
        return;
    }

    int team_size;
    ompt_data_t parallel_data;
    ompt_data_t* parallel_data_t = &parallel_data;
    ompt_data_t** parallel_data_tt = &parallel_data_t;
    if (!ompt_get_parallel_info(0, parallel_data_tt, &team_size)) {
        return; 
    }

    /*
    ParRegionData* parallel_data_ptr = nullptr;
    parallel_data_ptr = static_cast<ParRegionData*>(parallel_data_t->ptr);
    if (parallel_data_ptr == nullptr) {
        return;
    }
    */

    LabelPtr current_label = nullptr;
    LockSetPtr current_lockset = nullptr;    
    current_label = task_data_ptr->label;
    current_lockset = task_data_ptr->lockset;        

    /* -------- BEGIN DATA SHARING PROPERTY CHECKING ------------------------------------*/
    ompt_data_t* thread_data_t = ompt_get_thread_data();
    void* thread_stack_base = nullptr;
    void* thread_stack_top = nullptr;               
    ThreadData* thread_data_ptr = nullptr;        
    if (thread_data_t == NULL) {
        KA_TRACE(0, STDERR, 0, "CheckAccess", "cannot get thread data", 0);
    } else {
        thread_data_ptr = static_cast<ThreadData*>(thread_data_t->ptr);  
        if (thread_data_ptr->stack_top_addr == nullptr || thread_data_ptr->stack_base_addr == nullptr) {
            KA_TRACE(0, STDERR, 0, "CheckAccess", "thread stack bound not completely set", 0);
        } else {
            thread_stack_base = thread_data_ptr->stack_base_addr;  
            thread_stack_top = thread_data_ptr->stack_top_addr;      
        } 
    }

    void* current_exit = frame_t->exit_frame; 
    if (!thread_stack_base || !thread_stack_top || !current_exit) {
#ifdef VERBOSE
        KA_TRACE(1000, STDERR, 0, "CheckAccess", "thread base: %s  top: %s exit frame: %s", thread_stack_base? "yes":"no",
           thread_stack_top? "yes" : "no",
           current_exit? "yes" : "no");
#endif
        return;
    }

    DataSharingAttr data_sharing;

    if ((uint64_t)address < (uint64_t)thread_stack_base || 
         (uint64_t)address > (uint64_t)thread_stack_top) {
#ifdef VERBOSE
        KA_TRACE(1000, STDOUT, 0, "CheckAccess", "address out of thread stack %p", address);
#endif
        data_sharing = NON_THREAD_PRIVATE;   
    } else { // address is within currnet thread stack. 
        if ((uint64_t)address < (uint64_t)current_exit) {
#ifdef VERBOSE
            KA_TRACE(1000, STDOUT, 0, "CheckAccess", "addr below cur exit frame %p < %p", address, current_exit);
#endif
            data_sharing = THREAD_PRIVATE_BELOW_EXIT;
        } else {
#ifdef VERBOSE
            KA_TRACE(1000, STDOUT, 0, "CheckAccess", "addr above cur exit frame %p > %p", address, current_exit);
#endif
            data_sharing = THREAD_PRIVATE_ABOVE_EXIT;
        }
        thread_data_ptr->SetLowestAddr(address); // update the lowest address touched on current stack by current task
    }
    //further check for explicit task. 
    /* -------- END DATA SHARING PROPERTY CHECKING ------------------------------------*/
#ifdef BYTE_LEVEL
    AccessHistory* access_history_base = GetOrCreateShadowBaseAddress<AccessHistory>(address);
    long first_slot_index = (long)(PAGE_OFFSET((uint64_t)address));
    long last_slot_index = (long)(first_slot_index  + bytes_accessed - 1);
    long overflow = last_slot_index - (long)NUM_ENTRY_SHADOW_PAGE + 1;
    AccessHistory* access_history = (AccessHistory*)access_history_base + first_slot_index;
    if (overflow <= 0) { // not overflowing
        for (uint8_t i = 0; i < bytes_accessed; ++i) {
            ExecuteCheckProtocol(&access_history[i], task_data_ptr, current_label, current_lockset, flag, data_sharing, 
                    address
#ifdef DEBUG_EXPLICIT_TASK
                    , task_type
#endif
                    , instn_addr 
                    , bytes_accessed 
                    , hw_lock
#ifdef PERFORMANCE_INSPECT
                    , i
#endif
                    );
        }
    } else {          
        for (uint8_t i = 0; i < bytes_accessed - overflow; i++) {
            ExecuteCheckProtocol(&access_history[i], task_data_ptr, current_label, current_lockset, flag, data_sharing, 
                    address
#ifdef DEBUG_EXPLICIT_TASK
                    , task_type
#endif
                    , instn_addr 
                    , bytes_accessed
                    , hw_lock
#ifdef PERFORMANCE_INSPECT
                    , i
#endif
                    );
        }
        access_history = GetOrCreateShadowBaseAddress<AccessHistory>((char*)address + bytes_accessed);
        for (uint8_t i = 0; i < overflow; ++i) {
            ExecuteCheckProtocol(&access_history[i], task_data_ptr, current_label, current_lockset, flag, data_sharing, 
                    address
#ifdef DEBUG_EXPLICIT_TASK
                    , task_type
#endif
                    , instn_addr 
                    , bytes_accessed
                    , hw_lock 
#ifdef PERFORMANCE_INSPECT
                    , i
#endif
                    );
        }
    }
#endif
#ifdef WORD_LEVEL   
    uint32_t words_accessed = bytes_accessed >> 2; // divided by 4
    AccessHistory* access_history_base = GetOrCreateShadowBaseAddress<AccessHistory>(address);
    long first_slot_index = (long)PAGE_OFFSET((uint64_t)address);
    long last_slot_index = (long)(first_slot_index + (long)words_accessed - 1); 
    long overflow = last_slot_index - (long) (NUM_ENTRY_SHADOW_PAGE) + 1;
    AccessHistory* access_history = (AccessHistory*)access_history_base + first_slot_index;
    if (overflow <= 0) {
        for (uint8_t i = 0; i < words_accessed; ++i) {
            ExecuteCheckProtocol(&access_history[i], task_data_ptr, current_label, current_lockset, flag, data_sharing, 
                    address
#ifdef DEBUG_EXPLICIT_TASK
                    , task_type
#endif
                    , instn_addr 
                    , words_accessed
                    , hw_lock 
#ifdef PERFORMANCE_INSPECT
                    , i
#endif
          );
        }
    } else {
        for (uint8_t i = 0; i < words_accessed - overflow; i++) {
            ExecuteCheckProtocol(&access_history[i], task_data_ptr, current_label, current_lockset, flag, data_sharing, 
                    address
#ifdef DEBUG_EXPLICIT_TASK
                    , task_type
#endif
                    , instn_addr 
                    , words_accessed
                    , hw_lock 
#ifdef PERFORMANCE_INSPECT
                    ,i
#endif
                  );
        }
        // get the next shadow page 
        access_history = GetOrCreateShadowBaseAddress<AccessHistory>((char*)address + bytes_accessed); // it is not possible to overflow again
        for (uint8_t i = 0; i < overflow; ++i) {
            ExecuteCheckProtocol(&access_history[i], task_data_ptr, current_label, current_lockset, flag, data_sharing, 
                    address
#ifdef DEBUG_EXPLICIT_TASK
                    , task_type
#endif
                    , instn_addr 
                    , words_accessed 
                    , hw_lock 
#ifdef PERFORMANCE_INSPECT
                    ,i
#endif
                   );
        }
    }
#endif

}
}
