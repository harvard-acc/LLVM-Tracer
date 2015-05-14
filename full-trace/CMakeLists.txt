set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -O3")

file(GLOB SRC "*.cpp")
add_library(full_trace SHARED ${SRC})

# remove name prefix in order not to generate libxxxxxx name
set_target_properties(full_trace PROPERTIES PREFIX "")

if(${BUILD_ON_SOURCE})
  set_target_properties(full_trace PROPERTIES LIBRARY_OUTPUT_DIRECTORY
                         "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
