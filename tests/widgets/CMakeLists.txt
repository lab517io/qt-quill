foreach(test
        tst_colors
        tst_load
        tst_export
        tst_editor_api
        tst_editor_keys
        tst_editor_changes)
    quillqt_add_test(${test} SOURCES ${test}.cpp LIBS QuillQt::Widgets)
endforeach()
