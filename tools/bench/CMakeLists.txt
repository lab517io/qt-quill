# quill-bench: timings on large documents (M11). Not installed; see main.cpp.
add_executable(quill-bench main.cpp Corpora.cpp Corpora.h)
quillqt_target_defaults(quill-bench RELAXED_STRINGS)
target_link_libraries(quill-bench PRIVATE QuillQt::Widgets Qt6::Test)
target_compile_definitions(quill-bench PRIVATE QUILLQT_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures")
