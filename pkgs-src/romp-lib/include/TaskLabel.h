#ifndef TASK_LABEL_H
#define TASK_LABEL_H 
#include <algorithm>
#include <memory>
#include <set>
#include <sstream>
#include <stdio.h>
#include <string>
#include <vector>
#include "messages.h"
#define TASKGROUP_MAX_DEPTH 16
#define MAX_LOCK_PER_TASK 16
#define HW_LOCK_VALUE 0

//#define DEBUG_LABEL_CREATION
enum LabelType {
    UNDEF_LABEL,
    IMPLICIT = 0,
    EXPLICIT = 1,
    LOGICAL = 2 
};

enum LabelCreationType  {
    CREATE_AFTER_FORK,
    CREATE_EXP_ALLOC, 
    CREATE_FIRST_DISPATCH, // workshare iteration is first dispatched
    CREATE_NONFIRST_DISPATCH,   
    CREATE_LOOP_END
};

typedef struct TaskGroupInfo {
    int hash[TASKGROUP_MAX_DEPTH] = {0}; //support at most 16 level of nested taskgroup ... should be enough
    int current_level = 0;
    int age_count =  0;
    int label[1 + TASKGROUP_MAX_DEPTH] = {0};  // the first element is the length of the label 
    TaskGroupInfo(){} 
    TaskGroupInfo(const TaskGroupInfo& other);
} TaskGroupInfo;

using TaskGroupPtr = std::shared_ptr<TaskGroupInfo>;

typedef struct LockSetInfo {
    uint8_t num_locks;
    uint64_t lock_1;
    uint64_t lock_2;  
    std::set<uint64_t> lockset;
    LockSetInfo() {
        num_locks = 0;
        lock_1 = 0;
        lock_2 = 0;
    }
    LockSetInfo(const LockSetInfo& other);
    void add_lock(uint64_t lock);
    void remove_lock(uint64_t lock);
    std::string ToString() {
        std::string ret;
        ret = "locks: <";
        if (num_locks == 0) {
            ret = "empty";
        } else if (num_locks == 1) {
            ret += std::to_string(lock_1);           
        } else if (num_locks == 2) {
            ret += std::to_string(lock_1);
            ret += " , ";
            ret += std::to_string(lock_2);     
        } else {
            for (auto lock : lockset) {
                ret += std::to_string(lock);
                ret += " , ";
            }
        }
        ret += ">";
        return ret;
    }
} LockSetInfo;

using LockSetPtr = std::shared_ptr<LockSetInfo>;

/* 
 *  00: implicit
 *  01: explicit
 *  10: logical  (has iter is coupled with logical task tag, do not set or check) 
 *  segment type:
 *  00: LabelSegmentByte16  
 *  01: LabelSegmentByte20
 *  10: LabelSegmentByte32
 *  11: LabelSegmentByte36
 *
 *  iter-place-holder: if set means that this segment is added by loop begin, and should be nonsense, this makes 
 *  hot spot dispatch next callback avoid if-else branch. because the logic is always rip off last segment and append new segment
 */
#define SECTIONS_BIT_POS 4
enum SegmentType {
    BYTE_16=0,
    BYTE_24=1,
    BYTE_32=2,
    BYTE_40=3
};

inline void
SetBitAt(uint64_t& v, int x)
{
    v |= (1 << x);
}

inline void
ClearBitAt(uint64_t& v, int x)
{
    v &= ~(1 << x);
}

inline bool
GetBitAt(const uint64_t& v, int x)
{
    int bit = (v >> x) & 1;
    if (bit) {
        return true;
    } else {
        return false;
    }
}


inline SegmentType
GetSegmentType(const uint64_t& v)
{
    return (SegmentType)((v & 0x000000000000000c) >> 2);
}

inline bool
HasIterHolder(const uint64_t& v)
{
    return GetBitAt(v, SECTIONS_BIT_POS);
}

inline void 
SetSectionsHolderBit(uint64_t& v)
{
    SetBitAt(v, SECTIONS_BIT_POS);
}

inline void
ClearSectionsHolderBit(uint64_t& v)
{
    ClearBitAt(v, SECTIONS_BIT_POS);   
}

inline void
SetIterIndex(uint64_t& iter, uint64_t index)
{
    iter = index;
}

