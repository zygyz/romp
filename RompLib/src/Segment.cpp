#include "Segment.h"

#include <iomanip>
#include <glog/logging.h>
#include <glog/raw_logging.h>
#include <sstream>

// mask bits are set to 1 if they represent the corresponding field location
#define SEGMENT_TYPE_MASK 0x0000000000000003
#define WORK_SHARE_PLACEHOLDER_MASK 0x0000000000000004
#define TASKWAIT_SYNC_MASK 0x0000000000000008
#define TASKGROUP_SYNC_MASK 0x0000000000000010
#define SINGLE_MASK 0x0000000000000060
#define TASK_CREATE_MASK 0x00000000000fff80
#define UNDEFERRED_TASK_COUNT_MASK 0x0000000000f00000
#define LOOP_COUNT_MASK 0x0000000fff000000
#define PHASE_MASK 0x000000f000000000
#define TASKWAIT_MASK 0x00000f0000000000
#define SPAN_MASK 0x003ff00000000000
#define OFFSET_MASK 0xffc0000000000000

//TODO: revisit the taskgroup handling
#define TASKGROUP_ID_MASK    0x00000000ffff0000
#define TASKGROUP_LEVEL_MASK 0x000000000000ffff
#define TASKGROUP_PHASE_MASK 0x00000000ffff0000
#define TASKWAIT_PHASE_MASK  0x000000000000ffff

#define WORK_SHARE_TYPE_MASK 0xc000000000000000
#define OFFSET_SPAN_WIDTH 10

#define OFFSET_SHIFT 54
#define SPAN_SHIFT 44
#define TASKWAIT_SHIFT 40
#define PHASE_SHIFT 36
#define LOOP_COUNT_SHIFT 24
#define UNDEFERRED_TASK_COUNT_SHIFT 20
#define TASK_CREATE_SHIFT 7
#define WORK_SHARE_PLACEHOLDER_SHIFT 2 
#define SINGLE_EXECUTOR_SHIFT 5
#define SINGLE_OTHER_SHIFT 6
#define WORK_SHARE_TYPE_SHIFT 62

#define LOOP_COUNT_MASK_BITS 12

/*
 * Each segment contains a 64 bit value. From low to high, assign index 0-63
 * [54, 63]: offset 
 * [44, 53]: span 
 * [40, 43]: taskwait count
 * [36, 39]: phase count
 * [24, 35]: loop count 
 * [20, 23]: undeferred explicit task count
 * [7, 19]: task create count
 * [5, 6]: bit 5 set: is single executable; bit 6 set: is single other
 * [4]: mark if current task sync by taskgroup with its parent task
 * [3]: mark if current task (must be explicit) syncs with taskwait 
 * [2]: mark work share placeholder bit 
 * [0,1]: segment type 
 *
 * For workshare segment, we use the extra mWorkShareID to store information
 * [0,31]: work share id 
 * [62,63]: work share type: 00: iteartion 01: section, 
 */
std::string BaseSegment::toString() const {
  std::stringstream stream;
  if (mTaskGroup == 0) {
    stream << std::hex << std::setw(16) << std::setfill('0') << mValue;
  } else if (mOrderSecVal == 0) {
    stream << std::hex << std::setw(16) << std::setfill('0') << mValue << 
     std::setfill('0') << ",tg:" << std::hex << mTaskGroup;
  } else {
    stream << std::hex << std::setw(16) << std::setfill('0') << mValue << 
    std::setfill('0') << ",tg:" << mTaskGroup << ",osv:" << 
    mOrderSecVal;
  }
  return "[" + stream.str() + "]";
}

