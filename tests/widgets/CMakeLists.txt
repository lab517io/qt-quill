foreach(test
        tst_colors
        tst_load
        tst_export)
    quillqt_add_test(${test} SOURCES ${test}.cpp LIBS QuillQt::Widgets)
endforeach()
