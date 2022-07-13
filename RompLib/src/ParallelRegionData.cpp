#include "ParallelRegionData.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>

/*
 * This function maintains task dependence relationship upon task dependence
 * callback. Task dependence forms a directed acyclic graph. 
 */
void ParallelRegionData::maintainTaskDependence(void* taskPtr, const ompt_dependence_t& dependence) {
  taskDependenceGraph.addDependence(taskPtr, dependence);   
}
