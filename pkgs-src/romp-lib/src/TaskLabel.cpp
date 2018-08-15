#include <algorithm>
#include <cassert>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include "TaskLabel.h"
//#define DEBUG_TASKGROUP
using namespace std;
//#define DEBUG_LABEL_CREATE
//#define DEBUG_LABEL_MOD
//mutex g_val_mutex;

LockSetInfo :: LockSetInfo(const LockSetInfo & other) { // copy constructor
    num_locks = other.num_locks;
    if (num_locks > 0 && num_locks <= 2) {
        lock_1 = other.lock_1;
        lock_2 = other.lock_2;
    } else {
        lockset = other.lockset;
    }
}

void LockSetInfo :: add_lock(uint64_t lock) {
    if (num_locks == 0) { // no lock, set the first lock
        lock_1 = lock;
    } else if (num_locks == 1) { // has one lock, set the second lock
        auto smaller = min(lock, lock_1);
        auto larger = max(lock, lock_1); 
        lock_1 = smaller;
        lock_2 = larger;
    } else if (num_locks == 2) { // has two locks, move into lockset representation 
        lockset.insert(lock_1);
        lockset.insert(lock_2);   
        lockset.insert(lock);
    } else { // has three locks or more
        lockset.insert(lock); 
    }
    num_locks++; // increment the lock count 
}

void LockSetInfo :: remove_lock(uint64_t lock) {
    assert(num_locks > 0);  // should at least contain one lock  
    if (num_locks == 1) {
        assert(lock == lock_1);        
        lock_1 = 0;
    } else if (num_locks == 2) {
        assert(lock == lock_1 || lock == lock_2);
        if (lock == lock_1) {
            lock_1 = lock_2; // replace lock_1 with lock_2, and reset lock_2
            lock_2 = 0;
        } else {
            lock_2 = 0; // remove lock_2 
        } 
    } else { // >= 3 locks
        auto it = lockset.find(lock);
        lockset.erase(it);       
        if (num_locks == 3) { // if contains three locks, move back to two lock representation 
            uint64_t tmp[2];
            int i = 0;
            for (auto lock : lockset) {
                tmp[i] = lock;
                i++; 
            }
            lock_1 = min(tmp[0], tmp[1]);
            lock_2 = max(tmp[0], tmp[1]);
            lockset.clear();
        } 
    }
    num_locks--; // decrement lock count 
}

inline void*
CopySegmentBaseOnType(void* ptr)
{
    auto seg16_ptr = static_cast<LabelSegmentByte16*>(ptr);       
    auto seg_type = GetSegmentType(seg16_ptr->v);      
    void* copy = nullptr;
    switch(seg_type) {
        case BYTE_16:
            copy = static_cast<void*>(new LabelSegmentByte16(*seg16_ptr)); 
            break;
        case BYTE_24:
            copy = static_cast<void*>(new LabelSegmentByte24(*(static_cast<LabelSegmentByte24*>(ptr)))); 
            break;
        case BYTE_32:
            copy = static_cast<void*>(new LabelSegmentByte32(*(static_cast<LabelSegmentByte32*>(ptr)))); 
            break;
        case BYTE_40:
            copy = static_cast<void*>(new LabelSegmentByte40(*(static_cast<LabelSegmentByte40*>(ptr)))); 
            break;
        deault:
            break;
    }
    return copy;
}

inline void 
Label :: _CopyLabelAllButLastKSegments(LabelPtr& to_copy, uint32_t k) 
{
#ifdef DEBUG_LABEL_CREATE
    assert(to_copy != nullptr); 
#endif
    auto label_head = to_copy->GetHead();
    auto ptr = label_head->next; 
    assert(ptr != nullptr);
    auto num_segs = to_copy->GetCnt();
#ifdef DEBUG_LABEL_CREATE
    assert(num_segs >= k);   
#endif
    //assert(num_segs >= 2);
    uint32_t num_seg_copied = 0;
    while (ptr && num_seg_copied < num_segs - k) {
        auto copy = CopySegmentBaseOnType(ptr);
        assert(copy != nullptr);
        static_cast<LabelSegmentPrefix*>(_tail)->next = copy;   
        _tail = copy;
        ptr = static_cast<LabelSegmentPrefix*>(ptr)->next;
        num_seg_copied++; 
    }        
#ifdef DEBUG_LABEL_CREATE
    assert(num_seg_copied == num_segs - k);
#endif
    _cnt = num_segs - k;        
}

