cmake_minimum_required(VERSION 3.22)

project(ChaosLab
  VERSION 1.0.0
  DESCRIPTION "Adversarial systems laboratory for deterministic failure injection across distributed AI infrastructure"
  LANGUAGES CXX)

option(CHAOSLAB_BUILD_TESTS "Build Chaos Lab tests" ON)
option(CHAOSLAB_BUILD_CUDA "Build CUDA-backed chaos scenarios (requires CUDA toolkit)" ON)
option(CHAOSLAB_BUILD_CLI "Build the chaos CLI" ON)
option(CHAOSLAB_BUILD_TARGETS "Build target apps" ON)
option(CHAOSLAB_BUILD_EXAMPLES "Build runnable examples" ON)
option(CHAOSLAB_BUILD_BENCHMARKS "Build benchmark executables" ON)
option(CHAOSLAB_BUILD_PACKAGE "Generate installable package config" ON)

set(CHAOSLAB_SOVERSION 1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  # Scope C++ warning/security flags to the CXX language only: forwarding these
  # to nvcc as -Xcompiler produces nvcc's "single input file required" error.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /WX /permissive- /Zc:__cplusplus /EHsc /guard:cf /sdl")
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror -Wpedantic")
endif()
if(WIN32)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

include(GNUInstallDirs)

add_library(chaoslab STATIC
  src/digest.cpp
  src/random.cpp
  src/identity.cpp
  src/fault.cpp
  src/campaign.cpp
  src/scenario.cpp
  src/scheduler.cpp
  src/assertions.cpp
  src/evidence.cpp
  src/safety.cpp
  src/replay.cpp
  src/fuzz.cpp
  src/text.cpp
  src/result.cpp
)
target_include_directories(chaoslab
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_definitions(chaoslab PUBLIC CHAOSLAB_VERSION="${PROJECT_VERSION}")
set_target_properties(chaoslab PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${CHAOSLAB_SOVERSION} POSITION_INDEPENDENT_CODE ON)

add_library(chaoslab_os STATIC
  src/process.cpp
  src/transport.cpp
  src/tcp.cpp
  src/resource.cpp
  src/persistence.cpp
  src/authority.cpp
  src/windows_error.cpp
  src/runtime_protocol.cpp
)
target_link_libraries(chaoslab_os PUBLIC chaoslab)
if(WIN32)
  target_link_libraries(chaoslab_os PUBLIC ws2_32)
endif()
target_include_directories(chaoslab_os PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(CHAOSLAB_BUILD_CUDA)
  include(CheckLanguage)
  check_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)
    set(CMAKE_CUDA_ARCHITECTURES 120)
    # Override CMake's default CUDA debug/release flags: nvcc + MSVC trips on the
    # leading-space multi-flag -Xcompiler value ("single input file required").
    set(CMAKE_CUDA_FLAGS_DEBUG "-g")
    set(CMAKE_CUDA_FLAGS_RELEASE "-O3")
    set(CMAKE_CUDA_FLAGS_MIN_SIZE_RELEASE "-O2")
    add_library(chaoslab_cuda STATIC src/cuda.cpp src/cuda_kernels.cu)
    target_link_libraries(chaoslab_cuda PUBLIC chaoslab chaoslab_os CUDA::cudart)
    target_compile_definitions(chaoslab_cuda PUBLIC CHAOSLAB_HAS_CUDA=1)
  else()
    message(STATUS "CUDA compiler not found; building without CUDA-backed chaos scenarios.")
  endif()
endif()

if(CHAOSLAB_BUILD_CLI)
  add_executable(chaos_cli apps/chaos_cli.cpp)
  set_target_properties(chaos_cli PROPERTIES OUTPUT_NAME chaos)
  target_link_libraries(chaos_cli PRIVATE chaoslab chaoslab_os)
  if(TARGET chaoslab_cuda)
    target_link_libraries(chaos_cli PRIVATE chaoslab_cuda)
  endif()
endif()

if(CHAOSLAB_BUILD_TARGETS)
  add_executable(target_coordinator apps/target_coordinator.cpp)
  add_executable(target_worker apps/target_worker.cpp)
  target_link_libraries(target_coordinator PRIVATE chaoslab chaoslab_os)
  target_link_libraries(target_worker PRIVATE chaoslab chaoslab_os)
  if(TARGET chaoslab_cuda)
    target_link_libraries(target_worker PRIVATE chaoslab_cuda)
  endif()
endif()

if(CHAOSLAB_BUILD_EXAMPLES)
  add_executable(chaos_examples apps/chaos_examples.cpp)
  target_link_libraries(chaos_examples PRIVATE chaoslab chaoslab_os)
  if(TARGET chaoslab_cuda)
    target_link_libraries(chaos_examples PRIVATE chaoslab_cuda)
  endif()
endif()

if(CHAOSLAB_BUILD_BENCHMARKS)
  add_executable(chaos_bench apps/chaos_bench.cpp)
  target_link_libraries(chaos_bench PRIVATE chaoslab chaoslab_os)
endif()

if(CHAOSLAB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(CHAOSLAB_BUILD_PACKAGE)
  install(TARGETS chaoslab chaoslab_os EXPORT ChaosLabTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  if(TARGET chaoslab_cuda)
    install(TARGETS chaoslab_cuda EXPORT ChaosLabTargets
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
      INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  endif()
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(EXPORT ChaosLabTargets
    FILE ChaosLabTargets.cmake
    NAMESPACE chaoslab::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ChaosLab)
  include(CMakePackageConfigHelpers)
  configure_package_config_file(
    cmake/ChaosLabConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ChaosLabConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ChaosLab)
  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/ChaosLabConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/ChaosLabConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/ChaosLabConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ChaosLab)
endif()
