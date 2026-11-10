cmake_minimum_required(VERSION 3.15)
project(FirstOS_CPP_Apps CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/apps
    ${CMAKE_CURRENT_SOURCE_DIR}/explorer
    ${CMAKE_CURRENT_SOURCE_DIR}/media
)

# Compile the C++ apps as a static library
add_library(firstos_cpp_apps STATIC
    explorer/FileExplorer.cpp
    explorer/FileSignatureParser.cpp
    media/MediaPlayer.cpp
    media/ImageDecoder.cpp
)

find_package(Threads REQUIRED)
target_link_libraries(firstos_cpp_apps PUBLIC Threads::Threads)

# Unit test executable
enable_testing()
add_executable(test_cpp_apps
    tests/test_main.cpp
)
target_link_libraries(test_cpp_apps PRIVATE firstos_cpp_apps Threads::Threads)
add_test(NAME AllCppTests COMMAND test_cpp_apps)
