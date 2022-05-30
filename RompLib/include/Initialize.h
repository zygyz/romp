#pragma once
#include <atomic>
#include <glog/logging.h>
#include <omp-tools.h>
#include <stdlib.h>
#include <string>
#include <Symtab.h>

#include "Callbacks.h"
#include "CoreUtil.h"
#include "mcs-lock.h"
#include "TaskInfoQuery.h"

/* 
 * This header file defines functions that are used 
 * to initialize OMPT interface. 
*/

extern PerformanceCounters gPerformanceCounters;

bool gOmptInitialized = false; 
bool gDataRaceFound = false;
bool gReportLineInfo = false;
bool gReportAtRuntime = false;
bool gUseWordLevelCheck = false;
Dyninst::SymtabAPI::Symtab* gSymtabHandle = nullptr;

mcs_lock_t gDataRaceLock;
std::atomic_int gNumDataRace = 0;
std::vector<DataRaceInfo> gDataRaceRecords;

ompt_get_task_info_t omptGetTaskInfo;
ompt_get_parallel_info_t omptGetParallelInfo;
ompt_get_thread_data_t omptGetThreadData;
ompt_get_task_memory_t omptGetTaskMemory;

#define register_callback_t(name, type)                      \
do {                                                         \
  type f_##name = &on_##name;                                \
  printf("registering callback: '" #name "'\n");             \
  if (ompt_set_callback(name, (ompt_callback_t)f_##name) ==  \
      ompt_set_never)                                        \
    printf("Could not register callback: '" #name "'\n");    \
} while(0)

#define register_callback(name) register_callback_t(name, name##_t)

int omptInitialize(ompt_function_lookup_t lookup,
                   int initialDeviceNum,
                   ompt_data_t* toolData) {
  LOG(INFO) << "start initializing ompt";
  auto report_line_flag = getenv("ROMP_REPORT_LINE");
  if (report_line_flag != nullptr && std::string(report_line_flag) == "on") {
    gReportLineInfo = true;
  }
  auto report_flag = getenv("ROMP_REPORT");
  if (report_flag != nullptr && std::string(report_flag) == "on") {
    gReportAtRuntime = true;
  }
  auto word_level_flag = getenv("ROMP_WORD_LEVEL");
  if (word_level_flag != nullptr && std::string(word_level_flag) == "on") {
    gUseWordLevelCheck = true;
  }

  auto ompt_set_callback = 
      (ompt_set_callback_t)lookup("ompt_set_callback");

  register_callback_t(ompt_callback_mutex_acquired, ompt_callback_mutex_t);
  register_callback_t(ompt_callback_mutex_released, ompt_callback_mutex_t);
  register_callback_t(ompt_callback_reduction, ompt_callback_sync_region_t);
  register_callback(ompt_callback_implicit_task);
  register_callback(ompt_callback_sync_region);
  register_callback(ompt_callback_work);
  register_callback(ompt_callback_parallel_begin);
  register_callback(ompt_callback_parallel_end);
  register_callback(ompt_callback_task_create);
  register_callback(ompt_callback_task_schedule);
  register_callback(ompt_callback_dependences);
  register_callback(ompt_callback_thread_begin);
  register_callback(ompt_callback_thread_end);
  register_callback(ompt_callback_dispatch);
  register_callback(ompt_callback_task_dependence);

  omptGetTaskInfo = (ompt_get_task_info_t)lookup("ompt_get_task_info");
  omptGetParallelInfo = (ompt_get_parallel_info_t)lookup("ompt_get_parallel_info");
  omptGetThreadData = (ompt_get_thread_data_t)lookup("ompt_get_thread_data");
  omptGetTaskMemory = (ompt_get_task_memory_t)lookup("ompt_get_task_memory"); 

  gOmptInitialized = true;
  LOG(INFO) << "ompt intialized";
  return 1;
}

void omptFinalize(ompt_data_t* toolData) {
  LOG(INFO) << "finalizing ompt";
  if (gDataRaceFound) {
    LOG(INFO) << "data race found:";
    if (gReportLineInfo) {
      for (const auto& info : gDataRaceRecords) {
        reportDataRaceWithLineInfo(info, gSymtabHandle);
      } 
    }
  } else {
    LOG(INFO) << "data race not found";
  }
#ifdef PERFORMANCE
  gPerformanceCounters.printPerformanceCounters();
#endif
}