Label :: Label(const LabelCreateInfo& create_info) 
{
    _head = new LabelHead(); 
    _tail = static_cast<void*>(_head);
    auto k = create_info.num_seg_no_copy;
    auto to_copy = create_info.to_copy; 
    _CopyLabelAllButLastKSegments(to_copy, k);           
}

LabelPtr
CreateLabelAfterFork(LabelCreateInfo& create_info)
{
    auto label = make_shared<Label>(create_info);
    label->Append(create_info.extended_segment);
    return label;
}

LabelPtr 
CreateLabelExpAlloc(LabelCreateInfo& create_info)
{
    auto label = make_shared<Label>(create_info);
    label->Append(create_info.extended_segment);
    return label;
}

LabelPtr
CreateLabelDispatchNext(LabelCreateInfo& create_info)
{
    auto label = make_shared<Label>(create_info);
    label->Append(create_info.extended_segment);   
    return label;
}

LabelPtr
CreateLabelSectionsBegin(LabelCreateInfo& create_info)
{
    auto label = make_shared<Label>(create_info);
    label->Append(create_info.extended_segment); 
    return label;
}

LabelPtr
CreateLabelSectionsEnd(LabelCreateInfo& create_info)
{
    auto label = make_shared<Label>(create_info);
    auto tail_ptr = static_cast<LabelSegmentByte16*>(label->GetTail());  
    auto cur_loop_cnt = GetLoopCnt(tail_ptr->v);
    SetLoopCnt(tail_ptr->v, cur_loop_cnt+1);
    return label;
}

LabelPtr
CreateLabelLoopBegin(LabelCreateInfo& create_info)
{
    auto label = make_shared<Label>(create_info);
    label->Append(create_info.extended_segment);
    return label;
}

LabelPtr
CreateLabelExplicitTaskFork(LabelCreateInfo& create_info)
{  
    auto label = make_shared<Label>(create_info);         
    label->Append(create_info.extended_segment);
    return label;
}

LabelPtr
CreateLabelLoopEnd(LabelCreateInfo& create_info)
{
    auto label = make_shared<Label>(create_info);
    auto tail_ptr = static_cast<LabelSegmentByte16*>(label->GetTail());  
    auto cur_loop_cnt = GetLoopCnt(tail_ptr->v);
    SetLoopCnt(tail_ptr->v, cur_loop_cnt+1);
    return label;
}

TaskGroupInfo :: TaskGroupInfo(const TaskGroupInfo& other)
{
    for (int i = 0; i < TASKGROUP_MAX_DEPTH; ++i) {
        hash[i] = other.hash[i];
        label[i] = other.label[i]; 
    }
    current_level = other.current_level;   
    age_count = other.age_count;
}

inline LabelPtr
CopyCreateLabelByNumSegments(uint32_t num_seg_to_copy, LabelHead* orig_head, void*& ptr) 
{        
    auto modified = make_shared<Label>(); 
    ptr = orig_head->next;
    uint32_t cnt = 0;
    while (cnt < num_seg_to_copy && ptr) {
        auto seg16_ptr = static_cast<LabelSegmentByte16*>(ptr);
        void* copy = CopySegmentBaseOnType(ptr);
        cnt = modified->Append(copy);
        ptr = seg16_ptr->next; //. go to next segment
    }
    return modified;
}

