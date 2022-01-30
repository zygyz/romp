#include "Segment.h"

#include <iomanip>
#include <glog/logging.h>
#include <glog/raw_logging.h>
#include <sstream>

#define SEG_TYPE_MASK        0x0000000000000003
#define OFFSET_MASK          0xffff000000000000
#define SPAN_MASK            0x0000ffff00000000
#define TASKWAIT_MASK        0xffffffff0fffffff
#define PHASE_MASK           0x000000000f000000
#define WS_PLACE_HOLDER_MASK 0xfffffffffffffffb
#define LOOP_CNT_MASK        0x0000000000f00000
#define TASK_CREATE_MASK     0x00000000000fffc0
#define WORKSHARE_TYPE_MASK  0x0000000000000004
#define TASKWAIT_SYNC_MASK   0x0000000000000008
#define TASKGROUP_SYNC_MASK  0x0000000000000010
#define TASKGROUP_ID_MASK    0x00000000ffff0000
#define TASKGROUP_LEVEL_MASK 0x000000000000ffff
#define TASKGROUP_PHASE_MASK 0x00000000ffff0000
#define TASKWAIT_PHASE_MASK  0x000000000000ffff
#define SINGLE_MASK          0x0000000000000030

#define OFFSET_SPAN_WIDTH 16

#define OFFSET_SHIFT 48
#define SPAN_SHIFT 32
#define TASKWAIT_SHIFT 28
#define PHASE_SHIFT 24
#define LOOP_CNT_SHIFT 20
#define TASK_CREATE_SHIFT 6
#define WS_PLACE_HOLDER_POS 2  // least significant bit index is 0
#define SINGLE_EXECUTOR_SHIFT 4
#define SINGLE_OTHER_SHIFT 5

