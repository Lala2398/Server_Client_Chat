cmake_minimum_required(VERSION 3.10)
project(ChatProgram C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Helpful warnings while learning.
add_compile_options(-Wall -Wextra)

# Link pthreads the portable CMake way.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include_directories(${PROJECT_SOURCE_DIR}/include)

add_executable(server src/server.c)
target_link_libraries(server PRIVATE Threads::Threads)

add_executable(client src/client.c)
target_link_libraries(client PRIVATE Threads::Threads)
