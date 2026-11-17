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
        tst_paste
        tst_history
        tst_remote_cursors
        tst_image_resize)
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
foreach(test tst_editor_keys tst_editor_changes tst_toolbar tst_embeds tst_flutter_parity tst_remote_cursors tst_image_resize)
    add_test(NAME ${test}-hidpi COMMAND ${test} ${QUILLQT_TEST_ARGS})
    set_tests_properties(${test}-hidpi PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=2;LSAN_OPTIONS=suppressions=${QUILLQT_TESTS_DIR}/lsan.supp")
    foreach(style fusion windows)
        add_test(NAME ${test}-${style}-style COMMAND ${test} -style ${style} ${QUILLQT_TEST_ARGS})
        set_tests_properties(${test}-${style}-style PROPERTIES ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;LSAN_OPTIONS=suppressions=${QUILLQT_TESTS_DIR}/lsan.supp")
    endforeach()
endforeach()

# The editing suites again with the Delta undo model (QuillEditor::installHistory).
foreach(test tst_editor_keys tst_editor_api tst_toolbar tst_embeds tst_paste tst_image_resize)
    add_test(NAME ${test}-history COMMAND ${test} ${QUILLQT_TEST_ARGS})
    set_tests_properties(${test}-history PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QUILLQT_TEST_HISTORY=1;LSAN_OPTIONS=suppressions=${QUILLQT_TESTS_DIR}/lsan.supp")
endforeach()

# Editors collaborating through the example's server and session.
if(TARGET quillqt_collab_example)
    quillqt_add_test(tst_collab GUI SOURCES tst_collab.cpp LIBS quillqt_collab_example)
    target_include_directories(tst_collab PRIVATE ${PROJECT_SOURCE_DIR}/src/widgets)
endif()
