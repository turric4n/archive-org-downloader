cmake_minimum_required(VERSION 3.10)
project(archive_downloader C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler warnings: GCC/Clang use -Wall -Wextra; MSVC uses /W4.
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

# Build version, embedded via -DVERSION. Override at configure time with:
#   cmake -DVERSION="$(git describe --tags --always)"
set(VERSION "dev" CACHE STRING "Version string embedded in the binary")

# Core source set (shared by all platforms)
set(SOURCES
    src/main.c
    src/log.c
    src/util.c
    src/color.c
    src/http.c
    src/auth.c
    src/archive.c
    src/downloader.c
    src/thrd.c
    src/dashboard.c
    src/parson.c
)

add_executable(archive_downloader ${SOURCES})
target_include_directories(archive_downloader PRIVATE src)
target_compile_definitions(archive_downloader PRIVATE VERSION="${VERSION}")

if(WIN32)
    # WinHTTP backend requires no external dependencies
    target_link_libraries(archive_downloader PRIVATE winhttp)
else()
    # POSIX backend uses libcurl
    find_package(CURL REQUIRED)
    find_package(Threads REQUIRED)
    target_include_directories(archive_downloader PRIVATE ${CURL_INCLUDE_DIRS})
    target_link_libraries(archive_downloader PRIVATE ${CURL_LIBRARIES} Threads::Threads)
endif()

if(MSVC)
    target_compile_definitions(archive_downloader PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

install(TARGETS archive_downloader DESTINATION bin)

# ---- Tests ----
# Deterministic unit tests (no network). Link util.c only; run anywhere.
enable_testing()
add_executable(unit_test tests/unit_test.c src/util.c)
target_include_directories(unit_test PRIVATE src)
if(MSVC)
    target_compile_definitions(unit_test PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME unit COMMAND unit_test)

# End-to-end integration test (requires network access to archive.org).
# Enabled on POSIX (git-bash) where the run of "bash" is predictable. On Windows
# the same check runs directly in CI via `bash tests/integration_test.sh`.
if(NOT WIN32)
    find_program(BASH_CMD bash)
    if(BASH_CMD)
        add_test(
            NAME integration
            COMMAND ${BASH_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration_test.sh
                    $<TARGET_FILE:archive_downloader>
                    ${CMAKE_CURRENT_BINARY_DIR}/build-test/dest
        )
    endif()
endif()