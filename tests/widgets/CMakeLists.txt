foreach(test
        tst_colors
        tst_load
        tst_export
        tst_editor_api
        tst_editor_keys
        tst_editor_changes
        tst_toolbar
        tst_embeds
        tst_flutter_parity)
    quillqt_add_test(${test} SOURCES ${test}.cpp LIBS QuillQt::Widgets)
    # White-box tests may use private headers.
    target_include_directories(${test} PRIVATE ${PROJECT_SOURCE_DIR}/src/widgets)
endforeach()
