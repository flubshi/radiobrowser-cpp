# A minimal C++17 consumer used by CI to verify that the installed package
# (find_package) and the subproject integration (add_subdirectory /
# FetchContent) work.
cmake_minimum_required(VERSION 3.21)
project(radiobrowser_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(RADIOBROWSER_SOURCE_DIR "" CACHE PATH "Use this source tree via add_subdirectory")

if(RADIOBROWSER_SOURCE_DIR)
  add_subdirectory("${RADIOBROWSER_SOURCE_DIR}" radiobrowser)
else()
  find_package(radiobrowser CONFIG REQUIRED)
endif()

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE radiobrowser::radiobrowser)

enable_testing()
add_test(NAME consumer COMMAND consumer)
