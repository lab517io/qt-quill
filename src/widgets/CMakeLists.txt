add_library(quillqt_widgets STATIC
    include/quill/Colors.h
    include/quill/DeltaDocument.h
    include/quill/EmbedRegistry.h
    include/quill/Embeds.h
    include/quill/Formats.h
    include/quill/QuillEditor.h
    include/quill/QuillFindBar.h
    include/quill/QuillToolbar.h
    include/quill/Range.h
    include/quill/Source.h
    ClipboardHtml.cpp
    ClipboardHtml.h
    Colors.cpp
    DeltaDocument.cpp
    DocumentEdit.cpp
    DocumentEdit.h
    EmbedRegistry.cpp
    Embeds.cpp
    ImageLoader.cpp
    ImageLoader.h
    Export.cpp
    ImageSize.cpp
    ImageSize.h
    Formats.cpp
    QuillEditor.cpp
    QuillEditor_p.h
    QuillEditorKeys.cpp
    QuillFindBar.cpp
    QuillToolbar.cpp
    ColorButton.cpp
    ColorButton.h
    Icons.cpp
    Icons.h
    LinkDialog.cpp
    LinkDialog.h
)
add_library(QuillQt::Widgets ALIAS quillqt_widgets)
quillqt_target_defaults(quillqt_widgets)
target_include_directories(quillqt_widgets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(quillqt_widgets PUBLIC QuillQt::Delta Qt6::Gui Qt6::Widgets Qt6::Network)
