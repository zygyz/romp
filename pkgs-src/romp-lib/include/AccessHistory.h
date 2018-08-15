#ifndef ACCESS_RECORD_H 
#define ACCESS_RECORD_H
#include <atomic>
#include <memory>
#include <vector>
#include <unordered_map>
#include "pfq-rwlock.h"
#include "TaskLabel.h"

#define LEN_FILENAME 18

#define SET_DATA_UNIT_DEALLOC(MEM_STATE) \
        MEM_STATE |= 0x40

#define UNSET_DATA_UNIT_DEALLOC(MEM_STATE) \
        MEM_STATE &= 0xbf

#define GET_DATA_UNIT_DEALLOC(MEM_STATE, RES) \
        RES = (MEM_STATE & 0x40) > 0

#define SET_NUM_DATA_UNIT_ACCESSED(MEM_STATE, NUM_UNIT) \
        MEM_STATE &= 0xc3; \
        MEM_STATE |= ((char)NUM_UNIT) << 2

#define GET_NUM_DATA_UNIT_ACCESSED(MEM_STATE, NUM_UNIT) \
        NUM_UNIT = (MEM_STATE & 0x3c) >> 2

typedef enum {
    READ=1,
    WRITE=2,
    UNDEFINED=0
} AccessType;

typedef enum {
    PRIVATE=0,
    SHARED=1,
} SharedState;
// data shraing thread private below exit: 0
// thread private above exit: 2
// non thread private 3

/* Use two types of access record: one is with lockset pointer, one without. 
 * The one without lockset point has size 32 bytes. The one with lockst pointer has size 48 bytes.
 * The access_type is one byte large. The lowest 2 bits for identifiying the access type: 00: read 01: write: 10: undefind 
 * The third lowest bit marks if there exist lockset. If 
 * yes, it is 1, otherwise it is 0.
 * The fourth lowest bit marks if the record is in reduction. If yes, it is 1, else it is 0
 * The fifth lowest bit marks if the record is done by explicit task and is thread stack private to the explicit task
 * The sixth lowest bit marks if the record refers to an atomic instruction. If yes, it is 1, else it is 0 
*/
/*
typedef struct DummyNode {
    void* next;
    void* prev;
    DummyNode() {
        next = nullptr;
        prev = nullptr; 
    }
} DummyNode;
*/

typedef struct HeadNode {
    void* next;
    HeadNode() {
        next = nullptr;
    }     
} HeadNode;

typedef struct Prefix {
    void* next;
    LabelPtr label;
    char access_type;   
    void* task_ptr;           
    // below is for printing debug info
    uint64_t instn_addr;         
} Prefix;

typedef struct AR32 {
    void* next;
    LabelPtr label;
    char access_type;
    void* task_ptr;           
    //below is for printing debug info
    AR32(char type, LabelPtr& label, void* task_ptr, uint64_t instn_addr);
    uint64_t instn_addr;         
    AR32();
    AR32(char type, LabelPtr& label, void* task_ptr);
    ~AR32() {
        label.reset();
    }
    std::string ToString() {
        std::string ret = "AR32 --  label: ";
        ret += label->ToString();
        ret += " type: ";
        ret += std::to_string((int)access_type);         
        return ret;
    }  
} AR32;

AR32 :: AR32() 
{
    next = nullptr; 
    label = nullptr;
    access_type = 2; // access type undefined
    task_ptr = nullptr;
    instn_addr = 0; 
}


AR32 :: AR32(char type, LabelPtr& label_in, void* task_ptr_in) 
{
    next = nullptr;
    label = label_in;
    access_type = type;
    task_ptr = task_ptr_in;    
    instn_addr = 0;
} 

AR32 :: AR32(char type, LabelPtr& label_in, void* task_ptr_in, uint64_t instn_addr_in)
{
    next = nullptr;
    label = label_in;
    access_type = type;
    task_ptr = task_ptr_in;
    instn_addr = instn_addr_in;  
}

