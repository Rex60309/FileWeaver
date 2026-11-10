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
option(FILEWEAVER_BUILD_DESKTOP "Build opt-in Windows Qt search window" OFF)
find_package(CLI11 CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(Catch2 3 CONFIG REQUIRED)
add_library(fileweaver_core src/core/utc_time.cpp src/core/fuzzy.cpp)
target_include_directories(fileweaver_core PUBLIC include)
add_library(fileweaver_storage src/database/initialize.cpp)
target_include_directories(fileweaver_storage PUBLIC include)
target_link_libraries(fileweaver_storage PRIVATE unofficial::sqlite3::sqlite3)
add_library(fileweaver_filesystem src/filesystem/scan.cpp)
target_include_directories(fileweaver_filesystem PUBLIC include)
target_link_libraries(fileweaver_storage PRIVATE fileweaver_filesystem fileweaver_core)
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
add_test(NAME fuzzy-contract COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/fuzzy_contract.py" "$<TARGET_FILE:fileweaver>")
add_test(NAME storage COMMAND storage_tests)
add_test(NAME cli-contract COMMAND "${CMAKE_COMMAND}"
  "-DCLI=$<TARGET_FILE:fileweaver>" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/cli.cmake")

set(DESKTOP_SOURCES)
set(DESKTOP_TIDY_SOURCES)
if(FILEWEAVER_BUILD_DESKTOP)
  if(NOT WIN32)
    message(FATAL_ERROR "This desktop milestone is Windows-only; the CLI remains portable")
  endif()
  find_package(Qt6 6.3 REQUIRED COMPONENTS Widgets Test)
  find_package(Threads REQUIRED)
  set(DESKTOP_SOURCES apps/desktop/directory_changes.cpp apps/desktop/directory_changes.hpp apps/desktop/watch_controller.cpp apps/desktop/watch_controller.hpp apps/desktop/file_open.cpp apps/desktop/file_open.hpp apps/desktop/global_hotkey.cpp apps/desktop/global_hotkey.hpp apps/desktop/main.cpp apps/desktop/arguments.cpp apps/desktop/arguments.hpp apps/desktop/search_controller.cpp
    apps/desktop/search_controller.hpp apps/desktop/search_window.cpp
    apps/desktop/search_window.hpp tests/desktop/search_window_test.cpp)
  set(DESKTOP_TIDY_SOURCES apps/desktop/directory_changes.cpp apps/desktop/watch_controller.cpp apps/desktop/file_open.cpp apps/desktop/global_hotkey.cpp apps/desktop/main.cpp apps/desktop/arguments.cpp apps/desktop/search_controller.cpp
    apps/desktop/search_window.cpp tests/desktop/search_window_test.cpp)
  add_library(fileweaver_desktop_ui apps/desktop/directory_changes.cpp apps/desktop/directory_changes.hpp apps/desktop/watch_controller.cpp apps/desktop/watch_controller.hpp apps/desktop/file_open.cpp apps/desktop/file_open.hpp apps/desktop/global_hotkey.cpp apps/desktop/global_hotkey.hpp apps/desktop/arguments.cpp apps/desktop/arguments.hpp apps/desktop/search_controller.cpp
    apps/desktop/search_controller.hpp apps/desktop/search_window.cpp apps/desktop/search_window.hpp)
  set_target_properties(fileweaver_desktop_ui PROPERTIES AUTOMOC ON)
  target_include_directories(fileweaver_desktop_ui PUBLIC apps/desktop)
  target_link_libraries(fileweaver_desktop_ui PUBLIC fileweaver_storage Qt6::Widgets PRIVATE Threads::Threads user32 shell32 ole32)
  qt_add_executable(fileweaver-desktop WIN32 apps/desktop/main.cpp)
  target_link_libraries(fileweaver-desktop PRIVATE fileweaver_desktop_ui)
  qt_add_executable(desktop_tests tests/desktop/search_window_test.cpp)
  set_target_properties(desktop_tests PROPERTIES AUTOMOC ON WIN32_EXECUTABLE FALSE)
  target_link_libraries(desktop_tests PRIVATE fileweaver_desktop_ui Qt6::Test)
  foreach(desktop_target IN ITEMS fileweaver_desktop_ui fileweaver-desktop desktop_tests)
    target_compile_definitions(${desktop_target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
      target_compile_options(${desktop_target} PRIVATE /W4 /WX /permissive- /utf-8)
    else()
      target_compile_options(${desktop_target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
  endforeach()
  foreach(plugin_pair IN ITEMS "fileweaver-desktop,QWindowsIntegrationPlugin" "desktop_tests,QMinimalIntegrationPlugin")
    string(REPLACE "," ";" plugin_parts "${plugin_pair}")
    list(GET plugin_parts 0 executable_target)
    list(GET plugin_parts 1 plugin_target)
    if(NOT TARGET Qt6::${plugin_target})
      message(FATAL_ERROR "Required Qt platform plugin missing: ${plugin_target}")
    endif()
    add_custom_command(TARGET ${executable_target} POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${executable_target}>/platforms"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:Qt6::${plugin_target}>"
        "$<TARGET_FILE_DIR:${executable_target}>/platforms" VERBATIM)
  endforeach()
  if(TARGET Qt6::QModernWindowsStylePlugin)
    add_custom_command(TARGET fileweaver-desktop POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:fileweaver-desktop>/styles"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:Qt6::QModernWindowsStylePlugin>"
        "$<TARGET_FILE_DIR:fileweaver-desktop>/styles" VERBATIM)
  endif()
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    get_filename_component(compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(runtime IN ITEMS libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
      if(EXISTS "${compiler_bin}/${runtime}")
        foreach(executable_target IN ITEMS fileweaver-desktop desktop_tests)
          add_custom_command(TARGET ${executable_target} POST_BUILD COMMAND "${CMAKE_COMMAND}" -E
            copy_if_different "${compiler_bin}/${runtime}" "$<TARGET_FILE_DIR:${executable_target}>" VERBATIM)
        endforeach()
      endif()
    endforeach()
  endif()
  add_test(NAME desktop-interaction COMMAND "${CMAKE_COMMAND}"
    "-DTEST_EXECUTABLE=$<TARGET_FILE:desktop_tests>" "-DRESULT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/desktop/run_qt_test.cmake")
  set_tests_properties(desktop-interaction PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=minimal" TIMEOUT 60)
endif()

set(PROJECT_SOURCES tests/support/directory_link.hpp apps/cli/main.cpp apps/cli/output.hpp apps/cli/windows_io.hpp benchmarks/search/core_search.cpp benchmarks/search/process_memory.hpp src/database/initialize.cpp tests/integration/storage.cpp
  src/core/utc_time.cpp include/fileweaver/core/utc_time.hpp src/core/fuzzy.cpp include/fileweaver/core/fuzzy.hpp include/fileweaver/core/watch_schedule.hpp
  include/fileweaver/application/index.hpp
  include/fileweaver/application/inspect.hpp
  include/fileweaver/application/refresh.hpp
  include/fileweaver/application/search.hpp
  src/filesystem/scan.cpp include/fileweaver/filesystem/scan.hpp
  include/fileweaver/application/initialize.hpp include/fileweaver/core/storage_error.hpp ${DESKTOP_SOURCES})
list(TRANSFORM PROJECT_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
list(TRANSFORM DESKTOP_TIDY_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
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
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/fuzzy.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/filesystem/scan.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/storage.cpp" ${DESKTOP_TIDY_SOURCES} ${TIDY_PLATFORM_ARGS} --warnings-as-errors=* VERBATIM)
else()
  add_custom_target(lint COMMAND "${CMAKE_COMMAND}" -E echo "clang-tidy is required"
    COMMAND "${CMAKE_COMMAND}" -E false)
endif()
