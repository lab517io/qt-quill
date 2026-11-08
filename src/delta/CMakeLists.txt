add_library(quillqt_delta STATIC
    include/quill/Version.h
    Version.cpp
)
add_library(QuillQt::Delta ALIAS quillqt_delta)
quillqt_target_defaults(quillqt_delta)
target_include_directories(quillqt_delta PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# The Delta core must stay GUI-free: Qt6::Core only.
target_link_libraries(quillqt_delta PUBLIC Qt6::Core)
