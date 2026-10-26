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
            ${PROJECT_SOURCE_DIR}/src
        )
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

qce_add_test(test_viewport_state)
qce_add_test(test_utf8_map)
qce_add_test(test_simple_document)
qce_add_test(test_cursor_controller INTERNAL)
qce_add_test(test_line_renderer     INTERNAL)
qce_add_test(test_wrap_layout       INTERNAL)
qce_add_test(test_word_boundary)
qce_add_test(test_rules_highlighter)
qce_add_test(test_tokenize_bytes)
qce_add_test(test_folders_in_bytes)
qce_add_test(test_regex_shapes)
qce_add_test(test_highlighter_integration)
qce_add_test(test_fold_state)
qce_add_test(test_rule_based_folding)

# test_kate_xml_reader links against the kate companion library rather than
# building its source inline.
add_executable(test_kate_xml_reader test_kate_xml_reader.cpp)
target_link_libraries(test_kate_xml_reader PRIVATE
    qcodeedit::qcodeedit
    qcodeedit::kate
    Qt6::Test
)
add_test(NAME test_kate_xml_reader COMMAND test_kate_xml_reader)

# Kate syntax data-set version (SyntaxVersion / supportedSyntaxVersion).
add_executable(test_kate_syntax_version test_kate_syntax_version.cpp)
target_link_libraries(test_kate_syntax_version PRIVATE
    qcodeedit::kate
    Qt6::Test
)
add_test(NAME test_kate_syntax_version COMMAND test_kate_syntax_version)

# Kate data directories (dataRoot / dataDir / syntaxDir / themesDir).
add_executable(test_kate_paths test_kate_paths.cpp)
target_link_libraries(test_kate_paths PRIVATE
    qcodeedit::kate
    Qt6::Test
)
add_test(NAME test_kate_paths COMMAND test_kate_paths)

# Kate theme editor colours and the default (Breeze) theme choice.
add_executable(test_kate_theme test_kate_theme.cpp)
target_link_libraries(test_kate_theme PRIVATE
    qcodeedit::kate
    Qt6::Test
)
add_test(NAME test_kate_theme COMMAND test_kate_theme)

# Persistent Kate syntax index (index.json) + wildcard matcher.
add_executable(test_kate_syntax_index test_kate_syntax_index.cpp)
target_link_libraries(test_kate_syntax_index PRIVATE
    qcodeedit::kate
    Qt6::Test
)
add_test(NAME test_kate_syntax_index COMMAND test_kate_syntax_index)

# Kate data downloader — offline, against file:// URLs only.
if(TARGET qcodeedit::katedata)
    add_executable(test_kate_data_downloader test_kate_data_downloader.cpp)
    target_link_libraries(test_kate_data_downloader PRIVATE
        qcodeedit::katedata
        Qt6::Test
    )
    add_test(NAME test_kate_data_downloader COMMAND test_kate_data_downloader)
endif()

# Word frequency downloader — offline, against file:// URLs only.
if(TARGET qcodeedit::spelldata)
    add_executable(test_spell_data_downloader test_spell_data_downloader.cpp)
    target_link_libraries(test_spell_data_downloader PRIVATE
        qcodeedit::spelldata
        Qt6::Test
    )
    add_test(NAME test_spell_data_downloader COMMAND test_spell_data_downloader)
endif()

# Dynamic rule support (Kate's dynamic="true" %n expansion).
add_executable(test_dynamic_rules test_dynamic_rules.cpp)
target_link_libraries(test_dynamic_rules PRIVATE
    qcodeedit::qcodeedit
    qcodeedit::kate
    Qt6::Test
)
add_test(NAME test_dynamic_rules COMMAND test_dynamic_rules)

