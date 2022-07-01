#pragma once 
#include "AccessControl.h"
#include "AccessHistory.h"
#include "CoreUtil.h"
#include "LockSet.h"
#include "RecordManagement.h"
#include "TaskData.h"

bool happensBefore(Label* histLabel, Label* curLabel, int& diffIndex, TaskData* histTaskData, TaskData* curTaskData, RecordManagementInfo& recordManagementInfo);
bool analyzeSiblingImplicitTask(Label* histLabel, Label* curLabel, int index, RecordManagementInfo& recordManagementInfo);
bool analyzeSameTask(Label* histLabel, Label* curLabel, int index, RecordManagementInfo& recordManagementInfo);
bool analyzeOrderedSection(Label* histLabel, Label* curLabel, int index, bool isFromSiblingImplicitTasks, RecordManagementInfo& recordManagementInfo);
bool analyzeExplicitTask(Label* histLabel, Label* curLabel, int index, RecordManagementInfo& recordManagementInfo);
bool analyzeOrderedDescendants(Label* histLabel, int index, uint64_t histPhase, RecordManagementInfo& recordManagementInfo);
bool analyzeExplicitTaskSynchronizationWithTaskWait(Label* label, int index, RecordManagementInfo& recordManagementInfo);
bool analyzeMutualExclusion(const Record& histRecord, const Record& curRecord, RecordManagementInfo& recordManagementInfo);
bool analyzeRaceCondition(const Record& histRecord, const Record& curRecord, RecordManagementInfo& recordManagementInfo);
bool analyzeTaskGroupSync(Label* histLabel, Label* curLabel, int index);
uint64_t computeExitRank(uint64_t phase);
uint64_t computeEnterRank(uint64_t phase);
bool manageAccessRecords(AccessHistory* accessHistory, const Record& currentRecord, ReaderWriterLockGuard& lockGuard, std::vector<RecordManagementInfo>& info);
bool checkDataRaceForMemoryAddress(uint64_t checkedAddress, AccessHistory* accessHistory, const Record& accessRecord, std::vector<RecordManagementInfo>& recordManagementInfo);
void* setMemoryOwner(AccessHistory* accessHistory, int dataSharingType, void* taskData, void* memoryAddress);
