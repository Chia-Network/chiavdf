CMAKE_MINIMUM_REQUIRED(VERSION 3.14 FATAL_ERROR)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

IF(NOT CMAKE_BUILD_TYPE)
  SET(CMAKE_BUILD_TYPE "RELEASE")
ENDIF()

project(chiavdf)

set(CMAKE_MODULE_PATH
    ${CMAKE_CURRENT_LIST_DIR}/cmake
    ${CMAKE_MODULE_PATH}
)

find_package(GMP REQUIRED)
find_package(GMPXX REQUIRED)

include_directories(
  ${INCLUDE_DIRECTORIES}
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${GMP_INCLUDE_DIR}
  ${GMPXX_INCLUDE_DIR}
)

set (CMAKE_CXX_FLAGS "-std=c++1z")

# CMake 3.14+
include(FetchContent)

FetchContent_Declare(
  pybind11-src
  GIT_REPOSITORY https://github.com/pybind/pybind11.git
  GIT_TAG        v2.5.0
)
FetchContent_MakeAvailable(pybind11-src)

pybind11_add_module(chiavdf
  ${CMAKE_CURRENT_SOURCE_DIR}/python_bindings/fastvdf.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/refcode/lzcnt.c
)

add_executable(verifier_test
  ${CMAKE_CURRENT_SOURCE_DIR}/verifier_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/refcode/lzcnt.c
)

target_link_libraries(chiavdf PRIVATE ${GMP_LIBRARIES} ${GMPXX_LIBRARIES} -pthread)
target_link_libraries(verifier_test ${GMP_LIBRARIES} ${GMPXX_LIBRARIES} -pthread)
