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
  eImpWork = eImplicit | (eLogical << CASE_SHIFT),
  eExpImp = eExplicit | (eImplicit << CASE_SHIFT),
  eExpExp = eExplicit | (eExplicit << CASE_SHIFT),
  eExpWork = eExplicit | (eLogical<< CASE_SHIFT),
  eWorkImp = eLogical | (eImplicit << CASE_SHIFT),
  eWorkExp = eLogical | (eExplicit << CASE_SHIFT),
  eWorkWork = eLogical | (eLogical << CASE_SHIFT),
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
bool analyzeExplicitTask(Label* histLabel, Label* curLabel, int index);
bool analyzeOrderedDescendants(Label* histLabel, int index, uint64_t histPhase);
bool analyzeSyncChain(Label* label, int index);
bool analyzeMutualExclusion(const Record& histRecord, const Record& curRecord);
bool analyzeRaceCondition(const Record& histRecord, const Record& curRecord, bool& isHistBeforeCur, int& diffIndex, const uint64_t checkedByteAddress);
bool analyzeTaskGroupSync(Label* histLabel, Label* curLabel, int index);
uint64_t computeExitRank(uint64_t phase);
uint64_t computeEnterRank(uint64_t phase);
RecordManagement manageAccessRecord(const Record& histRecord, const Record& curRecord, bool isHistBeforeCur, int diffIndex);
void modifyAccessHistory(RecordManagement decision, std::vector<Record>* records, std::vector<Record>::iterator& cit);
}