std::string BaseSegment::toFieldsBreakdown() const {
  std::stringstream stream;
  uint64_t offset, span;
  getOffsetSpan(offset, span);
  stream << "offset: " << offset << " span: " << span;
  if (isTaskwaited()) {
    stream << " taskwaited";
  }
  if (isSingleExecutor()) {
    stream << " single executor"; 
  }
  if (isSingleOther()) {
    stream << " single other";
  }
  if (isTaskGroupSync()) { 
    stream << " task group sync";
  } 
  stream << " loop count: " << getLoopCount();
  stream << " task create count: " << getTaskcreate();
  stream << " task wait: " << getTaskwait();
  stream << " undeferrred task count: " << getUndeferredTaskCount();
  auto segmentType = getType();
  switch(segmentType) {
    case eImplicit:
      stream << " type: imp";
      break;
    case eExplicit:
      stream << " type: exp";
      break;
    case eLogical:
      stream << " type: logi";
      break;
  } 
  return "[" + stream.str() + "]";
}

BaseSegment::BaseSegment(SegmentType type, uint64_t offset, uint64_t span) {
  RAW_CHECK(span < (1 << OFFSET_SPAN_WIDTH), "span is overflowing");
  mValue = 0;
  mTaskGroup = 0;
  mOrderSecVal = 0;
  setType(type);
  setOffsetSpan(offset, span);
}

std::shared_ptr<BaseSegment> BaseSegment::clone() const {
  return std::make_shared<BaseSegment>(*this);
}

uint64_t BaseSegment::getValue() const {
  return mValue;
}

void BaseSegment::setOffsetSpan(uint64_t offset, uint64_t span) {
  mValue &= ~(OFFSET_MASK | SPAN_MASK);  // clear the offset, span field
  mValue |= (offset << OFFSET_SHIFT) & OFFSET_MASK; 
  mValue |= (span << SPAN_SHIFT) & SPAN_MASK; 
}

void BaseSegment::getOffsetSpan(uint64_t& offset, uint64_t& span) const {
  offset = (mValue & OFFSET_MASK) >> OFFSET_SHIFT;
  span = (mValue & SPAN_MASK) >> SPAN_SHIFT;
}

/* 
 * Taskgroup id increases monotonically. It is at the upper half of the
 * 32 bits mTaskGroup value
 */
uint16_t BaseSegment::getTaskGroupId() const {
  return static_cast<uint16_t>(mTaskGroup >> 16);
}
                                   
void BaseSegment::setTaskGroupId(uint16_t taskGroupId) {
  mTaskGroup = static_cast<uint32_t>(static_cast<uint64_t>(mTaskGroup) & ~TASKGROUP_ID_MASK);
  mTaskGroup |= static_cast<uint32_t>((static_cast<uint64_t>(taskGroupId) << 16) & TASKGROUP_ID_MASK);
}

/*
 * Upon encountering the end of taskgroup, the task informs its direct children
 * to record the ordered section phase. This is for reasoning about the 
 * happens-before relation when ordered section is involed. Store the phase at 
 * the upper half of the 32 bit mOrderSecVal.
 */
void BaseSegment::setTaskGroupPhase(uint16_t phase) {
  mOrderSecVal = static_cast<uint32_t>(static_cast<uint64_t>(mOrderSecVal) & TASKGROUP_PHASE_MASK); 
  mOrderSecVal |= static_cast<uint32_t>((static_cast<uint64_t>(phase) << 16) & TASKGROUP_PHASE_MASK);
}

/*
 * Upon encountering the takwait, the task informs its direct children to record 
 * the ordered section phase. Store the phase at the lower half of the 32 bit
 * mOrderSecVal.
 */
void BaseSegment::setTaskwaitPhase(uint16_t phase) {
  mOrderSecVal = static_cast<uint32_t>(static_cast<uint64_t>(mOrderSecVal) & TASKWAIT_PHASE_MASK);
  mOrderSecVal |= static_cast<uint32_t>((static_cast<uint64_t>(phase) & TASKWAIT_PHASE_MASK));
}

uint16_t BaseSegment::getTaskwaitPhase() const {
  return static_cast<uint16_t>(static_cast<uint64_t>(mOrderSecVal) & TASKWAIT_PHASE_MASK);
}

/*
 * Taskgroup level marks the nested number of level of taskgorup. 
 * It is the lower 16 bits of the 32 bits long word mTaskGroup
 */