inline void
SetLabelType(uint64_t& v, const LabelType& type) 
{
    switch(type) {
        case IMPLICIT: // setting lowest two bits to 00
            ClearBitAt(v, 0);
            ClearBitAt(v, 1); 
            break;
        case EXPLICIT: // 01
            SetBitAt(v, 0);
            ClearBitAt(v, 1);
            break;
        case LOGICAL: // 10
            ClearBitAt(v, 0);
            SetBitAt(v, 1);     
            break;
        deault:
            break;
    }
}

inline bool
GetTaskWaitedFlag(const uint64_t& v)
{
    return ((v & 0x20) == 0x20);
}

inline void
SetTaskWaitedFlag(uint64_t& v)
{
    v |= 0x20; // toggle the taskwaited bit
}

inline bool
GetInExclusiveFlag(const uint64_t& v)
{
    return ((v & 0x80) == 0x80);
}

inline void
SetInExclusiveFlag(uint64_t& v)
{
    v |= 0x80;
}

inline bool 
GetHasOrderedFlag(const uint64_t& v)
{
    return ((v & 0x40) == 0x40);
}

inline void 
SetHasOrderedFlag(uint64_t& v)
{
    v |= 0x40; // toggle the has ordered bit
}

inline int 
GetLabelType(const uint64_t& v)
{
    uint64_t ans = v & 0x0000000000000003; // each digit is byte
    return (int)ans;
}

inline void
GetOffsetSpan(const uint64_t& v, int& offset, int& span) 
{
    int num = v >> 32;
    offset = (num & 0xffff0000) >> 16;
    span =  num & 0x0000ffff;
}

inline void
SetOffsetSpan(uint64_t& v, int offset, int span) 
{
    uint64_t o = (uint64_t)offset;
    uint64_t s = (uint64_t)span;
    uint64_t mask_o = ((o << 48) & 0xffff000000000000);
    uint64_t mask_s = ((s << 32) & 0x0000ffff00000000);
    v &= 0x00000000ffffffff;
    v |= mask_o; 
    v |= mask_s;
#ifdef DEBUG_LABEL_CREATION
    int test_offset, test_span;
    GetOffsetSpan(v, test_offset, test_span);
    KA_TRACE(0, STDOUT, 0, "DEBUG", "SetOffsetSpanLabel: should have offset: %d = %d, span: %d = %d", offset,test_offset, span,test_span);
#endif
}

inline void
SetTaskWait(uint64_t& v, int taskwait)
{
    uint64_t tw = (uint64_t)taskwait; 
    uint64_t mask = ((tw << 28) & 0x00000000f0000000);
    v &= 0xffffffff0fffffff;    
    v |= mask;
}

inline int
GetTaskWait(const uint64_t& v) 
{
    return (int)((v & 0x00000000f0000000) >> 28);
}

inline void
SetTaskCreate(uint64_t& v, int taskcreate)
{
    uint64_t tc = (uint64_t)taskcreate;
    uint64_t mask = ((tc << 24) & 0x000000000f000000);
    v &= 0xfffffffff0ffffff;
    v |= mask;
}

inline int
GetTaskCreate(const uint64_t& v) 
{
    return (int)((v & 0x000000000f000000) >> 24);
}

inline void
SetLoopCnt(uint64_t& v, int loopcnt)
{
    uint64_t tc = (uint64_t)loopcnt;
    uint64_t mask = ((tc << 20) & 0x0000000000f00000);
    v &= 0xffffffffff0fffff;
    v |= mask;
}

inline int
GetLoopCnt(const uint64_t& v)
{
    return (int)((v & 0x0000000000f00000) >> 20);
}

inline void
SetPhase(uint64_t& v, int phase)
{
    uint64_t tc = (uint64_t)phase;
    uint64_t mask = ((tc << 16) & 0x00000000000f0000);
    v &= 0xfffffffffff0ffff;
    v |= mask;
}

inline int
GetPhase(const uint64_t& v)
{
    return (int)((v & 0x00000000000f0000) >> 16);
}

typedef struct LabelHead {
    void* next; //  8 bytes
    LabelHead() {
        next = nullptr;
    }
} LabelHead;

typedef struct LabelSegmentPrefix {
    void* next;
} LabelSegmentPrefix;