# Kate XML × tokenizeBytes regression (HANDOFF §3.4).
# Skips if xml.xml is not installed locally (see kateSyntaxPath).
add_executable(test_kate_xml_tokenize_regression test_kate_xml_tokenize_regression.cpp)
target_link_libraries(test_kate_xml_tokenize_regression PRIVATE
    qcodeedit::qcodeedit
    qcodeedit::kate
    Qt6::Test
)
target_compile_definitions(test_kate_xml_tokenize_regression PRIVATE
    QCE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)
add_test(NAME test_kate_xml_tokenize_regression COMMAND test_kate_xml_tokenize_regression)

# Fold pairing helper (HANDOFF_folding §4.2–§4.3).
add_executable(test_fold_pairing test_fold_pairing.cpp)
target_link_libraries(test_fold_pairing PRIVATE
    qcodeedit::qcodeedit
    qcodeedit::kate
    Qt6::Test
)
target_compile_definitions(test_fold_pairing PRIVATE
    QCE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)
add_test(NAME test_fold_pairing COMMAND test_fold_pairing)
qce_add_test(test_line_number_gutter)
qce_add_test(test_gutter_colors)
qce_add_test(test_code_edit_area_keys)
qce_add_test(test_code_edit_area_input_method)
qce_add_test(test_extra_selections INTERNAL)
qce_add_test(test_replace_ranges)

# Grammar: the morphology file and the rules (plain C++, its own small data).
if(TARGET qcodeedit::grammar)
    add_executable(test_grammar test_grammar.cpp)
    target_compile_definitions(test_grammar PRIVATE QCE_GRAMMAR_DATA_DIR="${QCE_GRAMMAR_SOURCE_DATA_DIR}")
    target_link_libraries(test_grammar PRIVATE qcodeedit::grammar Qt6::Test)
    add_test(NAME test_grammar COMMAND test_grammar)
endif()

# Spell checking with the system's hunspell dictionaries (skips without them).
if(TARGET qcodeedit::spell AND TARGET qcodeedit::kate)
    add_executable(test_spell test_spell.cpp)
    # The test opens the shared database itself, as another program would.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(TEST_SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)
    target_link_libraries(test_spell PRIVATE qcodeedit::spell qcodeedit::kate Qt6::Test PkgConfig::TEST_SQLITE3)
    if(TARGET qcodeedit::grammar)
        target_link_libraries(test_spell PRIVATE qcodeedit::grammar)
        target_compile_definitions(test_spell PRIVATE
            QCE_TEST_GRAMMAR QCE_GRAMMAR_SOURCE_DATA_DIR="${QCE_GRAMMAR_SOURCE_DATA_DIR}")
    endif()
    add_test(NAME test_spell COMMAND test_spell)
    set_tests_properties(test_spell PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# Real typing mistakes in English and French (tests/data/spell), with the
# system's dictionaries (skips without them).
if(TARGET qcodeedit::spell)
    add_executable(test_spell_texts test_spell_texts.cpp)
    target_compile_definitions(test_spell_texts PRIVATE QCE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    target_link_libraries(test_spell_texts PRIVATE qcodeedit::spell Qt6::Test)
    add_test(NAME test_spell_texts COMMAND test_spell_texts)
    set_tests_properties(test_spell_texts PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# Word forms unfolded from small .aff/.dic files written by the test itself.
if(TARGET qcodeedit::spell)
    add_executable(test_form_index test_form_index.cpp)
    target_include_directories(test_form_index PRIVATE ${PROJECT_SOURCE_DIR}/spell/src)
    target_link_libraries(test_form_index PRIVATE qcodeedit::spell Qt6::Test)
    add_test(NAME test_form_index COMMAND test_form_index)
endif()

# Code pages, detection and the editor's encoding guard (needs cpg).
if(TARGET qcodeedit::encoding)
    add_executable(test_encoding test_encoding.cpp)
    target_link_libraries(test_encoding PRIVATE qcodeedit::encoding Qt6::Test)
    add_test(NAME test_encoding COMMAND test_encoding)
endif()
