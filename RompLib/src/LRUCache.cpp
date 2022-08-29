#include "LRUCache.h"

#include <glog/logging.h>
#include <glog/raw_logging.h>

extern uint64_t gLRUCapacity;

LRUCache::LRUCache() {
  mCapacity = gLRUCapacity;
}

void LRUCache::clear() {
  mMap.clear();
  mList.clear();
}

uint64_t LRUCache::size() {
  return mList.size();
}

// return -1 if not found. Return 1 if is memory access is write , return 0 if memory access is read
int LRUCache::get(uint64_t key) {
  if (mMap.find(key) == mMap.end()) {
    return -1; 
  } 
  auto it = mMap[key];
  auto element = *it;
  mList.erase(it);
  mList.push_back(element);
  it = mList.end();
  it--; 
  mMap[key] = it;
  return element.value;
}

void LRUCache::put(uint64_t key, int value) {
  if (mMap.find(key) != mMap.end()) {
    // update the value 
    auto it = mMap[key];
    mList.erase(it);
    mList.push_back(ListElement(key, value)); 
    it = mList.end();
    it--;
    mMap[key] = it;
  } else {
    // insert the value  
    if (mMap.size() == mCapacity) {
      // reaches capcity 
      // remove the first one 
      auto it = mList.begin();
      auto oldestElementKey = it->key;
      mList.erase(it);   
      mMap.erase(oldestElementKey); 
    }
    mList.push_back(ListElement(key, value));
    auto it = mList.end();
    it--;
    mMap[key] = it;
 }
}
