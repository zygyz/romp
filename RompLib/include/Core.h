#pragma once 
#include "AccessControl.h"
#include "AccessHistory.h"
#include "CoreUtil.h"
#include "LockSet.h"
#include "RecordManagement.h"
#include "TaskData.h"

bool happensBefore(Label* histLabel, Label* curLabel, int& diffIndex);
bool analyzeSiblingImplicitTask(Label* histLabel, Label* curLabel, int index);
bool analyzeSameTask(Label* histLabel, Label* curLabel, int index);
bool analyzeOrderedSection(Label* histLabel, Label* curLabel, int index);
bool analyzeExplicitTask(Label* histLabel, Label* curLabel, int index);
bool analyzeOrderedDescendants(Label* histLabel, int index, uint64_t histPhase);
bool analyzeSyncChain(Label* label, int index);
bool analyzeMutualExclusion(const Record& histRecord, const Record& curRecord);
bool analyzeRaceCondition(const Record& histRecord, const Record& curRecord, const uint64_t checkedByteAddress, RecordManagementInfo& recordManagementInfo);
bool analyzeTaskGroupSync(Label* histLabel, Label* curLabel, int index);
uint64_t computeExitRank(uint64_t phase);
uint64_t computeEnterRank(uint64_t phase);
void manageAccessRecords(AccessHistory* accessHistory, const Record& currentRecord, ReaderWriterLockGuard& lockGuard);
bool modifyAccessHistory(AccessHistoryManagementDecision decision, std::vector<Record>* records, std::vector<Record>::iterator& cit, ReaderWriterLockGuard* guard);
bool checkDataRaceForMemoryAddress(uint64_t checkedAddress, AccessHistory* accessHistory, const Record& accessRecord, std::vector<RecordManagementInfo>& recordManagementInfo);
