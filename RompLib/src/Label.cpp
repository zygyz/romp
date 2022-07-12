#include "Label.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>
/* 
 * Use a shallow copy so that the new label does not create separate new 
 * segments. If later on some label segment changes, one should erase that 
 * label segment and create a new one otherwise the other label's content 
 * will be affected.
 */
Label::Label(const Label& label) {
  mLabel = label.mLabel; 
}

inline std::shared_ptr<BaseSegment> Clone(BaseSegment* segment) {
  auto segmentType = segment->getType();
  if (segmentType == eLogical) {
    return static_cast<WorkShareSegment*>(segment)->clone(); 
  } else if (segmentType == eExplicit) {
    return static_cast<ExplicitTaskSegment*>(segment)->clone(); 
  } else {
    return segment->clone();
  }
}

std::string Label::toString() const {
  auto result = std::string("");
  for (const auto& segment : mLabel) {
    auto type = segment->getType();
    if (type == eExplicit) {
      result += static_cast<ExplicitTaskSegment*>(segment.get())->toString();
    } else if (type == eLogical) {
      result += static_cast<WorkShareSegment*>(segment.get())->toString();
    } else {
      result += segment->toString();
    } 
     result += std::string("|");
  }
  return result;
}

std::string Label::toFieldsBreakdown() const {
  auto result = std::string("");
  for (const auto& segment : mLabel) {
    result += segment->toFieldsBreakdown();
    result += std::string(" | ");
  }
  result += "\n";
  return result;
}

void Label::appendSegment(const std::shared_ptr<BaseSegment>& segment) {
  mLabel.push_back(segment);
}

std::shared_ptr<BaseSegment> Label::popSegment() {
  if (mLabel.empty()) {
    RAW_LOG(FATAL, "label is empty");
  }
  auto lastSegment = mLabel.back();
  mLabel.pop_back();
  return lastSegment;
}

std::shared_ptr<BaseSegment> Label::getLastKthSegment(int k) {
  if (k > mLabel.size()) {
    RAW_LOG(FATAL, "index is out of bound");
    return nullptr;
  }
  auto len = mLabel.size();
  return mLabel.at(len - k);
}

void Label::setLastKthSegment(int k, const std::shared_ptr<BaseSegment>& segment) { 
  if (k > mLabel.size()) {
    RAW_LOG(FATAL, "%s %d", "set value out of bound", k);
    return;
  }
  auto len = mLabel.size();
  mLabel[len - k] = std::move(segment);
}

BaseSegment* Label::getKthSegment(int k) {
  if (k > mLabel.size()) {
    RAW_LOG(FATAL, "index %d out of bound", k);
  }
  return mLabel.at(k).get();
}

int Label::getLabelLength() const {
  return mLabel.size();
}

// This function performs label comparison.
int compareLabels(Label* left, Label* right) {
  auto& leftLabel = left->mLabel;
  auto& rightLabel = right->mLabel;  
  auto lenLeftLabel = leftLabel.size();
  auto lenRightLabel = rightLabel.size();
  auto len = std::min(lenLeftLabel, lenRightLabel);
  for (int i = 0; i < len; ++i) {
    if (*leftLabel.at(i) != *rightLabel.at(i)) {
      return i;
    }
  }
  // reach the end, one label is the prefix of another label
  if (lenLeftLabel == lenRightLabel) {
    return static_cast<int>(eSameLabel);
  }
  if (len == lenLeftLabel) {
    return static_cast<int>(eLeftIsPrefix);
  }
  return static_cast<int>(eRightIsPrefix);
}

std::shared_ptr<Label> generateImplicitTaskLabel(
                           Label* parentLabel,
                           unsigned int index,
                           unsigned int actualParallelism) {
  // create the new label by copy constructing from parent label
  auto newLabel = std::make_shared<Label>(*parentLabel);
  // create a new label segment
  auto newSegment = std::make_shared<BaseSegment>(eImplicit, 
          static_cast<uint64_t>(index), 
          static_cast<uint64_t>(actualParallelism));
  newLabel->appendSegment(newSegment);
  return newLabel;
}

std::shared_ptr<Label> generateInitialTaskLabel() {
  auto label = std::make_shared<Label>();
  auto segment = std::make_shared<BaseSegment>(eImplicit, 0, 1);
  label->appendSegment(segment);
  return label;
}

/*
 * Given the parent task label, generate the label for the explicit task.
 */
