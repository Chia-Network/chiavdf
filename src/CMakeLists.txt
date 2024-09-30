CMAKE_MINIMUM_REQUIRED(VERSION 3.14 FATAL_ERROR)
option(BUILD_CHIAVDFC "Build the chiavdfc shared library" OFF)
option(BUILD_PYTHON "Build the python bindings for chiavdf" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

IF(NOT CMAKE_BUILD_TYPE)
  SET(CMAKE_BUILD_TYPE "RELEASE")
ENDIF()

project(chiavdf)

set(CMAKE_MODULE_PATH
    ${CMAKE_CURRENT_LIST_DIR}/cmake
    ${CMAKE_MODULE_PATH}
)

if(MSVC)
  add_compile_options(/EHsc)
endif()

if(WIN32)
  set(MPIR_LIBRARY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../mpir_gc_x64")
  set(MPIR_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../mpir_gc_x64")
  include_directories(
    ${INCLUDE_DIRECTORIES}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MPIR_INCLUDE_DIR}
  )
  find_library(MPIR_LIBRARY NAMES mpir PATHS ${MPIR_LIBRARY_DIR} NO_DEFAULT_PATH)
  if(MPIR_LIBRARY)
    message(STATUS "MPIR library found at ${MPIR_LIBRARY}")
    link_libraries(${MPIR_LIBRARY})
  else()
    message(FATAL_ERROR "MPIR library not found")
  endif()

  list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../mpir_gc_x64")
else()
  find_package(GMP REQUIRED)
  find_package(GMPXX REQUIRED)

  include_directories(
    ${INCLUDE_DIRECTORIES}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${GMP_INCLUDE_DIR}
    ${GMPXX_INCLUDE_DIR}
  )
endif()

# CMake 3.14+
include(FetchContent)

if(BUILD_PYTHON)
  FetchContent_Declare(
    pybind11-src
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG        v2.11.1
  )
  FetchContent_MakeAvailable(pybind11-src)

  pybind11_add_module(chiavdf
    ${CMAKE_CURRENT_SOURCE_DIR}/python_bindings/fastvdf.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/refcode/lzcnt.c
  )

  target_link_libraries(chiavdf PRIVATE ${GMP_LIBRARIES} ${GMPXX_LIBRARIES})
  if(UNIX)
    target_link_libraries(chiavdf PRIVATE -pthread)
  endif()
endif()

add_executable(verifier_test
  ${CMAKE_CURRENT_SOURCE_DIR}/verifier_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/refcode/lzcnt.c
)
target_link_libraries(verifier_test PRIVATE ${GMP_LIBRARIES} ${GMPXX_LIBRARIES})

if(UNIX)
  target_link_libraries(verifier_test PRIVATE -pthread)
endif()

if(BUILD_CHIAVDFC)
  add_library(chiavdfc_shared SHARED
          ${CMAKE_CURRENT_SOURCE_DIR}/c_bindings/c_wrapper.cpp
          ${CMAKE_CURRENT_SOURCE_DIR}/refcode/lzcnt.c
  )
  add_library(chiavdfc_static STATIC
          ${CMAKE_CURRENT_SOURCE_DIR}/c_bindings/c_wrapper.cpp
          ${CMAKE_CURRENT_SOURCE_DIR}/refcode/lzcnt.c
  )
  target_link_libraries(chiavdfc_shared ${GMP_LIBRARIES} ${GMPXX_LIBRARIES})
  target_link_libraries(chiavdfc_static ${GMP_LIBRARIES} ${GMPXX_LIBRARIES})

  set_target_properties(chiavdfc_shared PROPERTIES
          OUTPUT_NAME chiavdfc
          LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/shared"
          RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/shared"
  )

  set_target_properties(chiavdfc_static PROPERTIES
          OUTPUT_NAME chiavdfc
          ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/static"
  )
endif()
