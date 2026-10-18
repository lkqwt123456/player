cmake_minimum_required(VERSION 3.20)
project(FluxPlayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(PLAYER_BUILD_APP "Build the Qt / FFmpeg player" ON)
option(PLAYER_BUILD_TESTS "Build automated tests" ON)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(Threads REQUIRED)
if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if(PLAYER_BUILD_TESTS)
    enable_testing()
    add_executable(queue_tests tests/queue_tests.cpp)
    target_include_directories(queue_tests PRIVATE src)
    target_link_libraries(queue_tests PRIVATE Threads::Threads)
    add_test(NAME bounded_queue COMMAND queue_tests)
    set_tests_properties(bounded_queue PROPERTIES TIMEOUT 15)
endif()

if(PLAYER_BUILD_APP)
    set(CMAKE_AUTOMOC ON)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Multimedia)
    find_package(FFmpeg REQUIRED)
    add_library(player_core STATIC
        src/core/MediaSession.cpp src/core/MediaSession.h
        src/core/VideoFilter.cpp src/core/VideoFilter.h
        src/core/StreamPublisher.cpp src/core/StreamPublisher.h)
    target_include_directories(player_core PUBLIC src)
    target_link_libraries(player_core PUBLIC Qt6::Gui FFmpeg::FFmpeg Threads::Threads)
    add_executable(FluxPlayer WIN32
        src/main.cpp src/player/PlayerController.cpp src/player/PlayerController.h
        src/ui/MainWindow.cpp src/ui/MainWindow.h
        src/ui/VideoWidget.cpp src/ui/VideoWidget.h)
    target_link_libraries(FluxPlayer PRIVATE player_core Qt6::Widgets Qt6::Multimedia)
    if(PLAYER_BUILD_TESTS)
        add_executable(media_smoke tests/media_smoke.cpp)
        target_link_libraries(media_smoke PRIVATE player_core)
        add_executable(controller_smoke tests/controller_smoke.cpp src/player/PlayerController.cpp src/player/PlayerController.h)
        target_link_libraries(controller_smoke PRIVATE player_core Qt6::Multimedia)
    endif()
    install(TARGETS FluxPlayer RUNTIME DESTINATION bin)
endif()
