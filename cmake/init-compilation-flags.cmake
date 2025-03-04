include_guard(GLOBAL)

if(CMAKE_CXX_COMPILER_ID MATCHES Clang)
    check_compiler_version(clang 10.0.0)
    set(COMPILER_CLANG True)
elseif(CMAKE_CXX_COMPILER_ID MATCHES GNU)
    check_compiler_version(gcc 8.3.0)
    set(COMPILER_GCC True)
endif()

set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to conform to")
set(CMAKE_CXX_EXTENSIONS OFF)
if (CMAKE_CXX_STANDARD LESS 17)
    message(FATAL_ERROR "c++17 expected at least!")
endif()
cmake_print_variables(CMAKE_CXX_STANDARD)

if(APPLE)
    include_directories(/usr/local/include)
    add_definitions(-D_XOPEN_SOURCE)

    set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl" CACHE INTERNAL "")
endif()

find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})

option(ADDRESS_SANITIZER "Enable address sanitizer")
if(ADDRESS_SANITIZER)
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)
    add_definitions(-DASAN_ENABLED=1)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type (default Debug)" FORCE)
endif()

option(UNDEFINED_SANITIZER "Enable undefined sanitizer")
if(UNDEFINED_SANITIZER)
    add_compile_options(-fsanitize=undefined -fno-sanitize-recover=all)
    add_link_options(-fsanitize=undefined)
    add_definitions(-DUSAN_ENABLED=1)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type (default Debug)" FORCE)
endif()
cmake_print_variables(ADDRESS_SANITIZER UNDEFINED_SANITIZER)

option(KPHP_TESTS "Build the tests" ON)
cmake_print_variables(KPHP_TESTS)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to `${DEFAULT_BUILD_TYPE}` as none was specified.")
    set(CMAKE_BUILD_TYPE ${DEFAULT_BUILD_TYPE} CACHE STRING "Build type (default ${DEFAULT_BUILD_TYPE})" FORCE)
endif()

cmake_print_variables(CMAKE_BUILD_TYPE)
if("${CMAKE_BUILD_TYPE}" STREQUAL ${DEFAULT_BUILD_TYPE})
    add_compile_options(-O3)
endif()

if(DEFINED ENV{FAST_COMPILATION_FMT})
    add_definitions(-DFAST_COMPILATION_FMT)
    message(STATUS FAST_COMPILATION_FMT="ON")
endif()

if(NOT DEFINED ENV{ENABLE_GRPROF})
    add_compile_options(-fdata-sections -ffunction-sections)
    if(APPLE)
        add_link_options(-Wl,-dead_strip)
    else()
        add_link_options(-Wl,--gc-sections)
    endif()
endif()

include_directories(${GENERATED_DIR})
add_compile_options(-fwrapv -fno-strict-aliasing -fno-stack-protector -ggdb -fno-omit-frame-pointer)
if(HOST STREQUAL "x86_64")
    add_compile_options(-mpclmul -march=nehalem -fno-common)
    add_link_options(-fno-common)
endif()

add_compile_options(-Werror -Wall -Wextra -Wunused-function -Wfloat-conversion -Wno-sign-compare
                    -Wuninitialized -Wno-redundant-move -Wno-missing-field-initializers)

if(NOT APPLE)
    check_cxx_compiler_flag(-gz=zlib DEBUG_COMPRESSION_IS_FOUND)
    if (DEBUG_COMPRESSION_IS_FOUND)
        add_compile_options(-gz=zlib)
        add_link_options(-Wl,--compress-debug-sections=zlib)
    endif()
endif()

add_link_options(-rdynamic -L/usr/local/lib -ggdb)
add_definitions(-D_GNU_SOURCE)
# prevents the `build` directory to be appeared in symbols, it's necessary for remote debugging with path mappings
add_compile_options(-fdebug-prefix-map="${CMAKE_BINARY_DIR}=${CMAKE_SOURCE_DIR}")
