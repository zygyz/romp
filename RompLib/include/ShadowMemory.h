#pragma once
#include <cstdint>
#include <glog/logging.h>
#include <glog/raw_logging.h>

/*
 * This header file declares ShadowMemory class template for managing shadow 
 * memory. Type T is the type of struct of access history. We use class 
 * template here to decouple the implementation of shadow memory management
 * and the actual form of access history. We assume the shadow memory works
 * on 64 bits system. So we use uint64_t to represent void*
 */
#define CANONICAL_FORM_MASK 0x0000ffffffffffff
namespace romp {

enum Granularity {
  eByteLevel,
  eWordLevel, // aligned four bytes treated as the same memory access
  eLongWordLevel, // aligned eight bytes treated as the same memory access
};

template<typename T>
class ShadowMemory {

public:
  ShadowMemory(const uint64_t l1PageTableBits = 20, 
               const uint64_t l2PageTableBits = 12,
               const uint64_t numMemAddrBits = 48,
               Granularity granularity = eByteLevel);

  ~ShadowMemory();
public:
  T* getShadowMemorySlot(const uint64_t address);
  uint64_t getNumEntriesPerPage();

private:
  uint64_t _getPageIndex(const uint64_t address);
  uint64_t _genPageIndexMask(const uint64_t numBits, const uint64_t lowZeros);
  uint64_t _getL1PageIndex(const uint64_t address);
  uint64_t _getL2PageIndex(const uint64_t address);
  T* _getOrCreatePageForMemAddr(const uint64_t address);   

private:
  void*** _pageTable; 
  uint64_t _numEntriesPerPage;
  uint64_t _shadowPageIndexMask;
  uint64_t _pageOffsetShift;
  uint64_t _numL1PageTableEntries;
  uint64_t _numL2PageTableEntries;
  uint64_t _l1PageTableShift;
  uint64_t _l2PageTableShift;
  uint64_t _l2IndexMask;

private: 
  static thread_local void* _cachedShadowPage;
  static thread_local void** _cachedL1Page;
  void* _getShadowPage(const uint64_t numEntriesPerPage);
  void** _getL1Page(const uint64_t numL2PageTableEntries);
  void _saveShadowPage(void* shadowPage);
  void _saveL1Page(void** l1Page);
};

template<typename T>
thread_local void* ShadowMemory<T>::_cachedShadowPage = nullptr;

template<typename T>
thread_local void** ShadowMemory<T>::_cachedL1Page = nullptr;


/*
 * numMemAddrBits: number of effective bits in a memory address. For x86-64, 
 *                 the value is 48. 
 * l1PageTableBits: number of higher part of the memory address bits. This 
 *                 part of memory address is used as index into the first 
 *                 level page table.
 * l2PageTableBits: number of middle part of the memory address bits. This 
 *                 part of memory address is used as index into the second 
 *                 level page table. 
 * Each page contains 2^(numMemAddrBits - l1PageTableBits - l2PageTableBits)
 * entries. For byte level granularity, each byte is associated with its own
 * entry. For word level granularity, every aligned four bytes are associated 
 * with one entry. For long word level granularity, every aligned eight bytes 
 * are associated with one entry.
 */
template<typename T>
ShadowMemory<T>::ShadowMemory(const uint64_t l1PageTableBits,
                              const uint64_t l2PageTableBits,
                              const uint64_t numMemAddrBits, 
                              Granularity granularity) {
  uint64_t lowZeroMask = 0;
  switch(granularity) {
    case eByteLevel:
      lowZeroMask = 0;
      _pageOffsetShift = 0;
      break;
    case eWordLevel:
      lowZeroMask = 2;
      _pageOffsetShift = 2;
      break;
    case eLongWordLevel:
      lowZeroMask = 3;
      _pageOffsetShift = 3;
      break;
    default:
      lowZeroMask = 0;
      _pageOffsetShift = 0;
      break;
  }
  _l1PageTableShift = numMemAddrBits - l1PageTableBits;  
  _l2PageTableShift = _l1PageTableShift - l2PageTableBits; 
  _l2IndexMask = (1 << l2PageTableBits) - 1;

  _numEntriesPerPage = 1 << (_l2PageTableShift - lowZeroMask);  

  _shadowPageIndexMask = _genPageIndexMask(_l2PageTableShift, lowZeroMask);

  _numL1PageTableEntries = 1 << l1PageTableBits;
  _numL2PageTableEntries = 1 << l2PageTableBits;
     
  // For l1PageTableBits = 20, this allocates a chunk of memory of size 
  // 2^20 * 8 = 8 Mb, which is managable.
  auto tmp = calloc(1, sizeof(void**) * _numL1PageTableEntries);
  if (tmp == NULL) {
    LOG(FATAL) << "cannot create page table";
  }
  _pageTable = static_cast<void***>(tmp); 
}

template<typename T>
ShadowMemory<T>::~ShadowMemory() {
  // we should explicitly delete the shadow page
  for (int i = 0; i < _numL1PageTableEntries; ++i) {
    if (_pageTable[i] != 0) {
      for (int j = 0; j < _numL2PageTableEntries; ++j) {
        if (_pageTable[i][j] != 0) {
          free(_pageTable[i][j]); //free the leaf shadow page
        }
      }
      free(_pageTable[i]);
    }
  }
  free(_pageTable);
}

/* 
 * Given the memory address, using the high bits to get the index into the 
 * first level page table.
 * Note that if the memory address is represented in canonical form, bits
 * [48, 64] (lowest bit as bit 1), are copies of bit 47. One should first
 * mask out bits [48, 64] to avoid overflow of first level page index.
 */
template<typename T>
uint64_t ShadowMemory<T>::_getL1PageIndex(const uint64_t address) {  
  return static_cast<uint64_t>((address & CANONICAL_FORM_MASK) >> 
          _l1PageTableShift);
}

/*
 * Get the index to the second level page table, using the middle field of 
 * the address.
 */
template<typename T>
uint64_t ShadowMemory<T>::_getL2PageIndex(const uint64_t address) {
  return static_cast<uint64_t>((address >> _l2PageTableShift) & _l2IndexMask);
}

/*
 * Given the memory address, return the corresponding slot in shadow memory.
 */
template<typename T>
T* ShadowMemory<T>::getShadowMemorySlot(const uint64_t address) {
  auto pageBase = _getOrCreatePageForMemAddr(address);   
  auto pageIndex = _getPageIndex(address); 
  return static_cast<T*>(pageBase + pageIndex);
}


/* 
 * Given the memory address, return the shadow page containing the access 
 * history slot that is associated with the address.
 */
template<typename T>
T* ShadowMemory<T>::_getOrCreatePageForMemAddr(const uint64_t address) {
  auto l1Index = _getL1PageIndex(address);
  if (_pageTable[l1Index] == 0) { 
    // the first level page is not allocated yet.
    auto freshL1Page = _getL1Page(_numL2PageTableEntries);
    auto success = __sync_bool_compare_and_swap(&_pageTable[l1Index], 
                                                0, freshL1Page);
    if (!success) { // someone has already allocated this slot
      RAW_DLOG(INFO, "saving l1 page to cache");
      _saveL1Page(freshL1Page);
    }
  }
  // now get the shadow page
  auto l2Index = _getL2PageIndex(address);
  if (_pageTable[l1Index][l2Index] == 0) {
    auto freshShadowPage = _getShadowPage(_numEntriesPerPage);
    auto success = __sync_bool_compare_and_swap(&_pageTable[l1Index][l2Index],
                                             0, freshShadowPage);
    if (!success) {
      _saveShadowPage(freshShadowPage);
    }
  }
  return static_cast<T*>(_pageTable[l1Index][l2Index]);
}


template<typename T>
uint64_t ShadowMemory<T>::_getPageIndex(const uint64_t address) {
  return (address & _shadowPageIndexMask) >> _pageOffsetShift;
}


template<typename T>
uint64_t ShadowMemory<T>::getNumEntriesPerPage() {
  return _numEntriesPerPage;
}

/*
 * Helper function to get an allocation of l1 page, which is a array of 
 * pointers to shadow pages. Use thread local storage for a caching.
 */
template<typename T>
void** ShadowMemory<T>::_getL1Page(uint64_t numL2PageTableEntries) {
  void** result = nullptr;
  if (_cachedL1Page != nullptr) {
    result = _cachedL1Page;
    _cachedL1Page = nullptr;
  } else {
    // no cached l1 page available, create one
    auto tmp = calloc(1, sizeof(void*) * numL2PageTableEntries);
    if (tmp == NULL) {
      RAW_LOG(FATAL, "%s\n", "cannot allocate l1 page"); 
    }
    result = static_cast<void**>(tmp);
  }
  return result;
}

/*
 * Helper function to get an allocation of shadow page, which contains 
 * entries of access history type T. 
 */
template<typename T>
void* ShadowMemory<T>::_getShadowPage(const uint64_t numEntriesPerPage) {
  void* result;
  if (_cachedShadowPage != nullptr) {
    result = _cachedShadowPage;
    _cachedShadowPage = nullptr;
  } else { 
    auto tmp = calloc(1, sizeof(T) * numEntriesPerPage);
    if (tmp == NULL) {
      RAW_LOG(FATAL, "%s\n", "cannot allocate shadowpage");
    }
    result = static_cast<void*>(tmp);
  }
  return result;
}

/*
 * It is always expected that when this function is called, the cached pointer
 * is nullptr.
 */
template<typename T>
void ShadowMemory<T>::_saveL1Page(void** l1Page) {     
  if (_cachedL1Page) {
    RAW_LOG(ERROR, "%s %lx\n", "cached l1 page is not nullptr:", _cachedL1Page);
    return;
  }
  _cachedL1Page = l1Page;  
}

template<typename T>
void ShadowMemory<T>::_saveShadowPage(void* shadowPage) {     
  if (_cachedShadowPage) {
    RAW_LOG(ERROR, "%s\n", "cached shadow page is not nullptr!");
    return;
  }
  _cachedShadowPage = shadowPage;  
}


/* 
 * Generate the mask value that is composed of `numBits` consequtive bits,
 * while masking lowest `mask` bits as 0.
 */
template<typename T>
uint64_t ShadowMemory<T>::_genPageIndexMask(uint64_t numBits, uint64_t lowZeros) {
  return (1 << numBits) - (1 << lowZeros);
}

}


