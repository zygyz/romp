//This is the original implemenation of the algorithm dated back to berkeley time. For reference only

static Direction 
HappensBefore(LabelPtr& from, LabelPtr& to)
{
    //KA_TRACE(100, STDERR, 0, "HappensBefore", "start ", 0); 
    int p_from = 0;
    int p_to = 0;
    int len_from = from->label_segments.size(); // the number of lable segments in a label
    int len_to = to->label_segments.size();  
    while(p_from < len_from && p_to < len_to && 
            SameSegment(from->label_segments.at(p_from), to->label_segments.at(p_to))) {
        p_from++;
        p_to++; 
    }
   // KA_TRACE(100, STDERR, 0, "HappensBefore", "p_from: %d p_to: %d, len_from: %d len_to: %d", p_from, p_to, len_from, len_to); 
    if (p_from == len_from && p_to <= len_to - 1) { // label 'from' is the prefix of label 'to'
        return LEFT_TO_RIGHT;
    }
    if (p_to == len_to && p_from <= len_from -1) { // label 'to' is the prefix of label 'from'
        return RIGHT_TO_LEFT;
    }
    if (p_from == len_from && p_to == len_to) { // the same label. 
        return SAME;
    }
    auto from_segment = from->label_segments.at(p_from);
    auto to_segment = to->label_segments.at(p_to);
    auto direction = ERROR;
    if (from_segment.type == IMPLICIT && to_segment.type == IMPLICIT || 
        from_segment.type == EXPLICIT && to_segment.type == EXPLICIT &&  from_segment.taskid == to_segment.taskid) {
        if (from_segment.taskid == to_segment.taskid) {
            if (p_from == len_from - 1 && p_to == len_to - 1) { // the label segment after is null
#ifdef VERBOSE 
                KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check00", 0); 
#endif
                direction = Check00(from, to, p_from);  
            } else if (p_from < len_from - 1 && p_to < len_to -1) {// both have next segment
                auto from_next_segment = from->label_segments.at(p_from + 1);
                auto to_next_segment = to->label_segments.at(p_to + 1);
                auto from_next_type = from_next_segment.type;
                auto to_next_type = to_next_segment.type;  
                switch(from_next_type) {
                    case IMPLICIT:
                        switch(to_next_type) {
                            case IMPLICIT:
#ifdef VERBOSE 
                                KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check01", 0); 
#endif
                                direction = Check01(from, to, p_from); 
                                break;
                            case EXPLICIT:
#ifdef VERBOSE 
                                KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check03", 0); 
#endif
                                direction = Check03(from, to, p_from);
                                break;
                            case LOGICAL:
#ifdef VERBOSE 
                                KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check07 reverse", 0); 
#endif
                                direction = Check07(to, from, p_from);
                                direction = Reverse(direction);
                                break; 
                        }    
                        break;
                    case EXPLICIT:
                        switch(to_next_type) {
                            case IMPLICIT:
#ifdef VERBOSE 
                                KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check03 reverse", 0); 
#endif
                                direction = Check03(to, from, p_from);
                                direction = Reverse(direction);//because here left->right means to->from 
                                break;
                            case EXPLICIT:
#ifdef VERBOSE 
                                KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check02", 0); 
#endif
                                direction = Check02(from, to, p_from);
                                break;
                            case LOGICAL:
#ifdef VERBOSE 
                                KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check09 reverse", 0); 
#endif
                                direction = Check09(to, from, p_from);
                                direction = Reverse(direction);
                                break; 
                        }    
                        break;
                    case LOGICAL:
                        switch(to_next_type) {
                            case IMPLICIT:
#ifdef VERBOSE 
                                KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check08", 0); 
#endif
                                direction = Check08(from, to, p_from);         
                                break;
                            case EXPLICIT:
#ifdef VERBOSE 
                                KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check09", 0); 
#endif
                                direction = Check09(from, to, p_from); 
                                break;
                            case LOGICAL:
#ifdef VERBOSE 
                                KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check07", 0); 
#endif
                                direction = Check07(from, to, p_from);         
                                break; 
                        }    
                        break;
                    default:
                        break;
                }
            } else if (p_from == len_from - 1 && p_to < len_to - 1) { // L[k+1] is null, L'[k+1] is not null
                auto to_next_segment = to->label_segments.at(p_to + 1); 
                auto to_next_type = to_next_segment.type;
                switch(to_next_type) {
                    case EXPLICIT:
#ifdef VERBOSE 
                        KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check04", 0); 
#endif
                        direction = Check04(from, to, p_from);          
                        break;
                    case IMPLICIT:
#ifdef VERBOSE 
                        KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check05", 0); 
#endif
                        direction = Check05(from, to, p_from);
                        break;
                    case LOGICAL:
#ifdef VERBOSE 
                        KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check06", 0); 
#endif
                        direction = Check06(from, to, p_from);
                        break;    
                } 
            } else if (p_to == len_to - 1 && p_from < len_from - 1) { // L[k+1] is not null, L'[k+1] is null
                auto from_next_segment = from->label_segments.at(p_from + 1);
                auto from_next_type = from_next_segment.type; 
                switch(from_next_type) {
                    case EXPLICIT:
#ifdef VERBOSE 
                        KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check04 reverse", 0); 
#endif
                        direction = Check04(to, from, p_to);
                        break;
                    case IMPLICIT:
#ifdef VERBOSE 
                        KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check05 reverse", 0); 
#endif
                        direction = Check05(to, from, p_to); 
                        break;
                    case LOGICAL:
#ifdef VERBOSE 
                        KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check06 reverse", 0); 
#endif
                        direction = Check06(to, from, p_to);
                        break;    
                } 
                direction = Reverse(direction);
            }
        } else { // L[k].task_id != L'[k].task_id
#ifdef VERBOSE 
            KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check10", 0); 
#endif
            direction = Check10(from, to, p_from);  
        }
    } else if (from_segment.type == LOGICAL && to_segment.type == LOGICAL) {
#ifdef VERBOSE 
        KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check11", 0); 
#endif
        direction = Check11(from, to, p_from);   
    } else {
#ifdef VERBOSE 
        KA_TRACE(0, STDERR, 0, "HappensBefore", "Error", 0); 
#endif
        return ERROR;
    }
#ifdef VERBOSE 
    KA_TRACE(100, STDERR, 0, "HappensBefore", "end", 0); 
#endif
    return direction;
} 

