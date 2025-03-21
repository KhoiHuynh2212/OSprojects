cmake_minimum_required(VERSION 3.10)

# Set the project name
project(OSProject VERSION 1.0)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Configure a header file to pass some of the CMake settings
# to the source code
configure_file(include/config.h.in include/config.h)

# Include directories
include_directories(
        ${PROJECT_BINARY_DIR}/include
        ${PROJECT_SOURCE_DIR}/include
)

# Source files
set(SOURCES
        CS3113_Project1.cpp
        CS3113_Project2.cpp
        CS3113_Project1.cpp
)

add_executable(${OSProject} ${SOURCES})
