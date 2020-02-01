option(CUSTOM_DYNINST "use custom dyninst build" OFF)


add_executable(InstrumentMain InstrumentMain.cpp 
                              InstrumentClient.cpp)
  
if (CUSTOM_DYNINST MATCHES "ON")
    # find the dyninst install directory by searching BPatch.h 
    find_path(DYNINST_INCLUDE_PATH BPatch.h) 
    find_path(GFLAGS_PATH "gflags/gflags.h")  
    find_path(GLOG_PATH  "glog/logging.h")
    include_directories(${DYNINST_INCLUDE_PATH}
                        ${GFLAGS_PATH}
                        ${GLOG_PATH})
    find_library(DYNINST_LIB dyninstAPI)
    find_library(INSTRUCTION_LIB instructionAPI)
    find_library(SYMTAB_LIB symtabAPI)
    find_library(PARSE_LIB parseAPI)
    find_library(BOOST_SYSTEM_LIB boost_system)
    find_library(GFLAGS_LIB gflags)
    find_library(GLOG_LIB glog)
    target_link_libraries(InstrumentMain "${DYNINST_LIB}")
    target_link_libraries(InstrumentMain "${INSTRUCTION_LIB}")
    target_link_libraries(InstrumentMain "${SYMTAB_LIB}")
    target_link_libraries(InstrumentMain "${PARSE_LIB}")
    target_link_libraries(InstrumentMain "${BOOST_SYSTEM_LIB}")
    target_link_libraries(InstrumentMain "${GFLAGS_LIB}") 
    target_link_libraries(InstrumentMain "${GLOG_LIB}") 
else()
  find_package(gflags REQUIRED)
  find_package(glog REQUIRED)
  find_package(Boost REQUIRED)
  find_package(Dyninst REQUIRED dyninstAPI symtabAPI instructionAPI parseAPI)
  target_link_libraries(InstrumentMain dyninstAPI parseAPI symtabAPI 
                        instructionAPI gflags glog) 
endif()

install(TARGETS InstrumentMain DESTINATION bin)
