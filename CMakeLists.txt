cmake_minimum_required(VERSION 3.16)
project(loglens CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Optional: cmake -S . -B build -DSANITIZE=address,undefined   (or: thread)
set(SANITIZE "" CACHE STRING "Sanitizers to enable")
if(SANITIZE)
  add_compile_options(-fsanitize=${SANITIZE} -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=${SANITIZE})
endif()
add_compile_options(-Wall -Wextra)

find_package(Threads REQUIRED)

add_library(loglens_core src/tokenizer.cpp src/index.cpp src/query.cpp)
target_include_directories(loglens_core PUBLIC src)

add_executable(loglens src/main.cpp)
target_include_directories(loglens PRIVATE third_party)
target_link_libraries(loglens PRIVATE loglens_core Threads::Threads)

enable_testing()
add_executable(loglens_tests tests/test_main.cpp)
target_link_libraries(loglens_tests PRIVATE loglens_core)
add_test(NAME loglens_tests COMMAND loglens_tests)