/* 
 *
 * |offset|span|taskwait|taskcreate|loop cnt|phase|-reserved-|has-ordered|taskwaited|sections-place-holder|segment type|task type|
 *   16     16    4          4         4       4      9           1                 1            1              1              2           2 
 *
 * total 8 * 8 = 64 bits
 * ===========================================STALE DESIGN=====================================================================
 * |offset|span|taskwait|taskcreate|loop cnt|phase|-reserved-|taskwaited|iter-place-holder|segment type|task type|
 *   16     16    4          4         4       4      10          1               1              2           2 
 * =========================================END STALE DESIGN===================================================================
 *
 * ===========================================STALE DESIGN=====================================================================
 * |offset|span|taskwait|taskcreate|loop cnt|phase|-reserved-|has lockset|seg type: |iter-place-holder|has-taskgroup|task type|
 *   16bit 16bit   4bit      4bit      4bit   4bit    9bit      1bit       2bits        1bit              1bit         2bit
 * =========================================END STALE DESIGN===================================================================
 *  type: 
 *  00: implicit
 *  01: explicit
 *  10: logical  (has iter is coupled with logical task tag, do not set or check) 
 *  segment type:
 *  00: LabelSegmentByte16  
 *  01: LabelSegmentByte24
 *  10: LabelSegmentByte32
 *  11: LabelSegmentByte40
 *
 *  iter-place-holder: if set means that this segment is added by loop begin, and should be nonsense, this makes 
 *  hot spot dispatch next callback avoid if-else branch. because the logic is always rip off last segment and append new segment
 *   
 *  taskwaited: set by parent task if the parent task encounters the taskwait synchronization point. 
 */

typedef struct LabelSegmentByte16 { // 00  
    void* next;  
    uint64_t v;
    LabelSegmentByte16() {
        v = 0;
        next = nullptr;
    }      
    LabelSegmentByte16(const LabelSegmentByte16& other) {//copy constructor 
        v = other.v;
        next = nullptr;  
    }    
} LabelSegmentByte16;

typedef struct LabelSegmentByte24 { // 01
    void* next; // 8
    uint64_t v; // 8
    uint64_t iter;
    LabelSegmentByte24() {
        next = nullptr;
        v = 0x0000000000000004;  // the segment type is preset  
        iter = 0;
    }  
    LabelSegmentByte24(const LabelSegmentByte24& other) {//copy constructor
        next = nullptr;
        v = other.v;
        iter = other.iter;
    }    
} LabelSegmentByte24;

typedef struct LabelSegmentByte32 { // 10 //  has taskgroup 
    void* next;   // 8
    uint64_t v; // 8    
    TaskGroupPtr taskgroup_ptr; // 16    
    LabelSegmentByte32() {
        v = 0x0000000000000008;     
        next = nullptr;
        taskgroup_ptr = nullptr;   
    }      
    
    LabelSegmentByte32(const LabelSegmentByte16& other, TaskGroupPtr& tg) {//this is to create a new label segment with new taskgroup 
        v = other.v; // first copy all the bits in the original one
        //should adjust the segment type
        v |= 0x8;   // set the seg type bits 
        next = nullptr;
        taskgroup_ptr = tg; // store the taskgroup pointer.
    }
         
    LabelSegmentByte32(const LabelSegmentByte32& other) {//copy constructor
        v = other.v;
        next = nullptr;
        taskgroup_ptr = std::make_shared<TaskGroupInfo>(*(other.taskgroup_ptr.get()));//make a deep copy, sounds legit
    }    
} LabelSegmentByte32;

typedef struct LabelSegmentByte40 { // 11
    void* next;   // 8
    uint64_t v; // 8    
    TaskGroupPtr taskgroup_ptr; // 16    
    uint64_t iter; //  
    LabelSegmentByte40() {
        next = nullptr;
        v = 0x000000000000000c;
        iter = 0;
        taskgroup_ptr = nullptr;
    }

    LabelSegmentByte40(const LabelSegmentByte24& other, TaskGroupPtr& tg) {
        //this constructor is to upgrade a byte24 segment to byte40 with additional taskgroup struct.
        v = other.v; // first copy all the bits in the other one
        iter = other.iter; // copy the iter counter from the other one
        v |= 0x8; // because byte24's seg type is 01 and byte40's seg type is 11
        next = nullptr;
        taskgroup_ptr = tg; // store the taskgroup pointer.
    }

    LabelSegmentByte40(const LabelSegmentByte40& other) {//copy constructor
        next = nullptr;
        v = other.v;
        iter = other.iter;    
        taskgroup_ptr = std::make_shared<TaskGroupInfo>(*(other.taskgroup_ptr.get()));
    }    
} LabelSegmentByte40;

inline int
GetIterValue(void* seg)
{
    auto seg16_ptr = static_cast<LabelSegmentByte16*>(seg);      
    auto seg_type = GetSegmentType(seg16_ptr->v);    
    assert(seg_type == BYTE_24 || seg_type == BYTE_40);
    auto ptr = static_cast<LabelSegmentByte24*>(seg);
    return ptr->iter; 
}