typedef struct AR48 {
    void* next; // 8
    LabelPtr label; // 16
    char access_type; // 1 + 7(padding)
    void* task_ptr;
    AR48(char type, LabelPtr& label, LockSetPtr& lockset, void* task_ptr, uint64_t instn_addr);
    uint64_t instn_addr;
    LockSetPtr lockset; //16
    AR48();
    AR48(char type, LabelPtr& label, LockSetPtr& lockset, void* task_ptr);
    ~AR48() {
#ifdef MEMORY_LEAK
        KA_TRACE(0, STDOUT, 0, "AR48", "Deallocation", 0);
#endif
        label.reset();  
        lockset.reset(); //TODO
    }
    std::string ToString() {
        std::string ret = "AR48 -- label: ";
        ret += label->ToString();
        ret += " type: ";
        ret += std::to_string((int)access_type);         
        ret += " lockset: ";
        ret += lockset->ToString();
        return ret;
    }  
} AR48;


AR48 :: AR48() 
{
    next = nullptr;
    label = nullptr;
    access_type = 4;  //makring has lockset
    task_ptr = nullptr;
    lockset = nullptr;
    instn_addr = 0;
}

AR48 :: AR48(char type, LabelPtr& label_in, LockSetPtr& lockset_in, void* task_ptr_in)
{
    next = nullptr;
    label = label_in; 
    access_type = 4 | type;
    task_ptr = task_ptr_in;       
    lockset = std::make_shared<LockSetInfo>(*(lockset_in.get())); // deep copy  //TODO
    instn_addr = 0;
}

AR48 :: AR48(char type, LabelPtr& label_in, LockSetPtr& lockset_in, void* task_ptr_in, uint64_t  instn_addr_in)
{
    next = nullptr;
    label = label_in;
    access_type = 4 | type;
    task_ptr = task_ptr_in;
    lockset = std::make_shared<LockSetInfo>(*(lockset_in.get())); // deep copy //TODO
    instn_addr = instn_addr_in;
}

inline bool
GetAccessRecordType(const char& v)
  // return true if it is large (contains lockset)
{ 
    auto ans = (v & 0x04) >> 2;   
    return ans == 1;
}

inline bool 
HasLockPrefix(const char& v) // 0b10 0000
{
    auto ans = (v & 20) >> 5;
}

inline void
SetHasLockPrefixBit(char& v)
{
    v |= 0x20;
}

inline void
ClearHasLockPrefixBit(char& v)
{
    // 0b1101 1111
    v &= 0xdf; 
}

inline bool  
InReduction(const char& v) 
{
    auto ans = (v & 0x08) >> 3;
    return ans == 1;      
}

inline void
SetInReductionBit(char& v)
{
    v |= 0x8;
}

inline void
ClearInReductionBit(char& v)
{
    v &= 0xf7; 
}

inline void
SetExpTskThreadPrivateBit(char& v)
{
    v |= 0x10; // 0b00010000
}

inline void
ClearExpTskThreadPrivatBit(char& v)
{
   v &= 0xef; 
}

inline bool
IsExpTaskThreadPrivate(char& v) 
{
    auto ans = (v & 0x10) >> 4;      
    return ans == 1;
}

inline int 
GetMemoryAccessType(const char& v)
{
    return (int)(v & 0x03);
}

/*
typedef struct AR40 {
    void* next; // 8 64 bits
    void* prev; // 8 64 bits   
    LabelPtr label;  // 16
    char access_type; //1+7(padding) 
    AR40();
    AR40(char type, LabelPtr& label);
    ~AR40() {
#ifdef MEMORY_LEAK
        KA_TRACE(0, STDOUT, 0, "AR40", "Deallocation", 0);
#endif
        label.reset();  
    }
} AR40;


AR40 :: AR40() 
{
    next = nullptr; 
    prev = nullptr;   
    label = nullptr;
    access_type = 2; // access type undefined
}

AR40 :: AR40(char type, LabelPtr& label_in) 
{
    next = nullptr;
    prev = nullptr;
    label = label_in;
    access_type = type;
}
  
typedef struct AR56 {
    void* next; // 8
    void* prev; // 8
    LabelPtr label; // 16
    char access_type; // 1 + 7(padding)
    LockSetPtr lockset; //16
    AR56();
    AR56(char type, LabelPtr& label, LockSetPtr& lockset);
    ~AR56() {
#ifdef MEMORY_LEAK
        KA_TRACE(0, STDOUT, 0, "AR56", "Deallocation", 0);
#endif
        label.reset();  
        lockset.reset();
    }
} AR56;

AR56 :: AR56() 
{
    next = nullptr;
    prev = nullptr;
    label = nullptr;
    access_type = 4; 
    lockset = nullptr;
}

AR56 :: AR56(char type, LabelPtr& label_in, LockSetPtr& lockset_in)
{
    next = nullptr;
    prev = nullptr; 
    label = label_in; 
    access_type = 4 | type;
    lockset = std::make_shared<LockSetInfo>(*(lockset_in.get())); // deep copy 
}



static inline void* 
setlsb(void* p)
{
    auto v = reinterpret_cast<uintptr_t>(p);
    return reinterpret_cast<void*>(v | 1); 
}

static inline void* 
clearlsb(void* p)
{
    auto v = reinterpret_cast<uintptr_t>(p);
    return reinterpret_cast<void*>(v & ~1);
}

static inline bool 
haslsb(void* p)
{
    auto v = reinterpret_cast<uintptr_t>(p);    
    return (v & 1) != 0;
}
*/

