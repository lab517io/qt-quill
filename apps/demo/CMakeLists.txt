add_executable(quill-demo main.cpp)
quillqt_target_defaults(quill-demo)
target_link_libraries(quill-demo PRIVATE QuillQt::Widgets)
