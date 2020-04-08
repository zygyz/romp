find_package(glog REQUIRED)

file(GLOB SOURCES src/*.cpp)

add_library(romp SHARED ${SOURCES})

find_library(SYMTABAPI "libsymtabAPI.so")
          
target_include_directories(romp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(romp glog ${SYMTABAPI})
install(TARGETS romp 
        LIBRARY DESTINATION lib)
