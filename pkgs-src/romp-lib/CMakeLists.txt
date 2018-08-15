cmake_minimum_required(VERSION 2.8)
project(omptrace)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O2 -std=c++11 -fpermissive -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free -L../../gperftools/gperftool-install/lib -L../../llvm-openmp/openmp/llvm-openmp-install/lib  -lpthread -ltcmalloc -lelf")
message(STATUS "cmake_cxx_flags = ${CMAKE_CXX_FLAGS}")
set(CMAKE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_PREFIX}/include)
set(CMAKE_INSTALL_LIBDIR ${CMAKE_INSTALL_PREFIX}/lib)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include ../../llvm-openmp/openmp/llvm-openmp-install/include)

file(GLOB SOURCES src/*.cpp)

message(STATUS "source = ${SOURCES}")
add_library(omptrace SHARED ${SOURCES})

install(TARGETS omptrace 
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/ompt_race.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
