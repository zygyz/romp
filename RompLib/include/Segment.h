#pragma once
#include <cstdint>
#include <memory> 
#include <string>

namespace romp {

enum SegmentType {
  eImplicit = 0x1,
  eExplicit = 0x2,
  eLogical = 0x3, 
  eError = 0x4,
};

enum TaskSyncType {
  eTaskwait,
  eTaskGroupEnd,
};
/*
 *  The abstract class definition for label segment 
 */
class Segment {
public: 
  virtual std::string toString() const = 0;
  virtual void setType(SegmentType type) = 0; 
  virtual SegmentType getType() const = 0;
  virtual std::shared_ptr<Segment> clone() const = 0;  
  virtual void setOffsetSpan(uint64_t offset, uint64_t span) = 0;
  virtual void setTaskwait(uint64_t taskwait) = 0;
  virtual void setTaskcreate(uint64_t taskcreate) = 0; 
  virtual void setPhase(uint64_t phase) = 0;
  virtual void setLoopCount(uint64_t loopCount) = 0;
  virtual void setTaskGroupId(uint16_t taskGroupId) = 0;
  virtual void setTaskGroupLevel(uint16_t taskGroupLevel) = 0;
  virtual void setTaskGroupPhase(uint16_t phase) = 0;
  virtual void setTaskwaitPhase(uint16_t phase) = 0;
  virtual void getOffsetSpan(uint64_t& offset, uint64_t& span) const = 0;
  virtual void setTaskwaited() = 0;
  virtual void setTaskGroupSync() = 0;
  virtual uint64_t getTaskwait() const = 0;
  virtual uint64_t getTaskcreate() const = 0;
  virtual uint64_t getPhase() const = 0;
  virtual uint64_t getLoopCount() const = 0;  
  virtual uint16_t getTaskGroupId() const = 0;
  virtual uint16_t getTaskGroupLevel() const = 0;
  virtual uint16_t getTaskGroupPhase() const = 0;
  virtual uint16_t getTaskwaitPhase() const = 0;
  virtual bool isTaskwaited() const = 0;
  virtual bool isTaskGroupSync() const = 0;
  virtual bool operator==(const Segment& rhs) const = 0;
  virtual bool operator!=(const Segment& rhs) const = 0;
  virtual ~Segment() = default;
};

/*
 * Base segment is for representing implicit task with no 
 * worksharing construct attached to it.
 * _value records most of the information wrt. openmp synchronization
 * _taskGroup records the taskgroup information
 * _orderSecVal records ordered section phase when taskwait/taskgroup
 * sync happens
 */
class BaseSegment : public Segment {
public:
  BaseSegment(): _value(0), _taskGroup(0), _orderSecVal(0) {}
  BaseSegment(const BaseSegment& segment): _value(segment._value), 
             _taskGroup(segment._taskGroup), _orderSecVal(segment._orderSecVal) {}
  BaseSegment(SegmentType type, uint64_t offset, uint64_t span);
  std::string toString() const override;
  void setType(SegmentType type) override;
  SegmentType getType() const override;
  std::shared_ptr<Segment> clone() const override;
  void setOffsetSpan(uint64_t offset, uint64_t span) override;
  void setTaskwait(uint64_t taskwait) override;
  void setTaskcreate(uint64_t taskcreate) override;
  void setPhase(uint64_t phase) override;
  void setLoopCount(uint64_t loopCount) override;
  void setTaskGroupId(uint16_t taskGroupId) override;
  void setTaskGroupLevel(uint16_t taskGroupLevel) override;
  void setTaskGroupPhase(uint16_t phase) override;
  void setTaskwaitPhase(uint16_t phase) override;
  void setTaskwaited() override;
  void setTaskGroupSync() override; 
  void getOffsetSpan(uint64_t& offset, uint64_t& span) const override;
  uint64_t getTaskwait() const override;
  uint64_t getTaskcreate() const override;
  uint64_t getPhase() const override;
  uint64_t getLoopCount() const override;
  uint16_t getTaskGroupId() const override;
  uint16_t getTaskGroupLevel() const override;
  uint16_t getTaskGroupPhase() const override;
  uint16_t getTaskwaitPhase() const override;
  bool isTaskwaited() const override;
  bool isTaskGroupSync() const override;
  bool operator==(const Segment& rhs) const override; 
  bool operator!=(const Segment& rhs) const override;
  uint64_t getValue() const;
protected:
  uint64_t _value;
  uint32_t _taskGroup;
  uint32_t _orderSecVal; 
};

/*
 * Workshare segment is for representing task with worksharing 
 * construct.
 */
class WorkShareSegment: public BaseSegment {
public:
  WorkShareSegment() : _workShareId(0) { setType(eLogical); 
      setOffsetSpan(0, 1); }
  WorkShareSegment(uint64_t id, bool isSection): _workShareId(id) { 
    setType(eLogical); 
    setWorkShareType(isSection);
    setOffsetSpan(0, 1);
  } 
  WorkShareSegment(const WorkShareSegment& segment): BaseSegment(segment), 
     _workShareId(segment._workShareId) { }                       
  void setPlaceHolderFlag(bool toggle);
  bool isPlaceHolder() const;
  void setWorkShareType(bool isSection);
  bool isSection() const;
  void setSingleFlag(bool isExecutor);
  bool isSingleExecutor() const;
  bool isSingleOther() const;
  uint64_t getWorkShareId() const;
  std::string toString() const override;
  std::shared_ptr<Segment> clone() const override;
  bool operator==(const Segment& rhs) const override;
  bool operator!=(const Segment& rhs) const override;
private: 
  uint64_t _workShareId; 
};


}
