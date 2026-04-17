# Helper: register a Qt Test executable with a single source file.
# Avoids repeating the add_executable + target_link_libraries + add_test trio.
#
# Usage:
#   qce_add_test(test_name)           # standard: only public headers visible
#   qce_add_test(test_name INTERNAL)  # also grants access to src/ headers
#                                     # (for tests of internal classes)
function(qce_add_test test_name)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE
        qcodeedit::qcodeedit
        Qt6::Test
    )
    if("INTERNAL" IN_LIST ARGN)
        target_include_directories(${test_name} PRIVATE
            ${CMAKE_SOURCE_DIR}/src
        )
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

qce_add_test(test_viewport_state)
qce_add_test(test_simple_document)
qce_add_test(test_cursor_controller INTERNAL)
qce_add_test(test_line_renderer     INTERNAL)
qce_add_test(test_wrap_layout       INTERNAL)
qce_add_test(test_rules_highlighter)
qce_add_test(test_highlighter_integration)
qce_add_test(test_line_number_gutter)
qce_add_test(test_code_edit_area_keys)
