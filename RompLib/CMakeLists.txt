find_package(glog REQUIRED)

file(GLOB SOURCES src/*.cpp)

add_library(romp SHARED ${SOURCES})

find_library(SYMTABAPI "libsymtabAPI.so" REQUIRED)
find_path(GLOG_PATH "glog/logging.h" REQUIRED)
find_path(GFLAGS_PATH "gflags/gflags.h" REQUIRED)
find_path(SYMTAB_PATH "Symtab.h" REQUIRED)
find_path(BOOST_PATH "boost/shared_ptr.h" REQUIRED)
find_path(TBB_PATH "tbb/concurrent_hash_map.h" REQUIRED)
find_path(OMPT_PATH "omp-tools.h" REQUIRED)
find_library(GLOG_LIB glog REQUIRED)

message(STATUS "OMPT_PATH: ${OMPT_PATH}")

target_include_directories(romp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include 
                           ${GLOG_PATH} 
                           ${GFLAGS_PATH}
                           ${SYMTAB_PATH}
                           ${OMPT_PATH}
                           ${TBB_PATH}
                           ${BOOST_PATH})

target_link_libraries(romp ${SYMTABAPI})
target_link_libraries(romp ${GLOG_LIB})
install(TARGETS romp 
        LIBRARY DESTINATION lib)
