cmake_minimum_required(VERSION 3.25)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain")
endif()
project(FileWeaver VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/benchmark-build-info-$<CONFIG>.json" CONTENT
  "{\"build_type\":\"$<CONFIG>\",\"compiler_id\":\"${CMAKE_CXX_COMPILER_ID}\",\"compiler_version\":\"${CMAKE_CXX_COMPILER_VERSION}\",\"cxx_standard\":20,\"cmake_version\":\"${CMAKE_VERSION}\",\"system\":\"${CMAKE_SYSTEM_NAME}\",\"architecture\":\"${CMAKE_SYSTEM_PROCESSOR}\"}\n")

option(FILEWEAVER_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
find_package(CLI11 CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(Catch2 3 CONFIG REQUIRED)
add_library(fileweaver_core src/core/utc_time.cpp)
target_include_directories(fileweaver_core PUBLIC include)
add_library(fileweaver_storage src/database/initialize.cpp)
target_include_directories(fileweaver_storage PUBLIC include)
target_link_libraries(fileweaver_storage PRIVATE unofficial::sqlite3::sqlite3)
add_library(fileweaver_filesystem src/filesystem/scan.cpp)
target_include_directories(fileweaver_filesystem PUBLIC include)
target_link_libraries(fileweaver_storage PRIVATE fileweaver_filesystem)
add_executable(fileweaver apps/cli/main.cpp)
add_executable(search_core_benchmark benchmarks/search/core_search.cpp)
target_include_directories(search_core_benchmark PRIVATE apps/cli)
target_link_libraries(search_core_benchmark PRIVATE fileweaver_storage CLI11::CLI11 unofficial::sqlite3::sqlite3)
target_link_libraries(fileweaver PRIVATE fileweaver_storage fileweaver_core CLI11::CLI11)
if(WIN32)
  target_link_libraries(fileweaver PRIVATE shell32)
  target_link_libraries(search_core_benchmark PRIVATE shell32 psapi)
endif()
add_executable(storage_tests tests/integration/storage.cpp)
target_include_directories(storage_tests PRIVATE apps/cli)
target_link_libraries(storage_tests PRIVATE fileweaver_storage fileweaver_filesystem fileweaver_core Catch2::Catch2WithMain unofficial::sqlite3::sqlite3)
foreach(project_target IN ITEMS fileweaver search_core_benchmark fileweaver_storage fileweaver_filesystem fileweaver_core storage_tests)
if(MSVC)
  target_compile_options(${project_target} PRIVATE /W4 /WX /permissive- /utf-8)
else()
  target_compile_options(${project_target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
if(FILEWEAVER_SANITIZERS)
  if(WIN32 OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "Combined ASan/UBSan requires a supported Linux/macOS compiler")
  endif()
  target_compile_options(${project_target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(${project_target} PRIVATE -fsanitize=address,undefined)
endif()
endforeach()

enable_testing()
find_package(Python3 3.9 COMPONENTS Interpreter REQUIRED)
add_test(NAME cli-json-contract COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/json_contract.py" "$<TARGET_FILE:fileweaver>")
add_test(NAME unicode-contract COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/unicode_contract.py" "$<TARGET_FILE:fileweaver>")
add_test(NAME cli-memory-contract COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/cli_memory_contract.py")
add_test(NAME benchmark-contract COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/benchmark_contract.py" "$<TARGET_FILE:fileweaver>"
  "${CMAKE_BINARY_DIR}/benchmark-build-info-$<CONFIG>.json" "$<TARGET_FILE:search_core_benchmark>")
add_test(NAME nested-benchmark-contract COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/nested_benchmark_contract.py" "$<TARGET_FILE:fileweaver>"
  "${CMAKE_BINARY_DIR}/benchmark-build-info-$<CONFIG>.json" "$<TARGET_FILE:search_core_benchmark>")
add_test(NAME filter-matrix-contract COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/filter_matrix_contract.py" "$<TARGET_FILE:fileweaver>"
  "${CMAKE_BINARY_DIR}/benchmark-build-info-$<CONFIG>.json")
add_test(NAME incremental-filter-contract COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/incremental_filter_contract.py" "$<TARGET_FILE:fileweaver>"
  "${CMAKE_BINARY_DIR}/benchmark-build-info-$<CONFIG>.json")
add_test(NAME failure-recovery-contract COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/failure_recovery_contract.py" "$<TARGET_FILE:fileweaver>"
  "${CMAKE_BINARY_DIR}/benchmark-build-info-$<CONFIG>.json")
add_test(NAME prefix-contract COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/prefix_contract.py" "$<TARGET_FILE:fileweaver>")
add_test(NAME storage COMMAND storage_tests)
add_test(NAME cli-contract COMMAND "${CMAKE_COMMAND}"
  "-DCLI=$<TARGET_FILE:fileweaver>" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/cli.cmake")

set(PROJECT_SOURCES apps/cli/main.cpp apps/cli/output.hpp apps/cli/windows_io.hpp benchmarks/search/core_search.cpp benchmarks/search/process_memory.hpp src/database/initialize.cpp tests/integration/storage.cpp
  src/core/utc_time.cpp include/fileweaver/core/utc_time.hpp
  include/fileweaver/application/index.hpp
  include/fileweaver/application/inspect.hpp
  include/fileweaver/application/refresh.hpp
  include/fileweaver/application/search.hpp
  src/filesystem/scan.cpp include/fileweaver/filesystem/scan.hpp
  include/fileweaver/application/initialize.hpp include/fileweaver/core/storage_error.hpp)
list(TRANSFORM PROJECT_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
find_program(CLANG_FORMAT clang-format HINTS "${CMAKE_SOURCE_DIR}/build/tools/python/clang_format/data/bin")
find_program(CLANG_TIDY clang-tidy HINTS "${CMAKE_SOURCE_DIR}/build/tools/python/clang_tidy/data/bin")
if(CLANG_FORMAT)
  add_custom_target(format-check COMMAND "${CLANG_FORMAT}" --dry-run --Werror
    ${PROJECT_SOURCES} VERBATIM)
else()
  add_custom_target(format-check COMMAND "${CMAKE_COMMAND}" -E echo "clang-format is required"
    COMMAND "${CMAKE_COMMAND}" -E false)
endif()
if(CLANG_TIDY)
  set(TIDY_PLATFORM_ARGS)
  if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    list(APPEND TIDY_PLATFORM_ARGS "--extra-arg=--target=x86_64-w64-windows-gnu")
    foreach(include_dir IN LISTS CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
      # Clang supplies its own intrinsic headers; GCC's private intrinsics are
      # incompatible with the Clang frontend used by clang-tidy.
      if(include_dir MATCHES "/lib/gcc/.*/include(-fixed)?$")
        continue()
      endif()
      list(APPEND TIDY_PLATFORM_ARGS "--extra-arg=-isystem${include_dir}")
    endforeach()
  endif()
  add_custom_target(lint COMMAND "${CLANG_TIDY}" -p "${CMAKE_BINARY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/apps/cli/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/search/core_search.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/database/initialize.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/utc_time.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/filesystem/scan.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/storage.cpp" ${TIDY_PLATFORM_ARGS} --warnings-as-errors=* VERBATIM)
else()
  add_custom_target(lint COMMAND "${CMAKE_COMMAND}" -E echo "clang-tidy is required"
    COMMAND "${CMAKE_COMMAND}" -E false)
endif()