LabelPtr
ModifyLabelBarrier(LabelPtr& label)
{
  /* create new copy of label because it is modified */  
    KA_TRACE(1000, STDOUT, 0, "ModifyLabelBarrier", "start ", 0);
    auto len_label = label->GetCnt(); 
    if (len_label < 2) {
        KA_TRACE(1000, STDERR, 0, "ModifyLabelBarrier", "len_label < 2 %s", label->ToString().c_str());
        return nullptr;
    } 
    //auto modified = make_shared<LabelStruct>(*(label.get()));
    auto orig_head = label->GetHead();       
    void* ptr = nullptr; 
    auto modified = CopyCreateLabelByNumSegments(len_label - 1, orig_head, ptr);
    // now first modify the second last label segment
    auto tail = modified->GetTail(); 
    int old_offset, span;
    GetOffsetSpan(static_cast<LabelSegmentByte16*>(tail)->v, old_offset, span);          
    auto new_offset = old_offset + span;
    SetOffsetSpan(static_cast<LabelSegmentByte16*>(tail)->v, new_offset, span);
    // now append the last segment
    auto copy = CopySegmentBaseOnType(ptr);
    modified->Append(copy);
#ifdef DEBUG_LABEL_MOD
    KA_TRACE(0, STDERR, 0, "ModifyLabelBarrier", "finish %s", modified->ToString().c_str());
#endif
    return modified;
}

LabelPtr
ModifyParentLabelImplicitTaskEnd(LabelPtr& parent_label, LabelPtr& child_label)
{
    //need to replace the parent label with a newly created one.
    //first copy the parent label segments to the new one
    //sommehow the copy constructor does not work well do this explicitly
    KA_TRACE(1000, STDOUT, 0, "ModifyLabelImplicitTaskEnd", "start ", 0);
   // replace the last segment with the child label's second last label segment 
    auto len_parent_label = parent_label->GetCnt();
    auto orig_parent_head = parent_label->GetHead();      
    void* ptr = nullptr;
    auto modified = CopyCreateLabelByNumSegments(len_parent_label - 1, orig_parent_head, ptr);
    auto len_child_label = child_label->GetCnt(); 
    auto child_seg = child_label->GetLabelSegmentAt(len_child_label - 1);      
    auto copy = CopySegmentBaseOnType(child_seg);           
    modified->Append(copy);  
#ifdef DEBUG_LABEL_MOD
    KA_TRACE(0, STDOUT, 0, "ModifyLabelImplicitTaskEnd", "finish %s", modified->ToString().c_str());
#endif
    return modified;
}

LabelPtr
ModifyLabelAcquiredOrdered(LabelPtr& label)
{
    LabelCreateInfo create_info;
    create_info.to_copy = label;
    create_info.num_seg_no_copy = 0;    
    auto modified = make_shared<Label>(create_info);
    auto tail_ptr = static_cast<LabelSegmentByte16*>(modified->GetTail());  
    auto phase = GetPhase(tail_ptr->v);
    SetPhase(tail_ptr->v, phase+1); 
    return modified;
}

LabelPtr
ModifyLabelReleaseOrdered(LabelPtr& label)
{
    LabelCreateInfo create_info;
    create_info.to_copy = label;
    create_info.num_seg_no_copy = 0;    
    auto modified = make_shared<Label>(create_info);
    auto tail_ptr = static_cast<LabelSegmentByte16*>(modified->GetTail());  
    auto phase = GetPhase(tail_ptr->v);
    SetPhase(tail_ptr->v, phase+1); 
    return modified;
}

LabelPtr
ModifyParentLabelExplicitTaskFork(LabelPtr& parent_label)
{
    // increaes the task create count of original last segment  
    LabelCreateInfo create_info;
    create_info.to_copy = parent_label;
    create_info.num_seg_no_copy = 0;
    auto modified = make_shared<Label>(create_info);
    auto tail_ptr = static_cast<LabelSegmentByte16*>(modified->GetTail());
    auto task_create_cnt = GetTaskCreate(tail_ptr->v); 
    SetTaskCreate(tail_ptr->v, task_create_cnt+1);
    return modified;
}

LabelPtr
ModifyLabelTaskWait(LabelPtr& label)
{
    LabelCreateInfo create_info;
    create_info.to_copy = label;
    create_info.num_seg_no_copy = 0;
    auto modified = make_shared<Label>(create_info);
    auto tail_ptr = static_cast<LabelSegmentByte16*>(modified->GetTail());
    auto task_wait_cnt = GetTaskWait(tail_ptr->v); 
    SetTaskWait(tail_ptr->v, task_wait_cnt+1); //The new label has taskwait count incremented
    return modified;
}

