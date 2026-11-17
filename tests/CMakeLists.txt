set(QUILLQT_TESTS_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# quillqt_add_test(<name> [GUI] SOURCES <files...> LIBS <targets...>)
# GUI: the test creates a QGuiApplication / QApplication (gets QUILLQT_TEST_ARGS).
function(quillqt_add_test name)
    cmake_parse_arguments(ARG "GUI" "" "SOURCES;LIBS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    quillqt_target_defaults(${name} RELAXED_STRINGS)
    target_include_directories(${name} PRIVATE ${QUILLQT_TESTS_DIR}/common)
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBS})
    target_compile_definitions(${name} PRIVATE
        QUILLQT_FIXTURES_DIR="${QUILLQT_TESTS_DIR}/fixtures"
        QUILLQT_TEST_OUTPUT_DIR="${CMAKE_BINARY_DIR}/test-output")
    set(args "")
    if(ARG_GUI)
        set(args ${QUILLQT_TEST_ARGS})
    endif()
    add_test(NAME ${name} COMMAND ${name} ${args})
    # Unit tests never need a display.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;LSAN_OPTIONS=suppressions=${QUILLQT_TESTS_DIR}/lsan.supp")
endfunction()

add_subdirectory(smoke)
add_subdirectory(delta)
add_subdirectory(widgets)
add_subdirectory(bench)