static inline Direction 
Check00(LabelPtr& from, LabelPtr& to, int k)
{
  //  KA_TRACE(100, STDOUT, 0, "Check00", "called", 0); 
    auto from_seg = from->label_segments.at(k);
    auto to_seg = to->label_segments.at(k);    
    //KA_TRACE(0, STDOUT, 0, "Check00", "--0", 0); 
    if (from_seg.offset < to_seg.offset) {
        return LEFT_TO_RIGHT;
    } else if (from_seg.offset > to_seg.offset) {
        return RIGHT_TO_LEFT; 
    } else {
        //KA_TRACE(0, STDOUT, 0, "Check00", "--1", 0); 
        auto from_tg = from_seg.taskgroup_ptr;
        auto to_tg = to_seg.taskgroup_ptr;
        if (SameTaskGroup(from_tg, to_tg)) {
            if (from_seg.loop_cnt >= to_seg.loop_cnt && 
                from_seg.taskwait_cnt >= to_seg.taskwait_cnt &&
                from_seg.taskcreate_cnt >= to_seg.taskcreate_cnt) {
                return RIGHT_TO_LEFT;
            } else if (to_seg.loop_cnt >= from_seg.loop_cnt && 
                       to_seg.taskwait_cnt >= from_seg.taskwait_cnt &&
                       to_seg.taskcreate_cnt >= from_seg.taskcreate_cnt) {
                return LEFT_TO_RIGHT;   
            } else {
#ifdef SHOW_ERROR 
                KA_TRACE(0, STDERR, 0, "check 00 ", " error", 0);
#endif
                return ERROR; 
            }
        }  
        if (from_tg == nullptr) {
            return LEFT_TO_RIGHT;
        }   
        if (to_tg == nullptr) {
            return RIGHT_TO_LEFT;
        }
        return OrderedByTaskGroup(from_tg, to_tg);    
    }
    return ERROR;
}

static inline Direction
Check01(LabelPtr& from, LabelPtr& to, int k) 
{
    auto from_seg = from->label_segments.at(k);
    auto to_seg = to->label_segments.at(k);    
    if (from_seg.offset < to_seg.offset) {
        return LEFT_TO_RIGHT;
    } else if (from_seg.offset > to_seg.offset) {
        return RIGHT_TO_LEFT;
    } else {
#ifdef SHOW_ERROR 
        KA_TRACE(0, STDERR, 0, "check 01 ", " error", 0);
#endif
        return ERROR;
    }
}

static inline void
DoAffinityCheck() 
{
    KA_TRACE(0, STDOUT, 0, "DoAffinityCheck", " ", 0);
}