inline void
ModifyTaskGroupEnter(TaskGroupPtr& tg)
{
    tg->age_count++;    
    tg->current_level++;     
    auto current_level = tg->current_level;
    tg->hash[current_level] += 1;    
    auto l = tg->hash[current_level]; 
    auto len_label = tg->label[0]; //the first number is the 
  //if (len_label >= TASKGROUP_MAX_DEPTH)  KA_TRACE(100, STDERR, 0, "TaskGroupBegin",  "warning: taskgroup nesting exceeds max depth\n", 0);
    tg->label[len_label + 1] = l;
    tg->label[0] = len_label + 1;
}

inline void
ModifyTaskGroupExit(TaskGroupPtr& tg)
{ 
    tg->age_count++;      
    auto cur_level = tg->current_level;
    assert(cur_level > 0);  
    // reset all hash values whose keys are larger than cur_level
    for (int i = cur_level + 1; i < TASKGROUP_MAX_DEPTH; ++i) {
        tg->hash[i] = 0; // reset the value    
    } 
    tg->current_level--;
    auto len_label = tg->label[0];
    tg->label[len_label] = 0; // pop the last value
    tg->label[0] = len_label - 1; // decrement the len label counter
}

LabelPtr
ModifyLabelTaskGroupBegin(LabelPtr& label)
{
    // if the current last label segment does not contain task group substructure, 
    // we should create one, if it already has the structure, we should build new one based on the current one. 
    // auto has_taskgroup = HasTaskGroup(tail_ptr->v);       
    void* raw_tail_ptr = label->GetTail();  
    auto tail_ptr = static_cast<LabelSegmentByte16*>(raw_tail_ptr); // get the last label segment to check the segment type
    auto last_segment_type = GetSegmentType(tail_ptr->v); 
    LabelCreateInfo create_info;
    create_info.to_copy = label;
    LabelPtr modified = nullptr;          
    TaskGroupPtr tg;    
    void* new_last_seg = nullptr; 
    LabelSegmentByte24* tmp_ptr_24;
    switch(last_segment_type) {
        case BYTE_16: // make a byte 32 segment (add the pointer to taskgroup)
            create_info.num_seg_no_copy = 1; // do not copy the last segment 
            tg = make_shared<TaskGroupInfo>();//create a task group structure
            // now modify the taskgroup substructure here we already know it should be a byte32 segment 
            ModifyTaskGroupEnter(tg);
            new_last_seg = (void*) new LabelSegmentByte32(*tail_ptr, tg);
            modified = make_shared<Label>(create_info);                          
            modified->Append(new_last_seg); // append the new last segment to the new label
            break;
        case BYTE_24: // make a byte 40 segment 
            create_info.num_seg_no_copy = 1;        
            tg = make_shared<TaskGroupInfo>();//create a task group structure
            // now modify the taskgroup substructure here we already know it should be a byte40 segment 
            ModifyTaskGroupEnter(tg);
            tmp_ptr_24 = static_cast<LabelSegmentByte24*>(raw_tail_ptr);
            new_last_seg = (void*) new LabelSegmentByte40(*tmp_ptr_24, tg);
            modified = make_shared<Label>(create_info);                          
            modified->Append(new_last_seg); // append the new last segment to the new label
            break;
        case BYTE_32: // create the modified version of task group structure
            create_info.num_seg_no_copy = 0;   
            modified = make_shared<Label>(create_info);                          
            // get the taskgroup pointer in the last segment   
            raw_tail_ptr = modified->GetTail();       
            tg = static_cast<LabelSegmentByte32*>(raw_tail_ptr)->taskgroup_ptr;
            ModifyTaskGroupEnter(tg);
            break;
        case BYTE_40: // create the modified version of task group structure
            create_info.num_seg_no_copy = 0; 
            modified = make_shared<Label>(create_info);                          
            // get the taskgroup pointer in the last segment   
            raw_tail_ptr = modified->GetTail();       
            tg = static_cast<LabelSegmentByte40*>(raw_tail_ptr)->taskgroup_ptr;
            ModifyTaskGroupEnter(tg);
            break; 
    }
    return modified;
}

