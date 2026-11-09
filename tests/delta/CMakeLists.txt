foreach(test
        tst_slab_recorded
        tst_fastdiff
        tst_op
        tst_delta_api
        tst_json
        tst_properties
        tst_oracle)
    quillqt_add_test(${test} SOURCES ${test}.cpp LIBS QuillQt::Delta)
endforeach()

# QuillQt::Delta must not depend on Qt GUI modules.
get_target_property(_delta_libs quillqt_delta LINK_LIBRARIES)
foreach(_lib IN LISTS _delta_libs)
    if(_lib MATCHES "Qt6::(Gui|Widgets|Network)")
        message(FATAL_ERROR "QuillQt::Delta links ${_lib}; it must stay Qt Core only")
    endif()
endforeach()
