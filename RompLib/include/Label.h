#pragma once
#include <memory>
#include <vector>
#include "Segment.h"

enum LabelCompare {
  eLeftIsPrefix = -1,
  eRightIsPrefix = -2, 
  eSameLabel = -3,
  eLabelCompareUndefined = -4,
};
/*
 * Label class implements the high level representation of task label.
 * A task label consists of a series of label segments. Each label segment is 
 * represented by a derived class from Segment. 
 */
class Label {

public:
  Label() {}
  Label(const Label& label);
  ~Label() {} 
  std::string toString() const;
  std::string toFieldsBreakdown() const;
  void appendSegment(const std::shared_ptr<BaseSegment>& segment);
  std::shared_ptr<BaseSegment> popSegment();
  std::shared_ptr<BaseSegment> getLastKthSegment(int k);
  void setLastKthSegment(int k, const std::shared_ptr<BaseSegment>& segment);
  BaseSegment * getKthSegment(int k);
  friend int compareLabels(Label* left, Label* right);
  int getLabelLength() const;
private:
  std::vector<std::shared_ptr<BaseSegment> > mLabel;
};

int compareLabels(Label* left, Label* right);

std::shared_ptr<Label> generateImplicitTaskLabel(
                          Label* parentLabel, 
                          unsigned int index,
                          unsigned int actualParallelism);
std::shared_ptr<Label> generateInitialTaskLabel();
std::shared_ptr<Label> generateExplicitTaskLabel(Label* parentLabel, void* taskDataPtr);

std::shared_ptr<Label> mutateParentImpEnd(Label* childLabel);
std::shared_ptr<Label> mutateParentTaskCreate(Label* parentLabel);
std::shared_ptr<Label> mutateBarrierEnd(Label* label);
std::shared_ptr<Label> mutateTaskWait(Label* label);
std::shared_ptr<Label> mutateOrderSection(Label* label);
std::shared_ptr<Label> mutateLoopBegin(Label* label);
std::shared_ptr<Label> mutateLoopEnd(Label* label);
std::shared_ptr<Label> mutateSectionBegin(Label* label);
std::shared_ptr<Label> mutateSectionEnd(Label* label);
std::shared_ptr<Label> mutateSingleOther(Label* label);
std::shared_ptr<Label> mutateSingleExecutor(Label* label);
std::shared_ptr<Label> mutateWorkShareIterationDispatch(Label* label, uint64_t id);
std::shared_ptr<Label> mutateSectionDispatch(Label* label, void* id);
std::shared_ptr<Label> mutateLogicalDispatch(Label* l, uint64_t id, bool sec);
std::shared_ptr<Label> mutateTaskGroupBegin(Label* label);
std::shared_ptr<Label> mutateTaskGroupEnd(Label* label);
std::shared_ptr<Label> mutateTaskComplete(Label* label);
std::shared_ptr<Label> mutateTaskGroupSyncChild(Label* label);
