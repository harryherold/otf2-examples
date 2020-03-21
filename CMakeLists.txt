cmake_minimum_required(VERSION 3.10)

# set the project name
project(otf2_pthread_read)

link_directories(/home/cherold/procs/otf2-2.2/lib)

# add the executable
add_executable(otf2_pthread_read otf2_pthread_read.cpp)


find_package(OpenMP)
if(OpenMP_CXX_FOUND)
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
endif()

include_directories(/home/cherold/procs/otf2-2.2/include)
target_link_libraries(otf2_pthread_read otf2)