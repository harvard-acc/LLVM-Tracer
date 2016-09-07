set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")

file(GLOB SRC "*.cpp")
add_library(full_trace SHARED ${SRC})
install(TARGETS full_trace LIBRARY DESTINATION lib)

# remove name prefix in order not to generate libxxxxxx name
set_target_properties(full_trace PROPERTIES PREFIX "")
