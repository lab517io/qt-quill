add_library(quillqt_delta STATIC
    include/quill/Attributes.h
    include/quill/Delta.h
    include/quill/FastDiff.h
    include/quill/Op.h
    include/quill/OpIterator.h
    include/quill/Version.h
    Attributes.cpp
    Delta.cpp
    FastDiff.cpp
    Json.cpp
    Op.cpp
    OpIterator.cpp
    Version.cpp
)
add_library(QuillQt::Delta ALIAS quillqt_delta)
quillqt_target_defaults(quillqt_delta)
target_include_directories(quillqt_delta PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# The Delta core must stay GUI-free: Qt6::Core only.
target_link_libraries(quillqt_delta PUBLIC Qt6::Core)
