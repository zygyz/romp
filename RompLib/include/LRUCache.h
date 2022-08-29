#pragma once
#include <cstdint>
#include <list>
#include <unordered_map>

typedef struct ListElement {
  ListElement(uint64_t key, int value): key(key), value(value) {}
  uint64_t key;
  int value;
}ListElement;

class LRUCache {
public:
LRUCache();
int get(uint64_t key);
void put(uint64_t key, int value);
void clear();
uint64_t size();
void setCapacity(uint64_t capacity);
private:
uint64_t mCapacity;
std::list<ListElement> mList;
std::unordered_map<uint64_t, std::list<ListElement>::iterator> mMap;
};
