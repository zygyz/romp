#pragma once 
#include "AccessHistory.h"
#include "CoreUtil.h"
#include "LockSet.h"
#include "TaskData.h"

enum AccessHistoryManagementDecision{
  eNoOperation,
  eSkipAddCurrentRecord,
  eDeleteHistoryRecord,
};

bool happensBefore(Label* histLabel, Label* curLabel, int& diffIndex, TaskData* histTaskData, TaskData* curTaskData, uint64_t memAddr);
bool analyzeSiblingImplicitTask(Label* histLabel, Label* curLabel, int index);
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
AccessHistoryManagementDecision manageAccessRecord(const Record& histRecord, const Record& curRecord, bool isHistBeforeCur, int diffIndex);
void modifyAccessHistory(AccessHistoryManagementDecision decision, std::vector<Record>* records, std::vector<Record>::iterator& cit);
void* setMemoryOwner(AccessHistory* accessHistory, int dataSharingType, void* taskData, void* memoryAddress);
