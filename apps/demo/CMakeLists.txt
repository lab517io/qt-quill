add_executable(quill-demo
    main.cpp
    MainWindow.cpp
    MainWindow.h
    DeltaInspector.cpp
    DeltaInspector.h
    NotesEmbed.cpp
    NotesEmbed.h
    samples.qrc
)
quillqt_target_defaults(quill-demo)
set_target_properties(quill-demo PROPERTIES AUTORCC ON)
if(CMAKE_CROSSCOMPILING)
    # The host rcc may compress with zstd, which the target Qt cannot read
    # (seen with Qt for Windows/llvm-mingw built from Linux).
    set_target_properties(quill-demo PROPERTIES AUTORCC_OPTIONS "--compress-algo;zlib")
endif()
target_link_libraries(quill-demo PRIVATE QuillQt::Widgets)

if(QUILLQT_BUILD_TESTS)
    # Smoke test: the demo starts, loads a sample and renders.
    add_test(NAME quill-demo-screenshot
             COMMAND quill-demo ${QUILLQT_TEST_ARGS} --offline --sample flutter_quill_example.json
                     --screenshot ${CMAKE_BINARY_DIR}/test-output/demo.png)
    set_tests_properties(quill-demo-screenshot PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;LSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/tests/lsan.supp")
endif()