std::shared_ptr<Label> generateExplicitTaskLabel(Label* parentLabel, void* taskDataPtr) {
  auto newLabel = std::make_shared<Label>(*parentLabel);
  auto segment = std::make_shared<ExplicitTaskSegment>(taskDataPtr); 
  newLabel->appendSegment(segment);
  return newLabel;
}

std::shared_ptr<Label> mutateParentImpEnd(Label* childLabel) {
  auto newLabel = std::make_shared<Label>(*childLabel);
  newLabel->popSegment();       
  return newLabel;
}

/*
 * Upon creating explicit task, increase the task create count in the segment 
 * of parent task. If the created task is undeferred task, increment the undeferred task count
 */
std::shared_ptr<Label> mutateParentTaskCreate(Label* parentLabel) {
  auto newLabel = std::make_shared<Label>(*parentLabel);  
  auto lastSegment = newLabel->popSegment();
  auto taskCreate = lastSegment->getTaskcreate();
  auto newSegment = Clone(lastSegment.get());
  newSegment->setTaskCreateCount(taskCreate + 1);  
  newLabel->appendSegment(newSegment);
  return newLabel;
}

/*
 * Given the task label `label`, generate the mutated label for encounteing 
 * the barrier. This mutation is done by adding span to the offset field of 
 * the second last segment of the label.
 */
std::shared_ptr<Label> mutateBarrierEnd(Label* label) {
  auto newLabel = std::make_shared<Label>(*label);
  auto segment = newLabel->getLastKthSegment(2); //get the second last segment
  uint64_t offset, span; 
  segment->getOffsetSpan(offset, span); //get the offset and span value
  offset += span;
  //because we don't know the actual derived type of segment, should do a clone
  auto newSegment = Clone(segment.get());
  newSegment->setOffsetSpan(offset, span); //set the new offset and span
  newLabel->setLastKthSegment(2, newSegment); 
  return newLabel; 
} 

/*
 * Given the label `label`, create a new label which is a mutation for after 
 * encountering taskwait clause. This is done by incrementing the taskwait 
 * field counter in the last label segment
 */ 
std::shared_ptr<Label> mutateTaskWait(Label* label) {
  auto newLabel = std::make_shared<Label>(*label);
  auto lastSegment = newLabel->popSegment(); // replace the last segment
  auto taskwait = lastSegment->getTaskwait();
  taskwait += 1;
  auto newSegment = Clone(lastSegment.get());
  newSegment->setTaskwait(taskwait);
  newLabel->appendSegment(newSegment);
  return newLabel;
}

/*
 * Given the label `label`, create a new label which is a mutation for 
 * encountering begin/end of ordered section. This is done by incrementing
 * the `phase` counter value by one.
 */
std::shared_ptr<Label> mutateOrderSection(Label* label) {
  auto newLabel = std::make_shared<Label>(*label);
  auto lastSegment = newLabel->popSegment(); // replace the last segment
  auto phase = lastSegment->getPhase();
  phase += 1;
  auto newSegment = Clone(lastSegment.get());
  newSegment->setPhase(phase);
  newLabel->appendSegment(newSegment);
  return newLabel;
}

/*
 * Mutate the label when workshare loop begin. Append a place holder segment
 * to mark the begin of the workshare loop.
 */
std::shared_ptr<Label> mutateLoopBegin(Label* label) {
  auto newLabel = std::make_shared<Label>(*label); 
  auto newSegment = std::make_shared<WorkShareSegment>(); 
  newSegment->toggleWorkSharePlaceHolderFlag();
  newLabel->appendSegment(newSegment);   
  return newLabel;
}

/*
 * Mutate the label when workshare loop ends. Pop the last segment which should
 * be a workshare segment. Increment the loop count of the current last 
 * segment by one (should replace the old one)
 */
std::shared_ptr<Label> mutateLoopEnd(Label* label) {
  auto newLabel = std::make_shared<Label>(*label); 
  newLabel->popSegment();
  auto segment = newLabel->popSegment();
  auto loopCount = segment->getLoopCount();
  loopCount += 1;
  auto newSegment = Clone(segment.get());
  newSegment->setLoopCount(loopCount);
  newLabel->appendSegment(newSegment);
  return newLabel;
}

/*
 * Sections construct is implemented as a dynamically scheduled workshare loop.
 * So we treat the sections as a workshared loop 
 */
std::shared_ptr<Label> mutateSectionBegin(Label* label) { 
  return mutateLoopBegin(label);
}

std::shared_ptr<Label> mutateSectionEnd(Label* label) {
  return mutateLoopEnd(label);
}

