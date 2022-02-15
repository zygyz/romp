#include "ParallelRegionData.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>

/*
 * This function maintains task dependence relationship upon task dependence
 * callback. Task dependence forms a directed acyclic graph. 
 */
void maintainTaskDeps(const ompt_dependence_t& dependence, void* taskPtr, ParallelRegionData* parRegionData) {
  parRegionData->taskDepGraph.addDeps(dependence, taskPtr);   
}
