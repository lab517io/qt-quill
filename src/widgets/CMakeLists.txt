add_library(quillqt_widgets STATIC
    include/quill/Colors.h
    include/quill/DeltaDocument.h
    include/quill/Formats.h
    Colors.cpp
    DeltaDocument.cpp
    Formats.cpp
)
add_library(QuillQt::Widgets ALIAS quillqt_widgets)
quillqt_target_defaults(quillqt_widgets)
target_include_directories(quillqt_widgets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(quillqt_widgets PUBLIC QuillQt::Delta Qt6::Gui Qt6::Widgets Qt6::Network)
