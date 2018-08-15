#include<assert.h>
#include<atomic>
#include<mutex>
#include<stdint.h>
#include<cstdlib>
#include<sys/mman.h>
#include<sys/mman.h>
#include "mcs-lock.h"
#include "messages.h"
//#define BYTE_LEVEL
#define WORD_LEVEL

#ifdef BYTE_LEVEL
#define PAGE_OFFSET_BITS (16LL)
#define PAGE_OFFSET(addr) ( addr & 0xFFFF) 
#define PAGE_OFFSET_MASK ( 0xFFFF) 
#endif

#ifdef WORD_LEVEL

#define PAGE_OFFSET_BITS (14LL) // 4 bytes granularity
#define PAGE_OFFSET(addr) ( (addr & 0xFFFC) >> 2) // c = 0b1100  = 12 
#define PAGE_OFFSET_MASK ( 0xFFFC) 

#endif

#define NUM_ENTRY_SHADOW_PAGE (1 << PAGE_OFFSET_BITS) // number of entries in one page.

// 2 level page table
#define PTR_SIZE (sizeof(void*))
#define LAST_LEVEL_BITS (16LL)
#define LEVEL_1_PAGE_TABLE_BITS  (20LL)
#define LEVEL_1_PAGE_TABLE_ENTRIES  (1 << LEVEL_1_PAGE_TABLE_BITS )
#define LEVEL_1_PAGE_TABLE_SIZE  (LEVEL_1_PAGE_TABLE_ENTRIES * PTR_SIZE )

#define LEVEL_2_PAGE_TABLE_BITS  (12)
#define LEVEL_2_PAGE_TABLE_ENTRIES  (1 << LEVEL_2_PAGE_TABLE_BITS )
#define LEVEL_2_PAGE_TABLE_SIZE  (LEVEL_2_PAGE_TABLE_ENTRIES * PTR_SIZE )

#define LEVEL_1_PAGE_TABLE_SLOT(addr) ((((uint64_t)addr) >> (LEVEL_2_PAGE_TABLE_BITS + LAST_LEVEL_BITS)) & 0xfffff)
#define LEVEL_2_PAGE_TABLE_SLOT(addr) ((((uint64_t)addr) >> LAST_LEVEL_BITS) & 0xFFF)

#define SHADOW_STRUCT_SIZE (sizeof (T))

// All fwd declarations
static void** gL1PageTable[LEVEL_1_PAGE_TABLE_ENTRIES];

static __thread void* available_page;

// Given an address, returns the corresponding shadow address [IF HAS BEEN ALLOCATED]. This function does not allocate new shadow memory slot
template <class T>
static inline T* GetAllocatedShadowBaseAddress(void const * const address) {
    T * shadowPage;
    void*** l1Ptr = &gL1PageTable[LEVEL_1_PAGE_TABLE_SLOT(address)];
    if (*l1Ptr == 0) { // if the slot for address is allocated, should not get a zero here
        return nullptr; 
    } 
    if (((*l1Ptr)[LEVEL_2_PAGE_TABLE_SLOT(address)]) == 0) {
        return nullptr;
    }
    return (T*)((*l1Ptr)[LEVEL_2_PAGE_TABLE_SLOT(address)]);  
}
    

template <class T>
static inline 
void* GetShadowPage() 
{
    void* result;
    if (available_page != NULL) {
        result = available_page;
        available_page = NULL; 
    } else {
        result = (void*) mmap(NULL, NUM_ENTRY_SHADOW_PAGE * SHADOW_STRUCT_SIZE, PROT_WRITE | PROT_READ,   MAP_NORESERVE | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }
    return result;
}

static inline
void SaveShadowPage(void* page)
{
    available_page = page;
}

/*
static inline 
void** GetFirstLevelPageTable()
{
    void** result;
    if (first_level_page != NULL) {
        result = first_level_page;
        first_level_page = NULL;
    } else {
        result = (void**) calloc(1, LEVEL_2_PAGE_TABLE_SIZE);    
    }
    return result;
}

static inline
void SaveFirstLevelPage(void** page)
{
    first_level_page = page;
}
*/


template <class T>
static inline T * GetOrCreateShadowBaseAddress(void const * const address) {
    T * shadowPage;
    void*** l1Ptr = &gL1PageTable[LEVEL_1_PAGE_TABLE_SLOT(address)];
    if ( *l1Ptr == 0) {
        void** first_level_page = (void**) calloc(1, LEVEL_2_PAGE_TABLE_SIZE);  
        bool success = __sync_bool_compare_and_swap(l1Ptr, 0, first_level_page);
        if (!success) {
            free(first_level_page);
        }
        if(((*l1Ptr)[LEVEL_2_PAGE_TABLE_SLOT(address)]) == 0 ) { // now the shadow page
            void* shadow = GetShadowPage<T>();
            bool success = __sync_bool_compare_and_swap((&(*l1Ptr)[LEVEL_2_PAGE_TABLE_SLOT(address)]), 0, shadow); 
            if (!success) { // the shadow page has already been created 
                SaveShadowPage(shadow);
            } 
        }
    } else if (((*l1Ptr)[LEVEL_2_PAGE_TABLE_SLOT(address)]) == 0 ) {
        void* shadow = GetShadowPage<T>();
        bool success = __sync_bool_compare_and_swap((&(*l1Ptr)[LEVEL_2_PAGE_TABLE_SLOT(address)]), 0, shadow); 
        if (!success) { // the shadow page has already been created 
            SaveShadowPage(shadow);
        } 
    }
    shadowPage = (T *)((*l1Ptr)[LEVEL_2_PAGE_TABLE_SLOT(address)]);
    //assert(shadowPage != nullptr);            
    return shadowPage;
}