static inline bool
CheckTaskWaitChain(LabelPtr& l_sw, int k_tw)
{
    int total_label_length = l_sw->label_segments.size();     
    if (k_tw == total_label_length - 1) { // it is already the last label segment, so task wait should take effect
        // no next       
        return true;
    }   
    auto s_current = l_sw->label_segments.at(k_tw);
    auto s_next = l_sw->label_segments.at(k_tw + 1);
    int i = k_tw + 1;
    while (s_next.type == EXPLICIT) {
        if (s_current.taskgroup_ptr != nullptr  &&  !IsVoidTaskGroupLabel(s_current.taskgroup_ptr)) {
            return true;
        }
        int current_task_id = s_current.taskid;
        auto type = s_current.type;
        if (label_list[current_task_id] == nullptr) {
#ifdef SHOW_ERROR 
            KA_TRACE(0, STDERR, current_task_id, "CheckTaskWaitChain", "cannot find current task in task map",0);
#endif
            return false;
        } 
        auto current_label = label_list[current_task_id]; //get the most recent task label of explicit task
        // if the explicit task chain is chained taskwaited, should have already seen the taskwait  
        auto last_seg = current_label->label_segments.back();
        if (last_seg.type != EXPLICIT) {
#ifdef SHOW_ERROR 
            KA_TRACE(0, STDERR, current_task_id, "CheckTaskWaitChain", "the latest last segment is not explicit",0);
#endif
            return false;
        }
        if (last_seg.taskwait_cnt > s_current.taskwait_cnt) {
            s_current = s_next; 
            if (i == total_label_length - 1)  { // it is already the last label segment of l_sw
                break;
            }
            s_next = l_sw->label_segments.at(i+1); 
            i++;        
        } else {
            return false;
        }      
    }
    return true;
}

static inline Direction
CheckTaskWait(LabelPtr& from, LabelPtr& to, int k)
{
#ifdef SHOW_ERROR 
    KA_TRACE(0, STDOUT, 0, "CheckTaskWait", "called",0);
#endif
    auto from_seg = from->label_segments.at(k);
    auto to_seg = to->label_segments.at(k);    
    bool lw_from = true;
    LabelPtr l_lw = nullptr;
    LabelPtr l_sw = nullptr;    
   
    if (from_seg.taskwait_cnt == to_seg.taskwait_cnt) {
#ifdef SHOW_ERROR 
        KA_TRACE(0, STDOUT, 0, "CheckTaskWait", "called",0);
#endif
        return PARALLEL;
    } else if (from_seg.taskwait_cnt > to_seg.taskwait_cnt) {
        l_lw = from;
        l_sw = to;
    } else {
        l_lw = to;
        l_sw = from;
        lw_from = false;
    }
    if (l_sw->label_segments.size() - 1 == k || l_sw->label_segments.at(k + 1).type == IMPLICIT) {
#ifdef SHOW_ERROR 
        KA_TRACE(0, STDERR, 0, "CheckTaskWait", " error", 0);
#endif
        return ERROR;
    }
    int k_tw = 0;
    if (l_sw->label_segments.at(k + 1).type == EXPLICIT) {
    //    auto s_current = l_sw->label_segments.at(k + 1); 
        k_tw = k + 1;
    } else if (l_sw->label_segments.at(k + 1).type == LOGICAL) {
        if (l_sw->label_segments.size() - 1 <= k + 1 || l_sw->label_segments.at(k + 2).type == IMPLICIT)  {  // l_sw[k+2] = null or l_sw[k+2] == implicit
           // DoAffinityCheck();         
            if (lw_from) {
                return RIGHT_TO_LEFT;
            } else {
                return LEFT_TO_RIGHT;
            }
        }
        //auto s_current = l_sw->label_segments.at(k + 2);      
        k_tw = k + 2;// check the deeper level, label_segments.at(k+2) should be explicit task. 
    }    
    if (CheckTaskWaitChain(l_sw, k_tw)) {
        if (l_sw->label_segments.at(k + 1).type == LOGICAL) {
            DoAffinityCheck();
        }
        if (lw_from) {
            return RIGHT_TO_LEFT;
        }  else {
            return LEFT_TO_RIGHT;
        }
    }
    return PARALLEL;
}