inline TaskGroupPtr
GetTaskGroupPtr(void* seg)
{
   auto seg16_ptr = static_cast<LabelSegmentByte16*>(seg);  
   auto seg_type = GetSegmentType(seg16_ptr->v);    
   TaskGroupPtr ret = nullptr;
   switch(seg_type) {
        case BYTE_16:
            break;
        case BYTE_24:
            break;
        case BYTE_32:
            ret = static_cast<LabelSegmentByte32*>(seg)->taskgroup_ptr;
            break;
        case BYTE_40:
            ret = static_cast<LabelSegmentByte40*>(seg)->taskgroup_ptr;      
            break;
        default:
            break;
   }
   return ret;
}

inline std::string 
SegmentValueToString(const uint64_t& v)
{
    std::stringstream buffer;
    int offset, span;
    GetOffsetSpan(v, offset, span); 
    auto taskwait_cnt = GetTaskWait(v);
    auto taskcreate_cnt = GetTaskCreate(v);
    auto loop_cnt = GetLoopCnt(v);
    auto phase = GetPhase(v);
    auto label_type = GetLabelType(v);  
    auto seg_type = GetSegmentType(v);
    buffer << "v: type:" << label_type << ",offset:" << offset << ",spn:" << span << ",tw:" << taskwait_cnt << ",tc:" << taskcreate_cnt << ",lc:" << loop_cnt << ",phs:" << phase;
   return buffer.str();     
}

inline std::string
SegmentToString(void* ptr) 
{
   std::stringstream buffer;     
   auto v = static_cast<LabelSegmentByte16*>(ptr)->v; 
   auto seg_type = GetSegmentType(v);  
   auto v_str = SegmentValueToString(v);     
   uint64_t iter_val = 0;
   switch(seg_type) {
      case BYTE_16:
          buffer << "[" << v_str << "]"; 
          break;
      case BYTE_24:
          iter_val = static_cast<LabelSegmentByte24*>(ptr)->iter; 
          buffer << "[" << v_str << " iter:" << std::to_string(iter_val) << "]";
          break;
      case BYTE_32:
          //auto iter = static_cast<LabelSegmentByte24*>(ptr)->iter; 
          //buffer << "[" << v_str << " iter:" << to_string(iter) << "]";
          buffer << "[" << v_str << " has task group" << "]";
          break;
      case BYTE_40:
          iter_val = static_cast<LabelSegmentByte40*>(ptr)->iter; 
          buffer << "[" << v_str << " iter:" << std::to_string(iter_val) << "]";
          break;
      default:
          break; 
    }  
    return buffer.str();
}


/*
typedef struct LabelSegment { // This is just for prototyping the system. Very space inefficient. takes around 9 * 8 = 72 bytes for just one label segment.
    LabelType type;   // 4
    int offset; // 4
    int span; // 4 
    int taskwait_cnt; //4 
    int taskcreate_cnt; // 4 
    TaskGroupPtr taskgroup_ptr = nullptr; // 16
    int loop_cnt;           // 4
    int iter; // 4
    int phase;  // 4
    LabelSegment(){} 
    LabelSegment(LabelType, int, int, int, int, TaskGroupPtr, int ,int, int);     
    void print() {
       KA_TRACE(0, STDOUT, 0, "", "[type:%d,ofst:%d,spn:%d,tw:%d,tc:%d,lc:%d,itr:%d,phs:%d]",type, offset, span, taskwait_cnt, taskcreate_cnt, taskcreate_cnt, loop_cnt, iter, phase); 
        if (taskgroup_ptr) {
            KA_TRACE(0, STDOUT, 0, "", "current level: %d", taskgroup_ptr->current_level); 
        }
    }
    std::string ToString() {
        std::stringstream buffer;
        buffer << "[type:" << type << ",ofst:" << offset << ",spn:" << span << ",tw:" << taskwait_cnt << ",tc:" << taskcreate_cnt << ",lc:" << loop_cnt << ",itr:" << iter << ",phs:" << phase << "]";
        return buffer.str();     
    }
    LabelSegment& operator=(const LabelSegment& other);  
    LabelSegment(const LabelSegment& other);    
} LabelSegment; 
*/

class Label;

using LabelPtr = std::shared_ptr<Label>;

typedef struct LabelCreateInfo {
    uint32_t num_seg_no_copy; // number of segments from the last one that are not copied.
    void* extended_segment;
    LabelPtr to_copy;
} LabelCreateInfo;

