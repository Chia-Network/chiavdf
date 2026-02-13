CMAKE_MINIMUM_REQUIRED(VERSION 3.14 FATAL_ERROR)
option(BUILD_CHIAVDFC "Build the chiavdfc shared library" OFF)
option(BUILD_PYTHON "Build the python bindings for chiavdf" ON)
option(BUILD_VDF_CLIENT "Build the vdf_client binary" OFF)
option(BUILD_VDF_BENCH "Build the vdf_bench binary" OFF)
option(BUILD_VDF_TESTS "Build vdf test binaries (1weso/2weso/prover)" OFF)
option(BUILD_HW_TOOLS "Build hardware timelord tools" OFF)
option(ENABLE_GNU_ASM "Enable GNU-style asm pipeline on x86/x64" ON)
option(GENERATE_ASM_TRACKING_DATA "Emit asm tracking instrumentation data from compile_asm" OFF)
option(HARDENING "Enable hardening" OFF)

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
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # MSVC does not provide the GNU intrinsics headers used by compile_asm.
    set(ENABLE_GNU_ASM OFF CACHE BOOL "Enable GNU-style asm pipeline on x86/x64" FORCE)
  endif()
  # clang-cl defaults to the MSVC linker (preferred for perf and compatibility).
else()
  add_compile_options("$<$<CONFIG:Debug>:-Og>")
endif()

find_package(Threads REQUIRED)

if(APPLE)
  add_compile_definitions(CHIAOSX)
endif()

if(WIN32)
  add_compile_definitions(CHIA_WINDOWS NOMINMAX WIN32_LEAN_AND_MEAN)
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

  find_library(
    MPIRXX_LIBRARY
    NAMES mpirxx libmpirxx gmpxx libgmpxx
    PATHS ${MPIR_LIBRARY_DIR} ${MPIR_LIBRARY_DIR}/lib ${MPIR_LIBRARY_DIR}/lib64
    NO_DEFAULT_PATH
  )
  if(MPIRXX_LIBRARY)
    message(STATUS "MPIRXX library found at ${MPIRXX_LIBRARY}")
    set(GMPXX_LIBRARIES ${MPIRXX_LIBRARY})
    link_libraries(${MPIRXX_LIBRARY})
  else()
    message(WARNING "MPIRXX library not found; gmpxx iostream operators may be missing")
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

set(VDF_COMMON_DEFINITIONS VDF_MODE=0 FAST_MACHINE=1)
if(GENERATE_ASM_TRACKING_DATA)
  list(APPEND VDF_COMMON_DEFINITIONS GENERATE_ASM_TRACKING_DATA=1)
endif()
set(VDF_COMMON_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/refcode/lzcnt.c
)
if(WIN32)
  list(APPEND VDF_COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/uint128_t/uint128_t.cpp
  )
endif()

