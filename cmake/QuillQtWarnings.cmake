# quillqt_target_defaults(<target> [RELAXED_STRINGS])
# Applies the language level and warning set to a quill-qt target without
# touching global settings of a consuming project. RELAXED_STRINGS allows
# implicit const char* -> QString conversion (used by tests for readability).
function(quillqt_target_defaults target)
    cmake_parse_arguments(ARG "RELAXED_STRINGS" "" "" ${ARGN})
    target_compile_features(${target} PUBLIC cxx_std_17)
    set_target_properties(${target} PROPERTIES
        CXX_EXTENSIONS OFF
        AUTOMOC ON)
    target_compile_definitions(${target} PRIVATE
        QT_NO_NARROWING_CONVERSIONS_IN_CONNECT
        QT_STRICT_ITERATORS)
    if(NOT ARG_RELAXED_STRINGS)
        target_compile_definitions(${target} PRIVATE
            QT_NO_CAST_FROM_ASCII
            QT_NO_CAST_TO_ASCII
            QT_NO_URL_CAST_FROM_STRING)
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        target_compile_definitions(${target} PRIVATE NOMINMAX)
        if(QUILLQT_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(QUILLQT_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