class RecordsListImp {
 //A light weight list impelementation of the access records container
    public:
        RecordsListImp() {
            KA_TRACE(1000, STDOUT, 0, "RecordsListImp ", "initialize", 0);
            _head = new HeadNode; // using dummy node will make traversal more efficient with the cost of 8 bytes, otherwise each iteration need an extra branch 
            _cnt = 0;
        }

        ~RecordsListImp() {
            while (_head->next) {
                _cnt = 0;
                deleteNodeAfter((void*)_head);
            }
            delete _head;
            _head = nullptr;
        }

        HeadNode* getHead() {
            return _head;
        }

        int getNumRecord() {
            return _cnt; 
        }

        void* next(void* cur_node) {
            assert(cur_node != nullptr);
            return ((HeadNode*)cur_node)->next;
        }

        void pushFront(void* new_node) {
            ((Prefix*)new_node)->next = _head->next;
            _head->next = new_node;
            _cnt++;
        }

        void deleteNodeAfter(void* prev_node) { 
           // swing the prev_node->next = pre_node->next->next;  
            assert(prev_node != nullptr);        
            void* to_delete = ((HeadNode*)prev_node)->next; 
            ((HeadNode*)prev_node)->next = ((HeadNode*)to_delete)->next; // do the swing
            auto type = ((Prefix*)to_delete)->access_type;
            if (GetAccessRecordType(type)) {
                KA_TRACE(1000, STDOUT, 0, "deleteNodeAfter", "AR48", 0);
                delete (AR48*)to_delete;
            } else {
                KA_TRACE(1000, STDOUT, 0, "deleteNodeAfter", "AR32", 0);
                delete (AR32*)to_delete;
            }
            to_delete = nullptr;
            _cnt--;
            return;
        }
        
    private:
        HeadNode* _head;
        int _cnt;
};

