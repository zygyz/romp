#pragma once 
#include "AccessControl.h"
#include "AccessHistory.h"
#include "CoreUtil.h"
#include "LockSet.h"
#include "RecordManagement.h"
#include "TaskData.h"

bool happensBefore(Label* histLabel, Label* curLabel, int& diffIndex, RecordManagementInfo& recordManagementInfo);
bool analyzeSiblingImplicitTask(Label* histLabel, Label* curLabel, int index, RecordManagementInfo& recordManagementInfo);
bool analyzeSameTask(Label* histLabel, Label* curLabel, int index, RecordManagementInfo& recordManagementInfo);
bool analyzeOrderedSection(Label* histLabel, Label* curLabel, int index, RecordManagementInfo& recordManagementInfo);
bool analyzeOrderedDescendants(Label* histLabel, int index, uint64_t histPhase, RecordManagementInfo& recordManagementInfo);
bool analyzeExplicitTaskSynchronizationWithTaskWait(Label* label, int index, RecordManagementInfo& recordManagementInfo);
bool analyzeMutualExclusion(const Record& histRecord, const Record& curRecord, RecordManagementInfo& recordManagementInfo);
bool analyzeRaceCondition(const Record& histRecord, const Record& curRecord, const uint64_t checkedByteAddress, RecordManagementInfo& recordManagementInfo);
bool analyzeTaskGroupSync(Label* histLabel, Label* curLabel, int index);
uint64_t computeExitRank(uint64_t phase);
uint64_t computeEnterRank(uint64_t phase);
void manageAccessRecords(AccessHistory* accessHistory, const Record& currentRecord, ReaderWriterLockGuard& lockGuard, std::vector<RecordManagementInfo>& info);
bool checkDataRaceForMemoryAddress(uint64_t checkedAddress, AccessHistory* accessHistory, const Record& accessRecord, std::vector<RecordManagementInfo>& recordManagementInfo);
