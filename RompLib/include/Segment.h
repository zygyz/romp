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
 *  The abstract class definition for label segment. 
 */
class Segment {
public: 
  virtual std::string toString() const = 0;
  virtual std::string toFieldsBreakdown() const = 0;
  virtual void setType(SegmentType type) = 0; 
  virtual SegmentType getType() const = 0;
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
  bool operator==(const Segment& rhs) const override; 
  bool operator!=(const Segment& rhs) const override;

  std::shared_ptr<BaseSegment> clone() const;

  void setOffsetSpan(uint64_t offset, uint64_t span);
  void setTaskwait(uint64_t taskwait);
  void setTaskCreateCount(uint64_t taskcreate);
  void setPhase(uint64_t phase);
  void setLoopCount(uint64_t loopCount);
  void setTaskGroupId(uint16_t taskGroupId);
  void setTaskGroupLevel(uint16_t taskGroupLevel);
  void setTaskGroupPhase(uint16_t phase);
  void setTaskwaitPhase(uint16_t phase);
  void setTaskwaited();
  void setTaskGroupSync(); 

  void toggleSingleExecutor();
  void toggleSingleOther();

  void getOffsetSpan(uint64_t& offset, uint64_t& span) const;
  uint64_t getTaskwait() const;
  uint64_t getTaskcreate() const;
  uint64_t getPhase() const;
  uint64_t getLoopCount() const;
  uint16_t getTaskGroupId() const ;
  uint16_t getTaskGroupLevel() const ;
  uint16_t getTaskGroupPhase() const ;
  uint16_t getTaskwaitPhase() const;
  bool isTaskwaited() const;
  bool isTaskGroupSync() const;
  bool isSingleExecutor() const;
  bool isSingleOther() const; 
  uint64_t getValue() const;

protected:
  uint64_t mValue; // store most of the label segment fields.
  uint32_t mTaskGroup; // TODO: revisit taskgroup handling
  uint32_t mOrderSecVal;  
};

class ExplicitTaskSegment: public BaseSegment {
public:
  ExplicitTaskSegment() { 
    initialize();
    mTaskDataPtr = nullptr;
  }
  ExplicitTaskSegment(void* taskDataPtr) {
    initialize();
    mTaskDataPtr = taskDataPtr;
  }
  ExplicitTaskSegment(const ExplicitTaskSegment& segment): BaseSegment(segment) {
    mTaskDataPtr = segment.mTaskDataPtr;
  }
  std::string toString() const override;
  std::string toFieldsBreakdown() const override;
  bool operator==(const Segment& rhs) const override;
  bool operator!=(const Segment& rhs) const override;

  std::shared_ptr<BaseSegment> clone() const;
  void* getTaskPtr() const;
private:
  void initialize();  
  void* mTaskDataPtr; 
};

/*
 * Workshare segment is for representing task with worksharing 
 * construct.
 */
class WorkShareSegment: public BaseSegment {
public:
  WorkShareSegment() {
    initialize(); 
    mWorkShareID = 0;
  }
  WorkShareSegment(uint64_t id, WorkShareType workshareType) {
    initialize();
    mWorkShareID = id;
    setWorkShareType(workshareType);
  }
  WorkShareSegment(const WorkShareSegment& segment): BaseSegment(segment) {
    mWorkShareID = segment.mWorkShareID;
  }
  void toggleWorkSharePlaceHolderFlag();
  bool isWorkSharePlaceHolder() const;
  void setWorkShareType(WorkShareType workShareType);
  WorkShareType getWorkShareType() const;
  uint64_t getWorkShareId() const;
  std::shared_ptr<BaseSegment> clone() const;

  std::string toString() const override;
  std::string toFieldsBreakdown() const override;
  bool operator==(const Segment& rhs) const override;
  bool operator!=(const Segment& rhs) const override;
private: 
  void initialize();
  uint64_t mWorkShareID; 
};
