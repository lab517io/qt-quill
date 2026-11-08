cmake_minimum_required(VERSION 3.21)

project(QuillQt
    VERSION 0.1.0
    DESCRIPTION "Native Qt Widgets rich text editor compatible with the Quill / flutter_quill Delta format"
    LANGUAGES CXX)

# Vendored use: demo and tests are only built by default when this is the top-level project.
option(QUILLQT_BUILD_DEMO  "Build the quill-demo application" ${PROJECT_IS_TOP_LEVEL})
option(QUILLQT_BUILD_TESTS "Build the quill-qt test-suite"   ${PROJECT_IS_TOP_LEVEL})

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/QuillQtWarnings.cmake)

if(QUILLQT_BUILD_TESTS)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
    enable_testing()
endif()

add_subdirectory(src/delta)
add_subdirectory(src/widgets)

if(QUILLQT_BUILD_DEMO)
    add_subdirectory(apps/demo)
endif()

if(QUILLQT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
