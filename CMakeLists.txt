cmake_minimum_required(VERSION 3.20)

project(OpenFHE_test LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# change openfhe install directory from /mnt/d/openfhe_install to /usr/local
set(OPENFHE_INSTALL_DIR "/usr/local" CACHE PATH "Path to OpenFHE install")
set(CMAKE_PREFIX_PATH "${OPENFHE_INSTALL_DIR}" ${CMAKE_PREFIX_PATH})
find_package(OpenFHE REQUIRED)
set(CMAKE_BUILD_RPATH "${OPENFHE_INSTALL_DIR}/lib")

# --- NEW: fetch Google Benchmark automatically ---
include(FetchContent)
FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.1
)
# Disable benchmark's own test suite so it doesn't try to pull in googletest too
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
# Optional but recommended: silence benchmark's install() rules so it doesn't
# interfere with your own project's install step, if you have one
# set(BENCHMARK_ENABLE_LIBPFM ON CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)


#function for adding executable for a class
function(add_openfhe_executable target source)
    add_executable(${target} ${source})

    target_link_options(${target} PRIVATE "-Wl,--disable-new-dtags")

    target_include_directories(${target} PRIVATE
        ${OpenFHE_INCLUDE}
        ${OpenFHE_INCLUDE}/pke
        ${OpenFHE_INCLUDE}/core
        ${OpenFHE_INCLUDE}/binfhe
        ${OpenFHE_INCLUDE}/cereal
    )

    target_link_libraries(${target} PRIVATE
        OPENFHEpke
        OPENFHEcore
        OPENFHEbinfhe
        benchmark::benchmark
    )
endfunction()


#add_openfhe_executable(openfhe_test main.cpp)
#add_openfhe_executable(treshold examples/thresholdCKKS.cpp)
#add_openfhe_executable(lib_test examples/openFHE_lib.cpp)
#add_openfhe_executable(treshBootstrap examples/tresholdBootstrap.cpp)
#add_openfhe_executable(primitives benchmarks/primitives.cpp)
add_openfhe_executable(tckksBoot benchmarks/tckksBootstrapping2.cpp)
add_openfhe_executable(bootstrapping examples/bootstrapping2.cpp)
add_openfhe_executable(tckksRescale benchmarks/tckksRescale.cpp)
add_openfhe_executable(tckksRelin benchmarks/tckksRelin.cpp)
add_openfhe_executable(tckksBootAdjusted benchmarks/tckksBootAdjusted.cpp)

