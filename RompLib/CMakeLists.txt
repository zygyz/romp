find_package(glog REQUIRED)

file(GLOB SOURCES src/*.cpp)

add_library(omptrace SHARED ${SOURCES})

find_path(LLVM_PATH omp.h)                    
find_path(GLOG_PATH "glog/logging.h")
find_path(GFLAGS_PATH "gflags/gflags.h")
find_path(SYMTABAPI_PATH "Symtab.h")
find_library(SYMTABAPI "libsymtabAPI.so")
          
target_include_directories(omptrace PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include  
                                    PUBLIC ${LLVM_PATH}
                                    PUBLIC ${GLOG_PATH}
                                    PUBLIC ${GFLAGS_PATH}
                                    PUBLIC ${SYMTABAPI_PATH})

target_link_libraries(omptrace glog ${SYMTABAPI})
install(TARGETS omptrace 
        LIBRARY DESTINATION lib)
