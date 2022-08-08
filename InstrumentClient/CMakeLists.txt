cmake_minimum_required(VERSION 3.20)
add_executable(InstrumentMain InstrumentMain.cpp 
                              InstrumentClient.cpp)
find_path(GFLAGS_PATH "gflags/gflags.h" REQUIRED)  
find_path(GLOG_PATH  "glog/logging.h" REQUIRED)
find_path(BOOST_PATH "boost/atomic.hpp" REQUIRED)
find_path(TBB_PATH "tbb/concurrent_hash_map.h" REQUIRED)
find_path(BPATCH_PATH "BPatch.h" REQUIRED)
include_directories(${DYNINST_INCLUDE_PATH}
                      ${GFLAGS_PATH}
                      ${BOOST_PATH}
                      ${GLOG_PATH}
                      ${TBB_PATH}
                      ${BPATCH_PATH})
find_library(COMMON common)
find_library(DYNINST_LIB dyninstAPI)
find_library(INSTRUCTION_LIB instructionAPI)
find_library(SYMTAB_LIB symtabAPI)
find_library(PARSE_LIB parseAPI)
find_library(BOOST_SYSTEM_LIB boost_system)
find_library(GFLAGS_LIB gflags REQUIRED)
find_library(GLOG_LIB glog REQUIRED)
message(STATUS "GFLAGS_LIB: ${GFLAGS_LIB}")
target_link_libraries(InstrumentMain "${COMMON}")
target_link_libraries(InstrumentMain "${DYNINST_LIB}")
target_link_libraries(InstrumentMain "${INSTRUCTION_LIB}")
target_link_libraries(InstrumentMain "${SYMTAB_LIB}")
target_link_libraries(InstrumentMain "${PARSE_LIB}")
target_link_libraries(InstrumentMain "${BOOST_SYSTEM_LIB}")
target_link_libraries(InstrumentMain "${GFLAGS_LIB}") 
target_link_libraries(InstrumentMain "${GLOG_LIB}") 

install(TARGETS InstrumentMain DESTINATION bin)
