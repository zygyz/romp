#pragma once
#include <cstdint>
#include <memory> 
#include <string>

enum SegmentType {
  eImplicit = 0x1,
  eExplicit = 0x2,
  eLogical = 0x3, 
  eError = 0x4,
};

enum WorkShareType {
  eIteration = 0,
  eSection = 1,
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
  virtual std::string toFieldsBreakdown() const = 0;
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
  virtual void toggleSingleExecutor() = 0;
  virtual void toggleSingleOther() = 0;
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
  virtual bool isSingleExecutor() const = 0;
  virtual bool isSingleOther() const = 0; 
  virtual bool isTaskwaited() const = 0;
  virtual bool isTaskGroupSync() const = 0;
  virtual bool operator==(const Segment& rhs) const = 0;
  virtual bool operator!=(const Segment& rhs) const = 0;
  virtual ~Segment() = default;
};

/*
 * Base segment is for representing implicit task with no 
 * worksharing construct attached to it.
 * mValue records most of the information wrt. openmp synchronization
 * mTaskGroup records the taskgroup information
 * mOrderSecVal records ordered section phase when taskwait/taskgroup
 * sync happens
 */
class BaseSegment : public Segment {
public:
  BaseSegment(): mValue(0), mTaskGroup(0), mOrderSecVal(0) {}
  BaseSegment(const BaseSegment& segment): mValue(segment.mValue), 
             mTaskGroup(segment.mTaskGroup), mOrderSecVal(segment.mOrderSecVal) {}
  BaseSegment(SegmentType type, uint64_t offset, uint64_t span);
  std::string toString() const override;
  std::string toFieldsBreakdown() const override;
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
  void toggleSingleExecutor();
  void toggleSingleOther();
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
  bool isSingleExecutor() const override;
  bool isSingleOther() const override; 
  bool operator==(const Segment& rhs) const override; 
  bool operator!=(const Segment& rhs) const override;
  uint64_t getValue() const;
protected:
  uint64_t mValue;
  uint32_t mTaskGroup;
  uint32_t mOrderSecVal; 
};

/*
 * Workshare segment is for representing task with worksharing 
 * construct.
 */
class WorkShareSegment: public BaseSegment {
public:
  WorkShareSegment() : mWorkShareID(0) { setType(eLogical); 
      setOffsetSpan(0, 1); }
  WorkShareSegment(uint64_t id, WorkShareType workShareType): mWorkShareID(id) { 
    setType(eLogical); 
    setWorkShareType(workShareType);
    setOffsetSpan(0, 1);
  } 
  WorkShareSegment(const WorkShareSegment& segment): BaseSegment(segment), 
     mWorkShareID(segment.mWorkShareID) { }                       
  void toggleWorkSharePlaceHolderFlag();
  bool isWorkSharePlaceHolder() const;
  void setWorkShareType(WorkShareType workShareType);
  WorkShareType getWorkShareType() const;
  uint64_t getWorkShareId() const;
  std::string toString() const override;
  std::string toFieldsBreakdown() const override;
  std::shared_ptr<Segment> clone() const override;
  bool operator==(const Segment& rhs) const override;
  bool operator!=(const Segment& rhs) const override;
private: 
  uint64_t mWorkShareID; 
};