uint16_t BaseSegment::getTaskGroupLevel() const {
  return static_cast<uint16_t>(static_cast<uint64_t>(mTaskGroup) & TASKGROUP_LEVEL_MASK); 
}

/*
 * Task group phase records the phase of the workshare task, if applicable,
 * as the task encounters the taskgroup start/end point.
 */
uint16_t BaseSegment::getTaskGroupPhase() const {
  return static_cast<uint16_t>((mTaskGroup & TASKGROUP_PHASE_MASK) >> 16);
}

void BaseSegment::setTaskwaited() {
  mValue |= TASKWAIT_SYNC_MASK; 
}

bool BaseSegment::isTaskwaited() const {
  return (mValue & TASKWAIT_SYNC_MASK) != 0;
}

bool BaseSegment::isSingleExecutor() const {
  return (mValue & SINGLE_MASK) >>  SINGLE_EXECUTOR_SHIFT;
}

bool BaseSegment::isSingleOther() const {
  return (mValue & SINGLE_MASK) >> SINGLE_OTHER_SHIFT;
}

void BaseSegment::toggleSingleExecutor() {
  mValue ^= 1UL << SINGLE_EXECUTOR_SHIFT;   
}

void BaseSegment::toggleSingleOther() {
  mValue ^= 1UL << SINGLE_OTHER_SHIFT;
}

void BaseSegment::setTaskGroupSync() { 
  mValue |= TASKGROUP_SYNC_MASK;
}

bool BaseSegment::isTaskGroupSync() const {
  return (mValue & TASKGROUP_SYNC_MASK) != 0;
}

void BaseSegment::setTaskGroupLevel(uint16_t taskGroupLevel) {
  mTaskGroup = static_cast<uint32_t>(static_cast<uint64_t>(mTaskGroup) & ~TASKGROUP_LEVEL_MASK);
  mTaskGroup |= static_cast<uint32_t>(static_cast<uint64_t>(taskGroupLevel) & TASKGROUP_LEVEL_MASK);
}

bool BaseSegment::operator==(const Segment& segment) const {
  return mValue == dynamic_cast<const BaseSegment&>(segment).mValue;
}

bool BaseSegment::operator!=(const Segment& segment) const {
  return !(*this == segment);
}

void BaseSegment::setTaskwait(uint64_t taskwait) {
  RAW_CHECK(taskwait < (1 << 4), "taskwait count is overflowing");
  mValue &= ~TASKWAIT_MASK; // clear the taskwait field 
  mValue |= (taskwait << TASKWAIT_SHIFT) & TASKWAIT_MASK;
}

uint64_t BaseSegment::getTaskwait() const {
  uint64_t taskwait = (mValue & TASKWAIT_MASK) >> TASKWAIT_SHIFT;
  return taskwait;
}

void BaseSegment::setTaskCreateCount(uint64_t taskcreate) { 
  RAW_CHECK(taskcreate < (1 << 13), "taskcreate count is overflowing");
  mValue &= ~TASK_CREATE_MASK;
  mValue |= (taskcreate << TASK_CREATE_SHIFT) & TASK_CREATE_MASK;
}

void BaseSegment::setUndeferredTaskCount(uint16_t undeferredTaskCount) {
  RAW_CHECK(undeferredTaskCount < (1 << 4), "undeferred task count is overflowing");
  mValue &= ~UNDEFERRED_TASK_COUNT_MASK;
  mValue |= (undeferredTaskCount << UNDEFERRED_TASK_COUNT_SHIFT) & UNDEFERRED_TASK_COUNT_MASK;
}

uint16_t BaseSegment::getUndeferredTaskCount() const {
  return static_cast<uint16_t>((mValue & UNDEFERRED_TASK_COUNT_MASK) >> UNDEFERRED_TASK_COUNT_SHIFT);
}

uint64_t BaseSegment::getTaskcreate() const {
  return static_cast<uint64_t>((mValue & TASK_CREATE_MASK) >> TASK_CREATE_SHIFT);
}

