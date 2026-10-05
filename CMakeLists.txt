cmake_minimum_required(VERSION 3.25)

project(seek
    VERSION 0.1.0
    DESCRIPTION "Instant ranked search over your notes and PDFs"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SEEK_BUILD_TESTS "Build the unit tests" ON)
option(SEEK_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(SEEK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

if(SEEK_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(POPPLER_CPP REQUIRED IMPORTED_TARGET poppler-cpp)

# Strict warnings for our own code only (third-party headers come in as system includes).
add_library(seek_warnings INTERFACE)
target_compile_options(seek_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor
    $<$<BOOL:${SEEK_WARNINGS_AS_ERRORS}>:-Werror>)

# The search engine as a library, so the CLI and the tests share the exact same code.
add_library(seek_core
    src/extract.cpp
    src/index.cpp
    src/search.cpp
    src/snippet.cpp
    src/sync.cpp
    src/tokenizer.cpp)
target_include_directories(seek_core
    PUBLIC include
    PRIVATE src)
target_link_libraries(seek_core
    PRIVATE PkgConfig::POPPLER_CPP seek_warnings)

add_executable(seek src/main.cpp)
target_compile_definitions(seek PRIVATE SEEK_VERSION="${PROJECT_VERSION}")
target_link_libraries(seek PRIVATE seek_core seek_warnings)

install(TARGETS seek)

if(SEEK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