LabelPtr
ModifyLabelTaskGroupEnd(LabelPtr& label)
{
    void* raw_tail_ptr = label->GetTail();  
    auto tail_ptr = static_cast<LabelSegmentByte16*>(raw_tail_ptr); // get the last label segment to check the segment type
    auto last_segment_type = GetSegmentType(tail_ptr->v); 
    assert(last_segment_type != BYTE_16 && last_segment_type != BYTE_24);
    LabelCreateInfo create_info;
    create_info.to_copy = label;
    create_info.num_seg_no_copy = 0;
    auto modified = make_shared<Label>(create_info);      
    raw_tail_ptr = modified->GetTail();
    if (last_segment_type == BYTE_32) {
        auto tg = static_cast<LabelSegmentByte32*>(raw_tail_ptr)->taskgroup_ptr;  
        ModifyTaskGroupExit(tg);
        return modified; 
    } 

    if (last_segment_type == BYTE_40) {
        auto tg = static_cast<LabelSegmentByte40*>(raw_tail_ptr)->taskgroup_ptr;  
        ModifyTaskGroupExit(tg);
        return modified; 
    }      
    assert(1); // should never reach here
    return nullptr;
}

bool SameTaskGroupStruct(TaskGroupPtr& t1, TaskGroupPtr& t2)
{
    // This is to check if every thing in t1 and t2 are the same .
    if (t1 == nullptr && t2 == nullptr) {
        return true;
    }      
    if (t1 == nullptr || t2 == nullptr) { 
        return false;     
    }
    //both are not nullptr
    for (int i = 0; i < TASKGROUP_MAX_DEPTH; ++i) {
        if (t1->hash[i] != t2->hash[i]) return false;
        if (t1->label[i] != t2->label[i]) return false;
    }
    return (t1->current_level == t2->current_level && 
            t1->age_count == t2->age_count);
}

bool SameTaskGroup(TaskGroupPtr& t1, TaskGroupPtr& t2)
{
    // assert(t1 and t2 are not both nullptr);
    if (t1 == nullptr || t2 == nullptr) { 
        return false;     
    }
    //both are not nullptr
    //here we compare all the values because beyond the label should be 0    
    //we only care about if t1 and t2 are at the same task group no matter if age_count match
    if (t1->label[0] == 0 && t2->label[0] == 0) {
        return true;
    } 

    for (int i = 0; i < TASKGROUP_MAX_DEPTH; ++i) {
        if (t1->label[i] != t2->label[i]) return false;
    }
    return true;
}

bool SameSegment(void* s1, void* s2)
{
    auto s1_seg16_ptr = static_cast<LabelSegmentByte16*>(s1);
    auto s2_seg16_ptr = static_cast<LabelSegmentByte16*>(s2);       
    if (s1_seg16_ptr->v != s2_seg16_ptr->v) return false; // if the v value is not the same, not same value 
    auto s1_type = GetSegmentType(s1_seg16_ptr->v);
    auto s2_type = GetSegmentType(s2_seg16_ptr->v);      
    if (s1_type != s2_type) {
#ifdef PERFORMANCE_INSPECT
        KA_TRACE(0, STDOUT, 0, "PERF ", "segment type not same: %d, %d", s1_type, s2_type);
#endif
        return false;
    }
    switch(s1_type) {
        case BYTE_16:
            return true;
        case BYTE_24:
            return (static_cast<LabelSegmentByte24*>(s1))->iter == (static_cast<LabelSegmentByte24*>(s2))->iter;
        case BYTE_32:
            return SameTaskGroupStruct(
                    (static_cast<LabelSegmentByte32*>(s1))->taskgroup_ptr, 
                    (static_cast<LabelSegmentByte32*>(s2))->taskgroup_ptr); 
        case BYTE_40:
            return (SameTaskGroupStruct(
                    (static_cast<LabelSegmentByte40*>(s1))->taskgroup_ptr, 
                    (static_cast<LabelSegmentByte40*>(s2))->taskgroup_ptr) &&
                    (static_cast<LabelSegmentByte40*>(s1))->iter == (static_cast<LabelSegmentByte40*>(s2))->iter);
    } 
    return false;
} 