void BaseSegment::setPhase(uint64_t phase) {
  RAW_CHECK(phase < 16, "phase count is overflowing");
  mValue &= ~PHASE_MASK;
  mValue |= (phase << PHASE_SHIFT) & PHASE_MASK;
}

uint64_t BaseSegment::getPhase() const {
  return static_cast<uint64_t>((mValue & PHASE_MASK) >> PHASE_SHIFT);
}

void BaseSegment::setLoopCount(uint64_t loopCount) {
  RAW_CHECK(loopCount < (1 << LOOP_COUNT_MASK_BITS), "loop count is overflowing");
  mValue &= ~LOOP_COUNT_MASK;
  mValue |= (loopCount << LOOP_COUNT_SHIFT) & LOOP_COUNT_MASK;
}

uint64_t BaseSegment::getLoopCount() const {
  return static_cast<uint64_t>((mValue & LOOP_COUNT_MASK) >> LOOP_COUNT_SHIFT);
}

void BaseSegment::setType(SegmentType type) {
  mValue |= static_cast<uint64_t>(type);
}

SegmentType BaseSegment::getType() const {
  auto mask = mValue & SEGMENT_TYPE_MASK;
  switch(mask) {
    case 0x1:
      return eImplicit;
    case 0x2:
      return eExplicit;
    case 0x3:
      return eLogical; 
  }
  RAW_LOG(FATAL, "undefined segment type: %d", mask);
  return eError;
}

std::string WorkShareSegment::toString() const {
  std::stringstream stream;
  auto baseResult = BaseSegment::toString();
  stream << "ws:" << std::hex << std::setw(16) << std::setfill('0') << mWorkShareID;
  auto result = "[" + baseResult + stream.str() + "]";
  return result;
}

std::string WorkShareSegment::toFieldsBreakdown() const {
  std::stringstream stream;
  auto baseResult = BaseSegment::toFieldsBreakdown();
  if (isWorkSharePlaceHolder()) {
    stream << " is workshare placeholder";
  }
  stream << " workshare type: " << getWorkShareType() << " work share id: " << getWorkShareId();
  stream << "ws:" << std::hex << std::setw(16) << std::setfill('0') << mWorkShareID;
  auto result = "[" + baseResult + stream.str() + "]";
  return result;
}

std::shared_ptr<BaseSegment> WorkShareSegment::clone() const {
  return std::make_shared<WorkShareSegment>(*this);
}

void WorkShareSegment::toggleWorkSharePlaceHolderFlag() {
  mValue ^= 1UL << WORK_SHARE_PLACEHOLDER_SHIFT;   
}

bool WorkShareSegment::isWorkSharePlaceHolder() const {
  return (mValue & WORK_SHARE_PLACEHOLDER_MASK) >>  WORK_SHARE_PLACEHOLDER_SHIFT;
}

bool WorkShareSegment::operator==(const Segment& segment) const {
  if (mValue == dynamic_cast<const BaseSegment&>(segment).getValue()) {
    // we know `segment` is also a workshare segment
    return mWorkShareID == dynamic_cast<const WorkShareSegment&>(segment).mWorkShareID;
  } 
  return false;
}

bool WorkShareSegment::operator!=(const Segment& segment) const {
  return !(*this == segment);
}

uint64_t WorkShareSegment::getWorkShareId() const {
  return mWorkShareID;
}

void WorkShareSegment::setWorkShareType(WorkShareType type) {
   mWorkShareID &= ~WORK_SHARE_TYPE_MASK; // clear work share type 
   mWorkShareID |= (static_cast<uint64_t>(type) << WORK_SHARE_TYPE_SHIFT); 
}

WorkShareType WorkShareSegment::getWorkShareType() const {
  return static_cast<WorkShareType>((mWorkShareID & static_cast<uint64_t>(WORK_SHARE_TYPE_MASK)) >> WORK_SHARE_TYPE_SHIFT);
}