class Label {
    /* A single linked list implementation of label */
    private:
        void _CopyLabelAllButLastKSegments(LabelPtr& to_copy, uint32_t k);
    public:
        Label() {
            _head = new LabelHead(); 
            _tail = static_cast<void*>(_head);
            _cnt = 0;
        }
        
        Label(const LabelCreateInfo& create_info); 

        ~Label() {
            KA_TRACE(1000, STDOUT, 0, "Label", "destruct", 0);
            if (_head == nullptr) return; 
            void* ptr = _head->next;
            while (ptr) {
                auto seg16_ptr = static_cast<LabelSegmentByte16*>(ptr);  
                auto seg_type = GetSegmentType(seg16_ptr->v);
                auto tmp = ptr;
                ptr = seg16_ptr->next;
                switch(seg_type) {
                    case BYTE_16:
                        delete static_cast<LabelSegmentByte16*>(tmp);
                        break;
                    case BYTE_24:
                        delete static_cast<LabelSegmentByte24*>(tmp);
                        break;
                    case BYTE_32:
                        delete static_cast<LabelSegmentByte32*>(tmp);
                        break;
                    case BYTE_40:
                        delete static_cast<LabelSegmentByte40*>(tmp);
                        break;
                }
                tmp = nullptr; 
            }
            delete _head;
            _head = nullptr;
            _tail = nullptr;
            _cnt = 0;
        }

        void* GetLabelSegmentAt(uint32_t i_th) {
            // return the label segment at i_th (start counting from 1)  
            if (i_th > _cnt) 
                return nullptr;
            uint32_t cnt = 1;
            void* ptr = _head->next;
            while (cnt < i_th) {
                auto seg16_ptr = static_cast<LabelSegmentByte16*>(ptr);   
                ptr = seg16_ptr->next;
                cnt++; // pass one
            }
            return ptr;
        } 

        void* GetNext(void* cur) {
            auto seg16_ptr = static_cast<LabelSegmentByte16*>(cur);
            return seg16_ptr->next;
        }

        uint32_t Append(void* new_segment) {
            static_cast<LabelSegmentPrefix*>(_tail)->next = new_segment;
            _tail = new_segment;
            _cnt++;
            return _cnt;
        }
        
        uint32_t GetCnt() {
            return _cnt;
        }  

        std::string ToString() {
            std::string ret;
            void* ptr = _head->next;    
            while (ptr) {
                auto seg16_ptr = static_cast<LabelSegmentByte16*>(ptr);
                ret += SegmentToString(ptr);    
                ptr = seg16_ptr->next;
            }
            return ret; 
        } 

        LabelHead* GetHead() {
            return _head;
        } 

        void* GetTail() {
            return _tail;
        }   
    private:
        LabelHead* _head;
        void* _tail; 
        uint32_t _cnt;
};




LabelPtr CreateLabelAfterFork(LabelCreateInfo& create_info);
LabelPtr CreateLabelExpAlloc(LabelCreateInfo& create_info);
LabelPtr CreateLabelDispatchNext(LabelCreateInfo& create_info);
LabelPtr CreateLabelLoopEnd(LabelCreateInfo& create_info);
LabelPtr CreateLabelLoopBegin(LabelCreateInfo& create_info);
LabelPtr CreateLabelExplicitTaskFork(LabelCreateInfo& create_info);
LabelPtr CreateLabelSectionsEnd(LabelCreateInfo& create_info);
LabelPtr CreateLabelSectionsBegin(LabelCreateInfo& create_info);

LabelPtr ModifyLabelBarrier(LabelPtr& label);
LabelPtr ModifyParentLabelImplicitTaskEnd(LabelPtr& parent_label, LabelPtr& child_label);
LabelPtr ModifyLabelAcquiredOrdered(LabelPtr& label);
LabelPtr ModifyLabelReleaseOrdered(LabelPtr& label);
LabelPtr ModifyParentLabelExplicitTaskFork(LabelPtr& parent_label);
LabelPtr ModifyLabelTaskWait(LabelPtr& label);
LabelPtr ModifyLabelTaskGroupBegin(LabelPtr& label);
LabelPtr ModifyLabelTaskGroupEnd(LabelPtr& label);

bool SameLabel(LabelPtr& l1, LabelPtr& l2);
bool SameSegment(void* s1, void* s2);
bool SameTaskGroup(TaskGroupPtr& t1, TaskGroupPtr& t2);

