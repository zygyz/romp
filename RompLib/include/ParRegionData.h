#pragma once

namespace romp {

typedef struct ParRegionData {
  void* dataPtr;  // TODO: define what is needed in the struct
  unsigned int numParallelism;
  int parallelFlag;
  ParRegionData() {}
  ParRegionData(unsigned int n, int p): numParallelism(n), parallelFlag(p) {
    dataPtr = nullptr; 
  } 
} ParRegionData;

}
