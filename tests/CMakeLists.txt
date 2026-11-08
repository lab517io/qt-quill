# quillqt_add_test(<name> SOURCES <files...> LIBS <targets...>)
function(quillqt_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    quillqt_target_defaults(${name})
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBS})
    target_compile_definitions(${name} PRIVATE
        QUILLQT_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        QUILLQT_TEST_OUTPUT_DIR="${CMAKE_BINARY_DIR}/test-output")
    add_test(NAME ${name} COMMAND ${name})
    # Unit tests never need a display.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

add_subdirectory(smoke)