namespace romp {

/*
 * Each segment contains a 64 bit value. From low to high, assign index 0-63
 * [0,1]: segment type 
 * [48, 63]: offset 
 * [32, 47]: span 
 * [28, 31]: taskwait count
 * [24, 27]: phase count
 * [20, 23]: loop count
 * [7, 19]: task create count
 * [5, 6]: bit 5 set: is single executable; bit 6 set: is single other
 * [4]: mark if current task sync by taskgroup with its parent task
 * [3]: mark if current task (must be explicit) syncs with taskwait 
 * [2]: mark if current workshare semgent is section, bit set: yes. 
 *      otherwise, sgment is iteration
 *
 * For workshare segment, we use the extra m_workShareId to store information
 * [0,31]: work share id // DEPRECATED 
 * [62,63]: single construct flag bits // DEPRECATED 
 */
std::string BaseSegment::toString() const {
  std::stringstream stream;
  if (m_taskGroup == 0) {
    stream << std::hex << std::setw(16) << std::setfill('0') << m_value;
  } else if (m_orderSecVal == 0) {
    stream << std::hex << std::setw(16) << std::setfill('0') << m_value << 
     std::setfill('0') << ",tg:" << std::hex << m_taskGroup;
  } else {
    stream << std::hex << std::setw(16) << std::setfill('0') << m_value << 
    std::setfill('0') << ",tg:" << m_taskGroup << ",osv:" << 
    m_orderSecVal;
  }
  return "[" + stream.str() + "]";
}

BaseSegment::BaseSegment(SegmentType type, uint64_t offset, 
        uint64_t span) {
  RAW_CHECK(span < (1 << OFFSET_SPAN_WIDTH), "span is overflowing");
  m_value = 0;
  m_taskGroup = 0;
  m_orderSecVal = 0;
  setType(type);
  setOffsetSpan(offset, span);
}

std::shared_ptr<Segment> BaseSegment::clone() const {
  return std::make_shared<BaseSegment>(*this);
}

uint64_t BaseSegment::getValue() const {
  return m_value;
}

void BaseSegment::setOffsetSpan(uint64_t offset, uint64_t span) {
  m_value &= ~(OFFSET_MASK | SPAN_MASK);  // clear the offset, span field
  m_value |= (offset << OFFSET_SHIFT) & OFFSET_MASK; 
  m_value |= (span << SPAN_SHIFT) & SPAN_MASK; 
}

void BaseSegment::getOffsetSpan(uint64_t& offset, uint64_t& span) const {
  offset = (m_value & OFFSET_MASK) >> OFFSET_SHIFT;
  span = (m_value & SPAN_MASK) >> SPAN_SHIFT;
}

/* 
 * Taskgroup id increases monotonically. It is at the upper half of the
 * 32 bits m_taskGroup value
 */
uint16_t BaseSegment::getTaskGroupId() const {
  return static_cast<uint16_t>(m_taskGroup >> 16);
}
                                   
void BaseSegment::setTaskGroupId(uint16_t taskGroupId) {
  m_taskGroup = static_cast<uint32_t>(
          static_cast<uint64_t>(m_taskGroup) & ~TASKGROUP_ID_MASK);
  m_taskGroup |= static_cast<uint32_t>(
          (static_cast<uint64_t>(taskGroupId) << 16) & TASKGROUP_ID_MASK);
}

/*
 * Upon encountering the end of taskgroup, the task informs its direct children
 * to record the ordered section phase. This is for reasoning about the 
 * happens-before relation when ordered section is involed. Store the phase at 
 * the upper half of the 32 bit m_orderSecVal.
 */
void BaseSegment::setTaskGroupPhase(uint16_t phase) {
  m_orderSecVal = static_cast<uint32_t>(
         static_cast<uint64_t>(m_orderSecVal) & TASKGROUP_PHASE_MASK); 
  m_orderSecVal |= static_cast<uint32_t>(
          (static_cast<uint64_t>(phase) << 16) & TASKGROUP_PHASE_MASK);
}

/*
 * Upon encountering the takwait, the task informs its direct children to record 
 * the ordered section phase. Store the phase at the lower half of the 32 bit
 * m_orderSecVal.
 */
void BaseSegment::setTaskwaitPhase(uint16_t phase) {
  m_orderSecVal = static_cast<uint32_t>(
          static_cast<uint64_t>(m_orderSecVal) & TASKWAIT_PHASE_MASK);
  m_orderSecVal |= static_cast<uint32_t>(
          (static_cast<uint64_t>(phase) & TASKWAIT_PHASE_MASK));
}

uint16_t BaseSegment::getTaskwaitPhase() const {
  return static_cast<uint16_t>(
      static_cast<uint64_t>(m_orderSecVal) & TASKWAIT_PHASE_MASK);
}

/*
 * Taskgroup level marks the nested number of level of taskgorup. 
 * It is the lower 16 bits of the 32 bits long word m_taskGroup
 */
uint16_t BaseSegment::getTaskGroupLevel() const {
  return static_cast<uint16_t>(
          static_cast<uint64_t>(m_taskGroup) & TASKGROUP_LEVEL_MASK); 
}

/*
 * Task group phase records the phase of the workshare task, if applicable,
 * as the task encounters the taskgroup start/end point.
 */
uint16_t BaseSegment::getTaskGroupPhase() const {
  return static_cast<uint16_t>((m_taskGroup & TASKGROUP_PHASE_MASK) >> 16);
}

void BaseSegment::setTaskwaited() {
  m_value |= TASKWAIT_SYNC_MASK; 
}

bool BaseSegment::isTaskwaited() const {
  return (m_value & TASKWAIT_SYNC_MASK) != 0;
}

bool BaseSegment::isSingleExecutor() const {
  return (m_value & SINGLE_MASK) >>  SINGLE_EXECUTOR_SHIFT;
}

bool BaseSegment::isSingleOther() const {
  return (m_value & SINGLE_MASK) >> SINGLE_OTHER_SHIFT;
}

void BaseSegment::toggleSingleExecutor() {
  m_value ^= 1UL << SINGLE_EXECUTOR_SHIFT;   
}

void BaseSegment::toggleSingleOther() {
  m_value ^= 1UL << SINGLE_OTHER_SHIFT;
}

void BaseSegment::setTaskGroupSync() { 
  m_value |= TASKGROUP_SYNC_MASK;
}

bool BaseSegment::isTaskGroupSync() const {
  return (m_value & TASKGROUP_SYNC_MASK) != 0;
}

void BaseSegment::setTaskGroupLevel(uint16_t taskGroupLevel) {
  m_taskGroup = static_cast<uint32_t>(
          static_cast<uint64_t>(m_taskGroup) & ~TASKGROUP_LEVEL_MASK);
  m_taskGroup |= static_cast<uint32_t>(
          static_cast<uint64_t>(taskGroupLevel) & TASKGROUP_LEVEL_MASK);
}

bool BaseSegment::operator==(const Segment& segment) const {
  return m_value == dynamic_cast<const BaseSegment&>(segment).m_value;
}

bool BaseSegment::operator!=(const Segment& segment) const {
  return !(*this == segment);
}
/*
 * Taskwait field is four bits. So if taskwait is more than 16, it overflows.
 */
void BaseSegment::setTaskwait(uint64_t taskwait) {
  RAW_CHECK(taskwait < 16, "taskwait count is overflowing");
  m_value &= TASKWAIT_MASK; // clear the taskwait field
  m_value |= (taskwait << TASKWAIT_SHIFT) & ~TASKWAIT_MASK;
}

uint64_t BaseSegment::getTaskwait() const {
  uint64_t taskwait = (m_value & ~TASKWAIT_MASK) >> TASKWAIT_SHIFT;
  return taskwait;
}

void BaseSegment::setTaskcreate(uint64_t taskcreate) { 
  RAW_CHECK(taskcreate < (1 << 15), "taskcreate count is overflowing");
  m_value &= ~TASK_CREATE_MASK;
  m_value |= (taskcreate << TASK_CREATE_SHIFT) & TASK_CREATE_MASK;
}

uint64_t BaseSegment::getTaskcreate() const {
  uint64_t taskcreate = (m_value & TASK_CREATE_MASK) >> TASK_CREATE_SHIFT;
  return taskcreate;
}

void BaseSegment::setPhase(uint64_t phase) {
  RAW_CHECK(phase < 16, "phase count is overflowing");
  m_value &= ~PHASE_MASK;
  m_value |= (phase << PHASE_SHIFT) & PHASE_MASK;

}

uint64_t BaseSegment::getPhase() const {
  uint64_t phase = (m_value & PHASE_MASK) >> PHASE_SHIFT;
  return phase;
}

void BaseSegment::setLoopCount(uint64_t loopCount) {
  RAW_CHECK(loopCount < 16, "loop count is overflowing");
  m_value &= ~LOOP_CNT_MASK;
  m_value |= (loopCount << LOOP_CNT_SHIFT) & LOOP_CNT_MASK;
}

uint64_t BaseSegment::getLoopCount() const {
  uint64_t loopCount = (m_value & LOOP_CNT_MASK) >> LOOP_CNT_SHIFT;
  return loopCount;
}

void BaseSegment::setType(SegmentType type) {
  m_value |= static_cast<uint64_t>(type);
}

SegmentType BaseSegment::getType() const {
  auto mask = m_value & SEG_TYPE_MASK;
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
  stream << "ws:" << std::hex << std::setw(16) << std::setfill('0') << 
      m_workShareId;
  auto result = "[" + baseResult + stream.str() + "]";
  return result;
}

std::shared_ptr<Segment> WorkShareSegment::clone() const {
  return std::make_shared<WorkShareSegment>(*this);
}

/*
 * Set place holder flag for the workshare segment. If toggle is true,
 * set the flag, otherwise, clear the flag.
 */
void WorkShareSegment::setPlaceHolderFlag(bool toggle) {
  if (toggle) {
    m_value |= (1 << WS_PLACE_HOLDER_POS);
  } else {
    m_value &= WS_PLACE_HOLDER_MASK;
  }
}

bool WorkShareSegment::operator==(const Segment& segment) const {
  if (m_value == dynamic_cast<const BaseSegment&>(segment).getValue()) {
    // we know `segment` is also a workshare segment
    return m_workShareId == 
        dynamic_cast<const WorkShareSegment&>(segment).m_workShareId;
  } 
  return false;
}

bool WorkShareSegment::operator!=(const Segment& segment) const {
  return !(*this == segment);
}

bool WorkShareSegment::isPlaceHolder() const {
  return ((m_value & ~WS_PLACE_HOLDER_MASK) >> WS_PLACE_HOLDER_POS) == 1;
}

bool WorkShareSegment::isSingleExecutor() const {
  return ((m_workShareId & SINGLE_MASK) >> SINGLE_EXECUTOR_SHIFT) == 1;
}

bool WorkShareSegment::isSingleOther() const {
  return ((m_workShareId & SINGLE_MASK) >> SINGLE_OTHER_SHIFT) == 1;
}

uint64_t WorkShareSegment::getWorkShareId() const {
  return m_workShareId;
}

void WorkShareSegment::setSingleFlag(bool isExecutor) {
  m_workShareId &= ~SINGLE_MASK;
  uint64_t b = 1;
  if (isExecutor) {
    // toggle the higher bit to 1
    m_workShareId |= (b << SINGLE_EXECUTOR_SHIFT);
  } else {
    // single other, toggle the lower bit to 1
    m_workShareId |= (b << SINGLE_OTHER_SHIFT);
  }
}

void WorkShareSegment::setWorkShareType(bool isSection) {
  m_value &= ~WORKSHARE_TYPE_MASK; // clear the bit first
  if (isSection) {
    m_value |= WORKSHARE_TYPE_MASK;  // set the bit
  } 
}

bool WorkShareSegment::isSection() const {
  return (m_value & WORKSHARE_TYPE_MASK) != 0;
}


}
