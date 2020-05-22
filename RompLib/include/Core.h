#pragma once 
#include "AccessHistory.h"
#include "CoreUtil.h"
#include "LockSet.h"
#include "TaskData.h"

namespace romp {

/*
 * Different sub cases for happens before analysis. Each case represents the 
 * segment type of corresponding next segment after the first pair of different
 * segments.
 */
#define CASE_SHIFT 2

enum CheckCase {
  eImpImp = eImplicit | (eImplicit << CASE_SHIFT),
  eImpExp = eImplicit | (eExplicit << CASE_SHIFT),
  eImpWork = eImplicit | (eWorkShare << CASE_SHIFT),
  eExpImp = eExplicit | (eImplicit << CASE_SHIFT),
  eExpExp = eExplicit | (eExplicit << CASE_SHIFT),
  eExpWork = eExplicit | (eWorkShare<< CASE_SHIFT),
  eWorkImp = eWorkShare | (eImplicit << CASE_SHIFT),
  eWorkExp = eWorkShare | (eExplicit << CASE_SHIFT),
  eWorkWork = eWorkShare | (eWorkShare << CASE_SHIFT),
}; 

enum RecordManagement{
  eNoOp,
  eSkipAddCur,
  eDelHist,
};

bool happensBefore(Label* histLabel, Label* curLabel, int& diffIndex);
bool analyzeSiblingImpTask(Label* histLabel, Label* curLabel, int index);
bool analyzeSameTask(Label* histLabel, Label* curLabel, int index);
bool analyzeOrderedSection(Label* histLabel, Label* curLabel, int index);
bool analyzeNextImpExp(Label* histLabel, Label* curLabel, int index);
bool analyzeNextImpWork(Label* histLabel, Label* curLabel, int index);
bool analyzeNextExpImp(Label* histLabel, Label* curLabel, int index);
bool analyzeNextExpExp(Label* histLabel, Label* curLabel, int index);
bool analyzeNextExpWork(Label* histLabel, Label* curLabel, int index);
bool analyzeNextWorkImp(Label* histLabel, Label* curLabel, int index);
bool analyzeNextWorkExp(Label* histLabel, Label* curLabel, int index);
bool analyzeNextWorkWork(Label* histLabel , Label* curLabel, int index);
bool analyzeOrderedDescendents(Label* histLabel, int index, uint64_t histPhase);
bool analyzeSyncChain(Label* label, int index);
bool analyzeMutualExclusion(const Record& histRecord, const Record& curRecord);
bool analyzeRaceCondition(const Record& histRecord, const Record& curRecord, 
                          bool& isHistBeforeCur, int& diffIndex);
bool analyzeTaskGroupSync(Label* histLabel, Label* curLabel, int index);

bool dispatchAnalysis(CheckCase checkCase, Label* hist, Label* cur, int index);
uint64_t computeExitRank(uint64_t phase);
uint64_t computeEnterRank(uint64_t phase);
inline CheckCase buildCheckCase(SegmentType histType, SegmentType curType);

RecordManagement manageAccessRecord(const Record& histRecord,
                                    const Record& curRecord, 
                                    bool isHistBeforeCur,
                                    int diffIndex);

void modifyAccessHistory(RecordManagement decision,
                         std::vector<Record>* records,
                         std::vector<Record>::iterator& cit);

bool isDupMemAccess(const CheckInfo& checkInfo, 
		    const std::shared_ptr<Label>& curLabel);

}