inline bool
Includes(LockSetPtr& l1, LockSetPtr& l2) { 
    //assert(l1 != nullptr && l2 != nullptr); 
    // l1->num_locks > 0 && l2->num_locks > 0     
    // return true if l1 contains l2  
    auto l1_num_locks = l1->num_locks;
    auto l2_num_locks = l2->num_locks;                
    if (l1_num_locks < l2_num_locks)
        return false; 
    // now that l1_num_locks >= l2_num_locks
    auto l1_lock_1 = l1->lock_1;
    auto l2_lock_1 = l2->lock_1;
    bool l1_lock_1_equals_l2_lock_1 = (l1_lock_1 == l2_lock_1);
    if (l1_num_locks == 1) { // l2_num_locks == 1
        return l1_lock_1_equals_l2_lock_1;
    }       
    if (l1_num_locks == 2) { // l2_num_locks == 1 || l2_num_locks == 2
        auto l1_lock_2 = l1->lock_2;   
        if (l2_num_locks == 2) { //return true only if l1_lock_1 == l2_lock_1 and l1_lock_2 == l2_lock_2 because lock_1 < lock_2 (sorted)
            return l1_lock_1_equals_l2_lock_1 && (l1_lock_2 == l2->lock_2);
        }
        //l2_num_locks == 1
        return l1_lock_1_equals_l2_lock_1 || (l2_lock_1 == l1_lock_2);
    } 
    // l1_num_locks >= 3 , l2_num_locks == 1,2,3,... l1_num_locks   
    if (l1->num_locks >= 3) { 
        return std::includes(l1->lockset.begin(), l1->lockset.end(), l2->lockset.begin(), l2->lockset.end());
    } 
    // l1_num_locks >= 3, l2_num_locks == 1 || l2_num_locks == 2
    bool l2_lock_1_in_l1_set = (l1->lockset.find(l2_lock_1) != l1->lockset.end()); // if l2_lock_1 is not found in l1->lockset, set to true
    if (l2->num_locks == 1) {
        return l2_lock_1_in_l1_set;
    }    
    return l2_lock_1_in_l1_set && (l1->lockset.find(l2->lock_2) != l1->lockset.end()); //return true if l2_lock_1 in l1->lockset and l2_lock_2 in l1->lockset
}

inline bool
HasCommon(LockSetPtr& l1, LockSetPtr& l2) {
    //assert(l1 != nullptr && l2 != nullptr);
    // find the intersectin of l1 and l2
    //l1->num_locks > 0 && l2->num_locks > 0
    // assume l1->num_locks >= l2->num_locks;
    auto l1_num_locks = l1->num_locks; 
    auto l2_num_locks = l2->num_locks;       
    auto l1_lock_1 = l1->lock_1;
    auto l2_lock_1 = l2->lock_1; 
    bool l1_lock_1_equals_l2_lock_1 = (l1_lock_1 == l2_lock_1);
    if (l1_num_locks == 1) {  // l2_num_locks == 1
        return l1_lock_1_equals_l2_lock_1;   
    }
    if (l1_num_locks == 2) { // l2_num_locks == 2 || l2_num_locks == 1
        if (l2_num_locks == 1) {
            return l1_lock_1_equals_l2_lock_1;
        }    
        // l2_num_locks == 2
        auto l1_lock_2 = l1->lock_2;
        auto l2_lock_2 = l2->lock_2;
        if (l1_lock_2 < l2_lock_1 || l1->lock_1 > l2_lock_2) // disjoint 
            return false;
        if (l1_lock_1_equals_l2_lock_1 || (l1_lock_2 == l2_lock_1)  || (l1_lock_2 == l2_lock_2) || (l1_lock_1 == l2_lock_2))
            return true;
        return false;
    } 
    // l1_num_locks >= 3
    if (l2_num_locks >= 3) {
        std::vector<int> v_intersection;
        std::set_intersection(l1->lockset.begin(), l1->lockset.end(), l2->lockset.begin(), l2->lockset.end(),
                            std::back_inserter(v_intersection));  
        return !v_intersection.empty();
    }
    // l2_num_locks == 1 || l2_num_locks == 2
    bool l2_lock_1_in_l1_set = (l1->lockset.find(l2_lock_1) != l1->lockset.end()); // if l2_lock_1 is not found in l1->lockset, set to true
    if (l2_lock_1_in_l1_set)
        return true;
    // l2_lock_1 not in l1->lockset
    if (l2_num_locks == 1)
        return false;         
    return (l1->lockset.find(l2->lock_2) != l1->lockset.end());
}

#endif
