#include "ParallelRegionData.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>

namespace romp {

/*
 * This function maintains task dependence relationship upon task dependence
 * callback. Task dependence forms a directed acyclic graph. 
 */
void maintainTaskDeps(const ompt_dependence_t& dependence, 
		      void* taskPtr,
		      ParRegionData* parRegionData) {
  parRegionData->taskDepGraph.addDeps(dependence, taskPtr);   
}

}