set(VDF_ASM_SOURCES)
if(ENABLE_GNU_ASM AND (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|i[3-6]86)$"))
  enable_language(ASM)
  add_executable(compile_asm EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/compile_asm.cpp
  )
  target_compile_definitions(compile_asm PRIVATE ${VDF_COMMON_DEFINITIONS})
  if(WIN32)
    target_compile_definitions(compile_asm PRIVATE CHIA_WINDOWS)
  elseif(APPLE)
    target_compile_definitions(compile_asm PRIVATE CHIAOSX)
  endif()
  target_link_libraries(compile_asm PRIVATE ${GMP_LIBRARIES} ${GMPXX_LIBRARIES} Threads::Threads)
  if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    execute_process(
      COMMAND ${CMAKE_CXX_COMPILER} --print-resource-dir
      OUTPUT_VARIABLE CLANG_RESOURCE_DIR
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(CLANG_BUILTINS_LIB "${CLANG_RESOURCE_DIR}/lib/windows/clang_rt.builtins-x86_64.lib")
    if(EXISTS "${CLANG_BUILTINS_LIB}")
      target_link_libraries(compile_asm PRIVATE "${CLANG_BUILTINS_LIB}")
    else()
      message(FATAL_ERROR "clang_rt builtins not found: ${CLANG_BUILTINS_LIB}")
    endif()
  endif()

  set(ASM_COMPILED ${CMAKE_CURRENT_BINARY_DIR}/asm_compiled.s)
  set(ASM_AVX2_COMPILED ${CMAKE_CURRENT_BINARY_DIR}/avx2_asm_compiled.s)
  set(ASM_AVX512_COMPILED ${CMAKE_CURRENT_BINARY_DIR}/avx512_asm_compiled.s)

  set(ASM_ENV)
  if(WIN32)
    set(ASM_PATH "$ENV{PATH}")
    string(REPLACE ";" "\\;" ASM_PATH_ESC "${ASM_PATH}")
    set(ASM_ENV
      ${CMAKE_COMMAND} -E env
      "PATH=${CMAKE_CURRENT_SOURCE_DIR}/../mpir_gc_x64\\;${ASM_PATH_ESC}"
    )
  endif()

  add_custom_command(
    OUTPUT ${ASM_COMPILED}
    COMMAND ${ASM_ENV} compile_asm
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS compile_asm
  )
  add_custom_command(
    OUTPUT ${ASM_AVX2_COMPILED}
    COMMAND ${ASM_ENV} compile_asm avx2
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS compile_asm
  )
  add_custom_command(
    OUTPUT ${ASM_AVX512_COMPILED}
    COMMAND ${ASM_ENV} compile_asm avx512
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS compile_asm
  )

  set_source_files_properties(
    ${ASM_COMPILED}
    ${ASM_AVX2_COMPILED}
    ${ASM_AVX512_COMPILED}
    PROPERTIES GENERATED TRUE
  )

  list(APPEND VDF_ASM_SOURCES
    ${ASM_COMPILED}
    ${ASM_AVX2_COMPILED}
    ${ASM_AVX512_COMPILED}
  )
endif()

if(BUILD_PYTHON)
  FetchContent_Declare(
    pybind11-src
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG        v2.13.6
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
  if (WIN32)
    # workaround for constexpr mutex constructor change in MSVC 2022
    # https://stackoverflow.com/questions/78598141/first-stdmutexlock-crashes-in-application-built-with-latest-visual-studio
    target_compile_definitions(chiavdf PUBLIC _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
  endif()
endif()

set(HAVE_BOOST_HEADERS FALSE)
set(NEED_BOOST_HEADERS FALSE)
if(BUILD_VDF_CLIENT OR BUILD_VDF_TESTS)
  set(NEED_BOOST_HEADERS TRUE)
endif()

if(NEED_BOOST_HEADERS)
  if(WIN32)
    find_path(BOOST_INCLUDE_DIR boost/asio.hpp)
    if(BOOST_INCLUDE_DIR)
      set(HAVE_BOOST_HEADERS TRUE)
    endif()
  else()
    find_package(Boost REQUIRED)
    set(HAVE_BOOST_HEADERS TRUE)
  endif()
endif()

function(vdf_add_boost_includes target_name)
  if(NEED_BOOST_HEADERS AND HAVE_BOOST_HEADERS)
    if(WIN32)
      target_include_directories(${target_name} PRIVATE ${BOOST_INCLUDE_DIR})
    else()
      target_include_directories(${target_name} PRIVATE ${Boost_INCLUDE_DIRS})
    endif()
  endif()
endfunction()

function(vdf_add_windows_clang_opts target_name)
  if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if(MSVC)
      target_compile_options(
        ${target_name}
        PRIVATE
        "$<$<COMPILE_LANGUAGE:C,CXX>:/O2>"
        "$<$<COMPILE_LANGUAGE:C,CXX>:/fp:fast>"
        "$<$<COMPILE_LANGUAGE:C,CXX>:/clang:-fno-math-errno>"
      )
    else()
      target_compile_options(
        ${target_name}
        PRIVATE
        "$<$<COMPILE_LANGUAGE:C,CXX>:-O3>"
        "$<$<COMPILE_LANGUAGE:C,CXX>:-ffp-contract=fast>"
        "$<$<COMPILE_LANGUAGE:C,CXX>:-fno-math-errno>"
      )
    endif()
  endif()
endfunction()

if(BUILD_VDF_CLIENT)
  add_executable(vdf_client
    ${CMAKE_CURRENT_SOURCE_DIR}/vdf_client.cpp
  )
  target_sources(vdf_client PRIVATE ${VDF_COMMON_SOURCES} ${VDF_ASM_SOURCES})
  target_compile_definitions(vdf_client PRIVATE ${VDF_COMMON_DEFINITIONS})
  if(NOT HAVE_BOOST_HEADERS)
    message(FATAL_ERROR "Boost headers not found (needed for vdf_client)")
  endif()
  vdf_add_boost_includes(vdf_client)
  target_link_libraries(vdf_client PRIVATE ${GMP_LIBRARIES} ${GMPXX_LIBRARIES} Threads::Threads)
  vdf_add_windows_clang_opts(vdf_client)
endif()

if(BUILD_VDF_BENCH)
  add_executable(vdf_bench
    ${CMAKE_CURRENT_SOURCE_DIR}/vdf_bench.cpp
  )
  target_sources(vdf_bench PRIVATE ${VDF_COMMON_SOURCES} ${VDF_ASM_SOURCES})
  target_compile_definitions(vdf_bench PRIVATE ${VDF_COMMON_DEFINITIONS})
  target_link_libraries(vdf_bench PRIVATE ${GMP_LIBRARIES} ${GMPXX_LIBRARIES} Threads::Threads)
  vdf_add_windows_clang_opts(vdf_bench)
endif()

if(BUILD_VDF_TESTS)
  add_executable(1weso_test
    ${CMAKE_CURRENT_SOURCE_DIR}/1weso_test.cpp
  )
  target_sources(1weso_test PRIVATE ${VDF_COMMON_SOURCES} ${VDF_ASM_SOURCES})
  target_compile_definitions(1weso_test PRIVATE ${VDF_COMMON_DEFINITIONS})
  vdf_add_boost_includes(1weso_test)
  target_link_libraries(1weso_test PRIVATE ${GMP_LIBRARIES} ${GMPXX_LIBRARIES} Threads::Threads)
  vdf_add_windows_clang_opts(1weso_test)

  add_executable(2weso_test
    ${CMAKE_CURRENT_SOURCE_DIR}/2weso_test.cpp
  )
  target_sources(2weso_test PRIVATE ${VDF_COMMON_SOURCES} ${VDF_ASM_SOURCES})
  target_compile_definitions(2weso_test PRIVATE ${VDF_COMMON_DEFINITIONS})
  vdf_add_boost_includes(2weso_test)
  target_link_libraries(2weso_test PRIVATE ${GMP_LIBRARIES} ${GMPXX_LIBRARIES} Threads::Threads)
  vdf_add_windows_clang_opts(2weso_test)

  add_executable(prover_test
    ${CMAKE_CURRENT_SOURCE_DIR}/prover_test.cpp
  )
  target_sources(prover_test PRIVATE ${VDF_COMMON_SOURCES} ${VDF_ASM_SOURCES})
  target_compile_definitions(prover_test PRIVATE ${VDF_COMMON_DEFINITIONS})
  vdf_add_boost_includes(prover_test)
  target_link_libraries(prover_test PRIVATE ${GMP_LIBRARIES} ${GMPXX_LIBRARIES} Threads::Threads)
  vdf_add_windows_clang_opts(prover_test)

endif()

if(BUILD_HW_TOOLS)
  set(HW_COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/vdf_base.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hw/hw_util.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hw/hw_proof.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hw/hw_interface.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hw/chia_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hw/ftdi_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hw/vdf_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hw/pll_freqs.cpp
  )
  set(EMU_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/hw/emu_funcs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hw/emu_runner.cpp
  )
  set(HW_VDF_ASM_SOURCES ${VDF_ASM_SOURCES})
  if(WIN32)
    # Generated asm currently collides with lzcnt symbols for HW tool links on Windows.
    set(HW_VDF_ASM_SOURCES)
  endif()

  if(WIN32)
    set(HW_LIBS
      ${CMAKE_CURRENT_SOURCE_DIR}/hw/libft4222/libft4222.lib
      ${CMAKE_CURRENT_SOURCE_DIR}/hw/libft4222/ftd2xx.lib
    )
    set(HW_SOCKET_LIBS ws2_32)
  elseif(APPLE)
    set(HW_LIBS
      ${CMAKE_CURRENT_SOURCE_DIR}/hw/libft4222/libft4222.dylib
      ${CMAKE_CURRENT_SOURCE_DIR}/hw/libft4222/libftd2xx.dylib
    )
    set(HW_SOCKET_LIBS)
  else()
    set(HW_LIBS
      ${CMAKE_CURRENT_SOURCE_DIR}/hw/libft4222/build-x86_64/libft4222.so
    )
    set(HW_SOCKET_LIBS)
  endif()

  add_executable(hw_test
    ${CMAKE_CURRENT_SOURCE_DIR}/hw/hw_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hw/real_hw.cpp
  )
  target_sources(hw_test PRIVATE ${HW_COMMON_SOURCES} ${HW_VDF_ASM_SOURCES} ${VDF_COMMON_SOURCES})
  target_compile_definitions(hw_test PRIVATE ${VDF_COMMON_DEFINITIONS})
  target_include_directories(hw_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/hw ${CMAKE_CURRENT_SOURCE_DIR}/hw/libft4222)
  target_link_libraries(hw_test PRIVATE ${GMP_LIBRARIES} ${GMPXX_LIBRARIES} Threads::Threads ${HW_LIBS})
  if(APPLE)
    target_link_options(hw_test PRIVATE "-Wl,-rpath,@executable_path/hw/libft4222")
  endif()

  add_executable(hw_vdf_client
    ${CMAKE_CURRENT_SOURCE_DIR}/hw/hw_vdf_client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/hw/real_hw.cpp
  )
  target_sources(hw_vdf_client PRIVATE ${HW_COMMON_SOURCES} ${HW_VDF_ASM_SOURCES} ${VDF_COMMON_SOURCES})
  target_compile_definitions(hw_vdf_client PRIVATE ${VDF_COMMON_DEFINITIONS})
  target_include_directories(hw_vdf_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/hw ${CMAKE_CURRENT_SOURCE_DIR}/hw/libft4222)
  target_link_libraries(hw_vdf_client PRIVATE ${GMP_LIBRARIES} ${GMPXX_LIBRARIES} Threads::Threads ${HW_LIBS} ${HW_SOCKET_LIBS})
  if(APPLE)
    target_link_options(hw_vdf_client PRIVATE "-Wl,-rpath,@executable_path/hw/libft4222")
  endif()

  add_executable(emu_hw_test
    ${CMAKE_CURRENT_SOURCE_DIR}/hw/hw_test.cpp
  )
  target_sources(emu_hw_test PRIVATE ${HW_COMMON_SOURCES} ${EMU_SOURCES} ${HW_VDF_ASM_SOURCES} ${VDF_COMMON_SOURCES})
  target_compile_definitions(emu_hw_test PRIVATE ${VDF_COMMON_DEFINITIONS})
  target_include_directories(emu_hw_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/hw ${CMAKE_CURRENT_SOURCE_DIR}/hw/libft4222)
  target_link_libraries(emu_hw_test PRIVATE ${GMP_LIBRARIES} ${GMPXX_LIBRARIES} Threads::Threads)

  add_executable(emu_hw_vdf_client
    ${CMAKE_CURRENT_SOURCE_DIR}/hw/hw_vdf_client.cpp
  )
  target_sources(emu_hw_vdf_client PRIVATE ${HW_COMMON_SOURCES} ${EMU_SOURCES} ${HW_VDF_ASM_SOURCES} ${VDF_COMMON_SOURCES})
  target_compile_definitions(emu_hw_vdf_client PRIVATE ${VDF_COMMON_DEFINITIONS})
  target_include_directories(emu_hw_vdf_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/hw ${CMAKE_CURRENT_SOURCE_DIR}/hw/libft4222)
  target_link_libraries(emu_hw_vdf_client PRIVATE ${GMP_LIBRARIES} ${GMPXX_LIBRARIES} Threads::Threads ${HW_SOCKET_LIBS})
endif()

add_executable(verifier_test
  ${CMAKE_CURRENT_SOURCE_DIR}/verifier_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/refcode/lzcnt.c
)
add_executable(stress_test
  ${CMAKE_CURRENT_SOURCE_DIR}/stress_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/refcode/lzcnt.c
)

target_link_libraries(verifier_test PRIVATE ${GMP_LIBRARIES} ${GMPXX_LIBRARIES})
target_link_libraries(stress_test PRIVATE ${GMP_LIBRARIES} ${GMPXX_LIBRARIES})

if(UNIX)
  target_link_libraries(verifier_test PRIVATE -pthread)
  target_link_libraries(stress_test PRIVATE -pthread)
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

    if (HARDENING)
        target_compile_definitions(chiavdfc_shared PRIVATE "_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG")
        target_compile_definitions(chiavdfc_static PRIVATE "_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG")
    endif()

  set_target_properties(chiavdfc_shared PROPERTIES
          OUTPUT_NAME chiavdfc
          LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/shared$<0:>"
          RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/shared$<0:>"
  )

  set_target_properties(chiavdfc_static PROPERTIES
          OUTPUT_NAME chiavdfc
          ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/static$<0:>"
  )
endif()
