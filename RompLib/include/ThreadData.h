#pragma once
#include <atomic>
#include <unordered_map>
#define ADDR_MAX 0xffffffffffff

typedef struct ThreadData {
  void* stackBaseAddress;
  void* stackTopAddress;
  void* lowestAccessedAddress;

  ThreadData() : stackBaseAddress(nullptr), 
                 stackTopAddress(nullptr), 
                 lowestAccessedAddress((void*)ADDR_MAX) {}

  void setLowestAddress(void* addr) {
    lowestAccessedAddress = addr;
  }

  void resetLowestAddress() {
    lowestAccessedAddress = (void*)ADDR_MAX;
  }
} ThreadData;
