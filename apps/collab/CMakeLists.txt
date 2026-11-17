# The example's building blocks, shared by quill-collab and tst_collab.
add_library(quillqt_collab_example STATIC
    Channel.cpp
    Channel.h
    CollabServer.cpp
    CollabServer.h
    CollabSession.cpp
    CollabSession.h
    Protocol.h
)
quillqt_target_defaults(quillqt_collab_example RELAXED_STRINGS)
target_include_directories(quillqt_collab_example PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(quillqt_collab_example PUBLIC QuillQt::Widgets Qt6::Network)

add_executable(quill-collab
    main.cpp
    CollabWindow.cpp
    CollabWindow.h
    ../demo/DeltaInspector.cpp
    ../demo/DeltaInspector.h
)
quillqt_target_defaults(quill-collab RELAXED_STRINGS)
target_link_libraries(quill-collab PRIVATE quillqt_collab_example)

if(QUILLQT_BUILD_TESTS)
    # Smoke test: server + two windows over loopback TCP, scripted
    # concurrent edits, convergence check, screenshot.
    add_test(NAME quill-collab-smoke
             COMMAND quill-collab ${QUILLQT_TEST_ARGS} --local 2
                     --screenshot ${CMAKE_BINARY_DIR}/test-output/collab.png)
    set_tests_properties(quill-collab-smoke PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;LSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/tests/lsan.supp")
endif()