/*
class RecordsConcListImp {
    // The Hakan Sundell concurrent linked list with ThreadScan 
    public:
        RecordsConcListImp() {
            _head = new DummyNode();
            _tail = new DummyNode();
            _head->prev = reinterpret_cast<void*>(BOTTOM);
            _head->next = _tail;
            _tail->prev = _head;
            _tail->next = reinterpret_cast<void*>(BOTTOM); 
        }

        bool empty() {
            return _head->next == _tail;
        }

        DummyNode* get_head() {
            return _head;
        }

        DummyNode* get_tail() {
            return _tail;
        }
        
        void pushFront(void* new_node) {
            void* prev = (void*)_head;
            void* next;
            do {
                next = CAST(prev)->next;
                CAST(new_node)->prev = prev;    
                CAST(new_node)->next = next;
            } while (!CAS(&(CAST(prev)->next), next, new_node));
            KA_TRACE(1000, STDOUT, 0, "pushFront", "after cas", 0);
            linkPrev(new_node, next);
        }

        bool next(void*& cursor) 
        {
            assert(!haslsb(cursor));
            while (true) {
                if (cursor == _tail) 
                    return false;
                auto next = clearlsb(CAST(cursor)->next);
                auto d = haslsb(CAST(next)->next); 
                if (d && CAST(cursor)->next != setlsb(next)) {
                    setMark(&(CAST(next)->prev));
                    CAS(&(CAST(cursor)->next), next, clearlsb(CAST(next)->next));
                      
        //            threadscan_collect(next); 
                    continue;
                }
                cursor = next;
                if (!d && next != (void*)_tail) 
                    assert(reinterpret_cast<uintptr_t>(next) != BOTTOM);
                if (cursor == (void*)_tail) 
                    return false;
                return true;
            }
        }    
       
        int deleteNodeAfter(void* cursor)
        {
            assert(!haslsb(cursor));
            auto node = cursor;
            if (node == (void*)_head || node == (void*)_tail)
                return -1;
            while (true) {
                auto next = CAST(cursor)->next;
                if (haslsb(next)) {
                    return -1;
                }
                if (CAS(&(CAST(node)->next), next, setlsb(next))) {
                    void* prev;
                    while (true) {
                        prev = CAST(node)->prev;        
                        if (haslsb(prev) || CAS(&(CAST(node)->prev), prev, setlsb(prev)))
                            break;

                    }
                    assert(!haslsb(next));
                    correctPrev(clearlsb(prev), next);
        //            threadscan_collect(node);
                    return 0;
                }
            }
        }
        

     private:
        DummyNode* _head;
        DummyNode* _tail;     

        void setMark(void** link)
        {
            void* p;
            do {
                p = *link;
            } while (!haslsb(p) && !CAS(link, p, setlsb(p)));
        }

        bool correctPrev(void* prev, void* node)
        {
            assert(!haslsb(prev));
            assert(!haslsb(node));
            assert(reinterpret_cast<uintptr_t>(prev) != BOTTOM);
            assert(reinterpret_cast<uintptr_t>(node) != BOTTOM); 

            void* lastlink = reinterpret_cast<void*>(BOTTOM);  

            while (true) {
                auto link1 = CAST(node)->prev;
                if (haslsb(CAST(node)->next))
                    return false;
                auto prev2 = CAST(prev)->next;
                if (haslsb(prev2)) {
                    if (reinterpret_cast<uintptr_t>(lastlink) == BOTTOM) {
                        prev = clearlsb(CAST(prev)->prev);
                    } else {
                        setMark(&(CAST(prev)->prev));
                        CAS(&(CAST(lastlink)->next), prev, clearlsb(prev2));
                        prev = lastlink;
                        lastlink = reinterpret_cast<void*>(BOTTOM);
                    }
                    continue;
                }
                if (prev2 != node) {
                    lastlink = prev;
                    prev = prev2;
                    continue;
                }
                if (CAS(&(CAST(node)->prev), link1, clearlsb(prev))) {
                    if (haslsb(CAST(prev)->prev))
                        continue;
                    else 
                        break;
                }
            } 
            return true;
        }

        void linkPrev(void* node, void* next) {
            void* link1;
            do {
                link1 = CAST(next)->prev;
                if (haslsb(link1) || CAST(node)->next != next)
                    return;
            } while (!CAS(&(CAST(next)->prev), link1, clearlsb(node))); 
            if (haslsb(CAST(node)->prev))
                correctPrev(node, next); 
        }       
};
*/

//using RecordsPtr = std::shared_ptr<RecordsConcListImp>;
using RecordsPtr = std::shared_ptr<RecordsListImp>;

/* This implements the record that is stored in the shadow memory 
 * The record is used for recording the history of threads reading/writing the  associated shadow memory address*/
typedef struct AccessHistory { //relate to each memory address
    //VersionInfo version_info;
    RecordsListImp* access_records; // 8 bytes // save 8 bytes, shared pointer here is not very usful any way
    mcs_lock_t lock; // 8bytes 
    //pfq_rwlock_t lock;        
    char mem_state; //  1bytes, padded to 4 bytes? 1 byte = 8 bits 
    //uint32_t access_time;
    AccessHistory() { /*access_time = 0; */}
    // lowest bit = 1: shared lowest bit = 0: private: highest bit = 1: race found on this address 
    // second lowest bit = 1: allocated 0: deallocated 
    // | race on this slot (1 if true)| deallocated (1 if true)| # of data unit accessed |  resv | shared/private (1 if shared) |
    // |        1 bit                 | 1bit                   | 4 bits                  | 1 bit |    1 bit                     |
         
} AccessHistory;
#endif
