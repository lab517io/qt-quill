foreach(test
        tst_colors
        tst_load
        tst_export
        tst_editor_api
        tst_editor_keys
        tst_editor_changes
        tst_toolbar
        tst_embeds
        tst_flutter_parity
        tst_paste)
    set(gui GUI)
    if(test STREQUAL "tst_colors") # QTEST_GUILESS_MAIN
        set(gui "")
    endif()
    quillqt_add_test(${test} ${gui} SOURCES ${test}.cpp LIBS QuillQt::Widgets)
    # White-box tests may use private headers.
    target_include_directories(${test} PRIVATE ${PROJECT_SOURCE_DIR}/src/widgets)
endforeach()

# The same widget suites at device pixel ratio 2 and under other widget
# styles (marker hit-tests, decorations, icons). Linux reproduces HiDPI and
# the Windows style; real platforms are covered by M9.
foreach(test tst_editor_keys tst_editor_changes tst_toolbar tst_embeds tst_flutter_parity)
    add_test(NAME ${test}-hidpi COMMAND ${test} ${QUILLQT_TEST_ARGS})
    set_tests_properties(${test}-hidpi PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=2;LSAN_OPTIONS=suppressions=${QUILLQT_TESTS_DIR}/lsan.supp")
    foreach(style fusion windows)
        add_test(NAME ${test}-${style}-style COMMAND ${test} -style ${style} ${QUILLQT_TEST_ARGS})
        set_tests_properties(${test}-${style}-style PROPERTIES ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;LSAN_OPTIONS=suppressions=${QUILLQT_TESTS_DIR}/lsan.supp")
    endforeach()
endforeach()