static inline Direction
CheckTaskGroup(LabelPtr& from, LabelPtr& to, int k) 
{
#ifdef SHOW_ERROR 
    KA_TRACE(0, STDOUT, 0, "CheckTaskGroup", "called", 0);
#endif
    auto from_seg = from->label_segments.at(k);
    auto to_seg = to->label_segments.at(k);    
    if (SameTaskGroup(from_seg.taskgroup_ptr, to_seg.taskgroup_ptr)) {
#ifdef SHOW_ERROR 
        KA_TRACE(0, STDOUT, 0, "CheckTaskGroup", "same task group", 0);
#endif
        return CheckTaskWait(from, to, k);  
    }
    bool from_is_short = false;
    auto from_tg = from_seg.taskgroup_ptr;
    auto to_tg = to_seg.taskgroup_ptr; 
    TaskGroupPtr s_longer = nullptr;
    TaskGroupPtr s_shorter = nullptr;      
    if (from_tg->label[0] < to_tg->label[0]) {
        s_longer = to_tg;  
        s_shorter = from_tg;   
        from_is_short = true;
     } else {
        s_longer = from_tg;
        s_shorter = to_tg;
     }
     if (IsTaskGroupPrefix(s_shorter, s_longer)) {
        //KA_TRACE(0, STDOUT, 0, "CheckTaskGroup", "is task group prefix", 0);
        if (from_is_short) {
            //KA_TRACE(0, STDOUT, 0, "CheckTaskGroup", "from is short", 0);
            if (from_seg.taskcreate_cnt <= to_seg.taskcreate_cnt) {
                return CheckTaskWait(from, to, k);  
            } else {
                return RIGHT_TO_LEFT;
            }
        }  else { // to is short, from is longer
         //   KA_TRACE(0, STDOUT, 0, "CheckTaskGroup", "to is short", 0);
            if (to_seg.taskcreate_cnt <= from_seg.taskcreate_cnt) {
                return CheckTaskWait(from, to, k);
            } else {
                return LEFT_TO_RIGHT;
            }  
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
#ifdef SHOW_ERROR 
    KA_TRACE(0, STDERR, 0, "CheckTaskGroup", " error", 0);
#endif
    return ERROR;
}

static inline Direction
Check02(LabelPtr& from, LabelPtr& to, int k)
{
    return CheckTaskGroup(from, to, k); 
}

static inline Direction
Check03(LabelPtr& from, LabelPtr& to, int k)
{
    auto from_seg = from->label_segments.at(k);
    auto to_seg = to->label_segments.at(k);    
    if (from_seg.offset < to_seg.offset) {
        return LEFT_TO_RIGHT;  
    } else {
        return CheckTaskGroup(from, to, k);
    }
}

static inline Direction
Check04(LabelPtr& from, LabelPtr& to, int k)
{
#ifdef SHOW_ERROR 
    KA_TRACE(0, STDOUT, 0, "Check04", "called", 0);
#endif
    auto from_seg = from->label_segments.at(k);
    auto to_seg = to->label_segments.at(k);    
    if (from_seg.taskcreate_cnt <= to_seg.taskcreate_cnt) {
        return LEFT_TO_RIGHT;     
    } else {
        return CheckTaskGroup(from, to, k);
    }
}

static inline Direction
Check05(LabelPtr& from, LabelPtr& to, int k)
{
    auto from_seg = from->label_segments.at(k);
    auto to_seg = to->label_segments.at(k);    
    if (from_seg.offset <= to_seg.offset) {
        return LEFT_TO_RIGHT;
    } else {
        return RIGHT_TO_LEFT;
    }
}

static inline Direction
Check06(LabelPtr& from, LabelPtr& to, int k)
{
    auto from_seg = from->label_segments.at(k);
    auto to_seg = to->label_segments.at(k);    
    if (from_seg.offset < to_seg.offset) {
        return LEFT_TO_RIGHT; 
    } 
    if (from_seg.loop_cnt <= to_seg.loop_cnt) {
        return LEFT_TO_RIGHT;
    }
    int len_to = to->label_segments.size();
    if (k + 2 >= len_to || to->label_segments.at(k + 2).type == IMPLICIT) {  
        return RIGHT_TO_LEFT;   
    }
    /*
    if (from_seg.taskgroup_ptr && !(IsVoidTaskGroupLabel(from_seg.taskgroup_ptr))) { // this part is not correct 
        return RIGHT_TO_LEFT;  
    }
    */
    if (to_seg.taskgroup_ptr && !(IsVoidTaskGroupLabel(to_seg.taskgroup_ptr))) {
        return RIGHT_TO_LEFT;
    } 
    if (CheckTaskWaitChain(to, k + 2)) {
        if (to->label_segments.at(k + 1).type == LOGICAL)  {
#ifdef SHOW_ERROR 
            KA_TRACE(0, STDERR, 0, "Check06", "should not be logical",0);
#endif
            return ERROR;
        }
        auto taskid = to->label_segments.at(k + 1).taskid; // this segment should not be logical
        if (label_list[taskid] == nullptr) {
#ifdef SHOW_ERROR 
            KA_TRACE(0, STDERR, 0, "Check06", "cannot find current task in task map",0);
#endif
            return ERROR;
        } 
        auto latest_label = label_list[taskid]; 
        if (latest_label->label_segments.back().taskwait_cnt > to->label_segments.at(k).taskwait_cnt) {
            return RIGHT_TO_LEFT;
        }
    } else {
        return PARALLEL;
    }
    return ERROR;  
}

static inline int 
ExitRank(int phase)
{
    if (phase % 2 == 1) {
        return phase - 1;
    } 
    return phase;

}

static inline int
EnterRank(int phase)
{
    if (phase % 2 == 1) {
        return phase + 1;
    } 
    return phase;
}

static inline bool
InFinish(LabelPtr& label, int k)
{  
    //KA_TRACE(0, STDOUT, 0, "InFinish", "star k = %d label size: %d",k, label->label_segments.size());
    if ((k + 1) >= label->label_segments.size() || 
            label->label_segments.at(k + 1).type == IMPLICIT) {
        return true; 
    }
    if (label->label_segments.at(k).taskgroup_ptr && 
          !IsVoidTaskGroupLabel(label->label_segments.at(k).taskgroup_ptr)) {
        return true;
    } 
    if (CheckTaskWaitChain(label,  k + 1)) {
        auto taskid = label->label_segments.at(k).taskid;
        LabelPtr latest_label  = nullptr;
        if (taskid < 10000) {
            if (label_list[taskid] == nullptr) {
#ifdef SHOW_ERROR 
                KA_TRACE(0, STDERR, taskid, "InFinish", "cannot find task %d", taskid);
#endif
                return false;
            } else {
                latest_label = label_list[taskid];
            }
        } else {
            if (logical_label_list[taskid-10000]  == nullptr) {
#ifdef SHOW_ERROR 
                KA_TRACE(0, STDERR, taskid, "InFinish", "cannot find task %d", taskid);
#endif
                return false;
            } else {
                latest_label = logical_label_list[taskid-10000];
            }
        }
        if (latest_label->label_segments.back().taskwait_cnt > 
            label->label_segments.at(k).taskwait_cnt) {
            return true;
        } else {
            return false;
        }
    }
    return false;
}


static inline CheckOrderedSection(LabelPtr& from, LabelPtr& to, int k)
{
    //KA_TRACE(0, STDERR, 0, "CheckOrderedSection", "k = %d from: %s to: %s",k, from?"yes":"no", to?"yes":"no");
    auto from_seg = from->label_segments.at(k);
    auto to_seg = to->label_segments.at(k);    
    bool left_is_from = true;
    LabelSegment s_left, s_right;
    LabelPtr l_left, l_right;
   // KA_TRACE(0, STDERR, 0, "CheckOrderedSection", "from iter: %d to iter: %d", from_seg.iter, to_seg.iter); 
    if (from_seg.iter < to_seg.iter) {
        s_left = from_seg;
        s_right = to_seg;
        l_left = from;
        l_right = to;
    } else {
        s_left = to_seg;
        s_right = from_seg; 
        l_left = to;
        l_right = from;    
        left_is_from = false;
    } 
   // KA_TRACE(0, STDERR, 0, "CheckOrderedSection", "left exit rank: %d right enter rank: %d", 
    //        ExitRank(s_left.phase), EnterRank(s_right.phase));
    if (ExitRank(s_left.phase) < EnterRank(s_right.phase)) {
        if (InFinish(l_left, k) && InFinish(l_right, k)) {
            if (left_is_from) {
                return LEFT_TO_RIGHT;  
            } else {
                return RIGHT_TO_LEFT;  
            }
        }
    }    
    //KA_TRACE(0, STDERR, 0, "CheckOrderedSection", "logical parallel",0);
    return PARALLEL_WORKSHARE;
}

static inline Direction
Check07(LabelPtr& from, LabelPtr& to, int k)
{
    auto from_seg = from->label_segments.at(k);
    auto to_seg = to->label_segments.at(k);    
    bool l_s_from = false;
    LabelPtr l_s, l_l;
    if (from_seg.loop_cnt == to_seg.loop_cnt) {
        return CheckOrderedSection(from, to, k + 1);
    } else if (from_seg.loop_cnt > to_seg.loop_cnt) {
        l_s = to;
        l_l = from;
    } else {
        l_s = from;
        l_l = to;
        l_s_from = true; 
    }      
    int len_ls = l_s->label_segments.size();    
    int len_ll = l_l->label_segments.size();
    // here k+1 is logical task 
    if (InFinish(l_s, k + 1)) {
        if (l_s_from) {
            return LEFT_TO_RIGHT;
        } else {
            return RIGHT_TO_LEFT;
        }
    }
    // l_s[k] is not in finish. This means that l_s[k+1] is explicit task.
    // Here check if task group contains the whole workshare loop. 
    auto ls_tg = l_s->label_segments.at(k).taskgroup_ptr; 
    auto ll_tg = l_l->label_segments.at(k).taskgroup_ptr;      
    // we wan to see if taskgroup that covers the workshare loop implies order.
    if (ls_tg == nullptr) { // the previosu one not covered by takgroup
        return PARALLEL;         
    } 
    // at this stage, ll_tg should not be null because ls_tg is not null
    // we want to check if the two workshare are ordered by nested taskgroup
    return  OrderedByTaskGroup(ls_tg, ll_tg);  
} 


static inline Direction
Check07(LabelPtr& from, LabelPtr& to, int k)
{
    auto from_seg = from->label_segments.at(k);
    auto to_seg = to->label_segments.at(k);    
    bool l_s_from = false;
    LabelPtr l_s, l_l;
    if (from_seg.loop_cnt == to_seg.loop_cnt) {
        return CheckOrderedSection(from, to, k + 1);
    } else if (from_seg.loop_cnt > to_seg.loop_cnt) {
        l_s = to;
        l_l = from;
    } else {
        l_s = from;
        l_l = to;
        l_s_from = true; 
    }      
    int len_ls = l_s->label_segments.size();    
    int len_ll = l_l->label_segments.size();
    if (k + 2 >= len_ls || l_s->label_segments.at(k + 2).type == IMPLICIT) {
        //DoAffinityCheck();
        if (l_s_from) {
            return LEFT_TO_RIGHT;
        } else {
            return RIGHT_TO_LEFT;
        }
    }
    if (l_s->label_segments.at(k).taskgroup_ptr && !IsVoidTaskGroupLabel(l_s->label_segments.at(k).taskgroup_ptr)) {
        //DoAffinityCheck();
        if (l_s_from) {
            return LEFT_TO_RIGHT;       
        } else {
            return RIGHT_TO_LEFT;
        }
    }
    if (l_s->label_segments.at(k + 1).taskgroup_ptr && !IsVoidTaskGroupLabel(l_s->label_segments.at(k + 1).taskgroup_ptr)) {
        //DoAffinityCheck();
        if (l_s_from) {
            return LEFT_TO_RIGHT;
        } else {
            return RIGHT_TO_LEFT;  
        }
    }
    if (CheckTaskWaitChain(l_s, k + 2)) {
        auto taskid = l_s->label_segments.at(k + 1).taskid;    
        if (label_list[taskid] == nullptr) {
            KA_TRACE(0, STDERR, taskid, "Check07", "cannot find the task in taskmap", 0);
            return ERROR;
        }  
        auto latest_label = label_list[taskid];
        if (latest_label->label_segments.back().taskwait_cnt > 
                l_s->label_segments.at(k + 1).taskwait_cnt) {
            //DoAffinityCheck();
            if (l_s_from) {
                return LEFT_TO_RIGHT;
            } else {
                return RIGHT_TO_LEFT;   
            }
        } else {
           return PARALLEL; 
        }    
    } else {
        return PARALLEL;
    }
} 

static inline Direction
Check08(LabelPtr& from, LabelPtr& to, int k)
{
    auto from_seg = from->label_segments.at(k);
    auto to_seg = to->label_segments.at(k);    
    if (to_seg.offset < from_seg.offset) {
        //DoAffinityCheck();
        return RIGHT_TO_LEFT;
    }
    if (from_seg.offset < to_seg.offset) {
        //DoAffinityCheck();
        return LEFT_TO_RIGHT;
    } 
    if (k + 2 >= from->label_segments.size() || 
            from->label_segments.at(k + 2).type == IMPLICIT) {
       //DoAffinityCheck();
        return LEFT_TO_RIGHT;
    }
    if (from_seg.taskgroup_ptr && !IsVoidTaskGroupLabel(from_seg.taskgroup_ptr)) {
        //DoAffinityCheck();
        return LEFT_TO_RIGHT;
    }
    auto next_from = from->label_segments.at(k + 1);
    if (next_from.taskgroup_ptr && !IsVoidTaskGroupLabel(next_from.taskgroup_ptr)) {
        //DoAffinityCheck();
        return LEFT_TO_RIGHT;
    }
    if (CheckTaskWaitChain(from, k + 3)) {
        auto taskid = from->label_segments.at(k + 1).taskid;
        LabelPtr latest_label;
        if (taskid >= 10000) {
            if (logical_label_list[taskid-10000] == nullptr) {
#ifdef SHOW_ERROR 
                KA_TRACE(0, STDERR, taskid, "Check08", "cannot find current task in task map",0);
#endif
                return ERROR;
            } else {
                latest_label = logical_label_list[taskid-10000];     
            }
        }  else {
            if (label_list[taskid] == nullptr) {
#ifdef SHOW_ERROR 
                KA_TRACE(0, STDERR, taskid, "Check08", "cannot find current task in task map",0);
#endif
                return ERROR;
            } else {
                latest_label=  label_list[taskid];
            }
        } 
        if (latest_label->label_segments.back().taskwait_cnt > from->label_segments.at(k + 1).taskwait_cnt) {
            //DoAffinityCheck();
            return LEFT_TO_RIGHT;    
        } else {
            return PARALLEL;
        }
    } else {
        return PARALLEL;
    }  
}

static inline Direction
Check09(LabelPtr& from, LabelPtr& to, int k) {
    auto from_seg = from->label_segments.at(k);
    auto to_seg = to->label_segments.at(k);    
    if (from_seg.taskcreate_cnt <= to_seg.taskcreate_cnt) {
        if (k + 2 >= from->label_segments.size() || from->label_segments.at(k + 2).type == IMPLICIT) {
           // DoAffinityCheck();
            return LEFT_TO_RIGHT;
        }
        if (from_seg.taskgroup_ptr != nullptr && !IsVoidTaskGroupLabel(from_seg.taskgroup_ptr)) {
            //DoAffinityCheck();
            return LEFT_TO_RIGHT;
        }
        auto next_from = from->label_segments.at(k + 1);
        if (next_from.taskgroup_ptr && !IsVoidTaskGroupLabel(next_from.taskgroup_ptr)) {
            //DoAffinityCheck();
            return LEFT_TO_RIGHT;
        }
        if (CheckTaskWaitChain(from, k + 2)) {
            auto taskid = from->label_segments.at(k + 1).taskid;
            if (label_list[taskid] == nullptr) {
#ifdef SHOW_ERROR 
                KA_TRACE(0, STDERR, taskid, "Check09", "cannot find current task in task map",0);
#endif
                return ERROR;
            } 
            auto latest_label = label_list[taskid]; 
            if (latest_label->label_segments.back().taskwait_cnt > from->label_segments.at(k + 1).taskwait_cnt) {
               // DoAffinityCheck();
                return LEFT_TO_RIGHT;    
            } else {
                return PARALLEL;
            }
        } else {
            return PARALLEL;
        }  
    } else {
        return CheckTaskGroup(from, to, k);
    }
}

static inline Direction
Check10(LabelPtr& from, LabelPtr& to, int k)
{
    if (k + 1 < from->label_segments.size() && k + 1 < to->label_segments.size() &&
          from->label_segments.at(k + 1).type == LOGICAL && to->label_segments.at(k + 1).type == LOGICAL) {  
        if (from->label_segments.at(k).loop_cnt == to->label_segments.at(k).loop_cnt) {
            return CheckOrderedSection(from, to, k + 1);
        } else {
    //        KA_TRACE(0, STDOUT , taskid, "Check10", "return parallel",0);
            return PARALLEL;
        } 
    }  
    //KA_TRACE(0, STDOUT , taskid, "Check10", "return parallel",0);
    return PARALLEL;
}

static inline Direction
Check11(LabelPtr& from, LabelPtr& to, int k)
{
   //L[k] is logical, L'[k] is logical 
    if (from == nullptr || to == nullptr) {
        KA_TRACE(0, STDERR, 0, "Check11", "from or to is null",0);
    }
    auto from_seg = from->label_segments.at(k);
    auto to_seg = to->label_segments.at(k);    
    if (from_seg.loop_cnt != to_seg.loop_cnt) {
#ifdef SHOW_ERROR 
        KA_TRACE(0, STDERR, 0, "Check11", "loop cnt not the same",0);
#endif
        return ERROR;   
    } 
    return CheckOrderedSection(from, to, k);
}

static inline Direction
Reverse(Direction direction)
{
    if (direction == LEFT_TO_RIGHT) {
        return RIGHT_TO_LEFT;
    } else if (direction == RIGHT_TO_LEFT) {
        return LEFT_TO_RIGHT;
    } else {
        return direction;
    }
}

static Direction 
HappensBefore(LabelPtr& from, LabelPtr& to)
{
    //KA_TRACE(100, STDERR, 0, "HappensBefore", "start ", 0); 
    int p_from = 0;
    int p_to = 0;
    int len_from = from->label_segments.size(); // the number of lable segments in a label
    int len_to = to->label_segments.size();  
    while(p_from < len_from && p_to < len_to && 
            SameSegment(from->label_segments.at(p_from), to->label_segments.at(p_to))) {
        p_from++;
        p_to++; 
    }
   // KA_TRACE(100, STDERR, 0, "HappensBefore", "p_from: %d p_to: %d, len_from: %d len_to: %d", p_from, p_to, len_from, len_to); 
    if (p_from == len_from && p_to <= len_to - 1) { // label 'from' is the prefix of label 'to'
        return LEFT_TO_RIGHT;
    }
    if (p_to == len_to && p_from <= len_from -1) { // label 'to' is the prefix of label 'from'
        return RIGHT_TO_LEFT;
    }
    if (p_from == len_from && p_to == len_to) { // the same label. 
        return SAME;
    }
    //KA_TRACE(0, STDERR, 0, "HappensBefore", "p_from: %d p_to: %d, len_from: %d len_to: %d reached here ", p_from, p_to, len_from, len_to); 
    //  now p_from, p_to are the first different label segments' indexes
    auto from_segment = from->label_segments.at(p_from);
    auto to_segment = to->label_segments.at(p_to);
    auto direction = ERROR;
    if (from_segment.type == IMPLICIT && to_segment.type == IMPLICIT || 
        from_segment.type == EXPLICIT && to_segment.type == EXPLICIT &&  from_segment.taskid == to_segment.taskid) {
        if (from_segment.taskid == to_segment.taskid) {
            if (p_from == len_from - 1 && p_to == len_to - 1) { // the label segment after is null
#ifdef VERBOSE 
                KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check00", 0); 
#endif
                direction = Check00(from, to, p_from);  
            } else if (p_from < len_from - 1 && p_to < len_to -1) {// both have next segment
                auto from_next_segment = from->label_segments.at(p_from + 1);
                auto to_next_segment = to->label_segments.at(p_to + 1);
                auto from_next_type = from_next_segment.type;
                auto to_next_type = to_next_segment.type;  
                switch(from_next_type) {
                    case IMPLICIT:
                        switch(to_next_type) {
                            case IMPLICIT:
#ifdef VERBOSE 
                                KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check01", 0); 
#endif
                                direction = Check01(from, to, p_from); 
                                break;
                            case EXPLICIT:
#ifdef VERBOSE 
                                KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check03", 0); 
#endif
                                direction = Check03(from, to, p_from);
                                break;
                            case LOGICAL:
#ifdef VERBOSE 
                                KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check07 reverse", 0); 
#endif
                                direction = Check07(to, from, p_from);
                                direction = Reverse(direction);
                                break; 
                        }    
                        break;
                    case EXPLICIT:
                        switch(to_next_type) {
                            case IMPLICIT:
#ifdef VERBOSE 
                                KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check03 reverse", 0); 
#endif
                                direction = Check03(to, from, p_from);
                                direction = Reverse(direction);//because here left->right means to->from 
                                break;
                            case EXPLICIT:
#ifdef VERBOSE 
                                KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check02", 0); 
#endif
                                direction = Check02(from, to, p_from);
                                break;
                            case LOGICAL:
#ifdef VERBOSE 
                                KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check09 reverse", 0); 
#endif
                                direction = Check09(to, from, p_from);
                                direction = Reverse(direction);
                                break; 
                        }    
                        break;
                    case LOGICAL:
                        switch(to_next_type) {
                            case IMPLICIT:
#ifdef VERBOSE 
                                KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check08", 0); 
#endif
                                direction = Check08(from, to, p_from);         
                                break;
                            case EXPLICIT:
#ifdef VERBOSE 
                                KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check09", 0); 
#endif
                                direction = Check09(from, to, p_from); 
                                break;
                            case LOGICAL:
#ifdef VERBOSE 
                                KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check07", 0); 
#endif
                                direction = Check07(from, to, p_from);         
                                break; 
                        }    
                        break;
                    default:
                        break;
                }
            } else if (p_from == len_from - 1 && p_to < len_to - 1) { // L[k+1] is null, L'[k+1] is not null
                auto to_next_segment = to->label_segments.at(p_to + 1); 
                auto to_next_type = to_next_segment.type;
                switch(to_next_type) {
                    case EXPLICIT:
#ifdef VERBOSE 
                        KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check04", 0); 
#endif
                        direction = Check04(from, to, p_from);          
                        break;
                    case IMPLICIT:
#ifdef VERBOSE 
                        KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check05", 0); 
#endif
                        direction = Check05(from, to, p_from);
                        break;
                    case LOGICAL:
#ifdef VERBOSE 
                        KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check06", 0); 
#endif
                        direction = Check06(from, to, p_from);
                        break;    
                } 
            } else if (p_to == len_to - 1 && p_from < len_from - 1) { // L[k+1] is not null, L'[k+1] is null
                auto from_next_segment = from->label_segments.at(p_from + 1);
                auto from_next_type = from_next_segment.type; 
                switch(from_next_type) {
                    case EXPLICIT:
#ifdef VERBOSE 
                        KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check04 reverse", 0); 
#endif
                        direction = Check04(to, from, p_to);
                        break;
                    case IMPLICIT:
#ifdef VERBOSE 
                        KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check05 reverse", 0); 
#endif
                        direction = Check05(to, from, p_to); 
                        break;
                    case LOGICAL:
#ifdef VERBOSE 
                        KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check06 reverse", 0); 
#endif
                        direction = Check06(to, from, p_to);
                        break;    
                } 
                direction = Reverse(direction);
            }
        } else { // L[k].task_id != L'[k].task_id
#ifdef VERBOSE 
            KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check10", 0); 
#endif
            direction = Check10(from, to, p_from);  
        }
    } else if (from_segment.type == LOGICAL && to_segment.type == LOGICAL) {
#ifdef VERBOSE 
        KA_TRACE(0, STDOUT, 0, "HappensBefore", "Check11", 0); 
#endif
        direction = Check11(from, to, p_from);   
    } else {
#ifdef VERBOSE 
        KA_TRACE(0, STDERR, 0, "HappensBefore", "Error", 0); 
#endif
        return ERROR;
    }
#ifdef VERBOSE 
    KA_TRACE(100, STDERR, 0, "HappensBefore", "end", 0); 
#endif
    return direction;
} 

static ParallelJudge
Parallel(LabelPtr& l1, LabelPtr& l2)
{
   // KA_TRACE(100, STDERR, 0, "Parallel ", "HappensBefore start", 0);         
    auto direction = HappensBefore(l1, l2);
    if (direction == PARALLEL) {
        return TRUE;
    }
    if (direction == PARALLEL_WORKSHARE) {
        return MAYBE;
    }
    if (direction == ERROR) {
        KA_TRACE(0, STDERR, 0, "Parallel ", "HappensBefore return error", 0);         
    }
    //KA_TRACE(100, STDERR, 0, "Parallel ", "end ", 0);         
    return FALSE;
}
