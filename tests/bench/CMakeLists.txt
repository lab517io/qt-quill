# Complexity guard for large documents (M11), using quill-bench's corpora.
quillqt_add_test(tst_scaling GUI
    SOURCES tst_scaling.cpp ${PROJECT_SOURCE_DIR}/tools/bench/Corpora.cpp
    LIBS QuillQt::Widgets)
target_include_directories(tst_scaling PRIVATE ${PROJECT_SOURCE_DIR}/tools/bench)
