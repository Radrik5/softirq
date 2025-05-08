cmake_minimum_required(VERSION 3.10)

# Project name and brief description
project(SoftIRQTestApp
        VERSION 1.0
        DESCRIPTION "A test application to simulate high Soft IRQ CPU usage on Linux"
        LANGUAGES CXX)

# Specify C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Check if we're on a Unix-like system
if(UNIX)
    # Add compile options for better error detection
    add_compile_options(-Wall -Wextra)
endif()

# Output executables to a single bin directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Define executable targets
add_executable(client client.cpp)
add_executable(server server.cpp)

# Include directories - ensure the config.h file is found
target_include_directories(client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Installation rules
install(TARGETS client server
        RUNTIME DESTINATION bin)

# Print configuration summary
message(STATUS "")
message(STATUS "Configuration Summary:")
message(STATUS "  C++ Standard:       ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type:         ${CMAKE_BUILD_TYPE}")
message(STATUS "  Output Directory:   ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "")
