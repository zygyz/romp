find_package(glog REQUIRED)

file(GLOB SOURCES src/*.cpp)

add_library(romp SHARED ${SOURCES})

find_library(SYMTABAPI "libsymtabAPI.so")
find_path(GLOG_PATH "glog/logging.h")
find_path(GFLAGS_PATH "gflags/gflags.h")
find_path(SYMTAB_PATH "Symtab.h")
find_path(BOOST_PATH "boost/shared_ptr.h")
find_library(GLOG_LIB glog)

target_include_directories(romp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include 
                           ${GLOG_PATH} 
                           ${GFLAGS_PATH}
                           ${SYMTAB_PATH}
                           ${BOOST_PATH})

target_link_libraries(romp ${SYMTABAPI})
target_link_libraries(romp ${GLOG_LIB})
install(TARGETS romp 
        LIBRARY DESTINATION lib)

