cmake_minimum_required(VERSION 3.10)
project(archive_downloader C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

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
    src/parson.c
)

add_executable(archive_downloader ${SOURCES})
target_include_directories(archive_downloader PRIVATE src)

if(WIN32)
    # WinHTTP backend requires no external dependencies
    target_link_libraries(archive_downloader PRIVATE winhttp)
else()
    # POSIX backend uses libcurl
    find_package(CURL REQUIRED)
    target_include_directories(archive_downloader PRIVATE ${CURL_INCLUDE_DIRS})
    target_link_libraries(archive_downloader PRIVATE ${CURL_LIBRARIES})
endif()

if(MSVC)
    target_compile_definitions(archive_downloader PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

install(TARGETS archive_downloader DESTINATION bin)