std::shared_ptr<Label> mutateSingleExecutor(Label* label) {
  auto newLabel = std::make_shared<Label>(*label); 
  auto segment = newLabel->getLastKthSegment(1); 
  auto newSegment = Clone(segment.get());
  newSegment->toggleSingleExecutor(); 
  newLabel->setLastKthSegment(1, newSegment); 
  return newLabel;
}

std::shared_ptr<Label> mutateSingleOther(Label* label) {
  auto newLabel = std::make_shared<Label>(*label); 
  auto segment = newLabel->getLastKthSegment(1); 
  auto newSegment = Clone(segment.get());
  newSegment->toggleSingleOther(); 
  newLabel->setLastKthSegment(1, newSegment); 
  return newLabel;
}

/*
 * Create a label for dispatched workshare construct. This is done by 
 * first pop off the last segment of the label, which should be a workshare
 * segment (if it is the first one encountered, the workshare segment is a 
 * place holder). Then append new workshare segment to the end of the label.
 * Depending on the type of workshare construct (iteration/section), create 
 * proper workshare segment.
 */
std::shared_ptr<Label> mutateLogicalDispatch(Label* label, uint64_t id, WorkShareType workShareType) {
  auto newLabel = std::make_shared<Label>(*label); 
  auto segment = newLabel->popSegment();
  RAW_DCHECK(segment->getType() == eLogical, "not a workshare segment");
  auto newSegment = std::make_shared<WorkShareSegment>(id, workShareType); 
  newLabel->appendSegment(newSegment);   
  return newLabel;
}

std::shared_ptr<Label> mutateWorkShareIterationDispatch(Label* label, uint64_t id) {
  return mutateLogicalDispatch(label, id, eIteration);
}

std::shared_ptr<Label> mutateSectionDispatch(Label* label, void* id) {  
  return mutateLogicalDispatch(label, reinterpret_cast<uint64_t>(id), eSection);
}

/*
 * Mutate the label of the task that encounters the beginnning of task group
 * This is done by incrementing the task group level by one, and increment the
 * task group id by one
 */
std::shared_ptr<Label> mutateTaskGroupBegin(Label* label) {
 auto newLabel = std::make_shared<Label>(*label);      
 auto segment = newLabel->popSegment();
 auto taskGroupId = segment->getTaskGroupId();
 taskGroupId += 1;
 auto taskGroupLevel = segment->getTaskGroupLevel();
 taskGroupLevel += 1;
 auto newSegment = Clone(segment.get());
 newSegment->setTaskGroupId(taskGroupId);
 newSegment->setTaskGroupLevel(taskGroupLevel);
 newLabel->appendSegment(newSegment);
 return newLabel;
}

/*
 * Mutate the label of the task that encounters the end of task group. 
 * This is done by decrementing the task group level by one, and increment
 * the task group id by one
 */
std::shared_ptr<Label> mutateTaskGroupEnd(Label* label) {
  auto newLabel = std::make_shared<Label>(*label);
  auto segment = newLabel->popSegment();
  auto taskGroupId = segment->getTaskGroupId();
  taskGroupId += 1;
  auto taskGroupLevel = segment->getTaskGroupLevel();
  taskGroupLevel -= 1;
  RAW_CHECK(taskGroupLevel >= 0, "not expecting task group level < 0");
  auto newSegment = Clone(segment.get()); 
  newSegment->setTaskGroupId(taskGroupId);
  newSegment->setTaskGroupLevel(taskGroupLevel);
  newLabel->appendSegment(newSegment);
  return newLabel;
}

/*
 * Mutate the label of the task that encounters the completion of explicit task.
 * This is done by poping the last label segment, which should be an explicit 
 * task segment.
 */
std::shared_ptr<Label> mutateTaskComplete(Label* label) {
  if (!label) {
    return nullptr;
  }
  auto newLabel = std::make_shared<Label>(*label);
  auto lastSeg = newLabel->popSegment();       
  auto lastSegType = lastSeg->getType(); 
  RAW_CHECK(lastSegType == eExplicit, "last segment should be explicit");
  return newLabel;
}

/*
 * Mutate the label of the child task inside a taskgroup construct when the 
 * taskgroup construct finishes. Set the taskgroup sync mark.
 */
std::shared_ptr<Label> mutateTaskGroupSyncChild(Label* label) {
  auto newLabel = std::make_shared<Label>(*label); 
  auto lastSeg = newLabel->getLastKthSegment(1);
  lastSeg->setTaskGroupSync();
  return newLabel;
}
