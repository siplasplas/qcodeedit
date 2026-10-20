# qcodeedit-spell — optional spell checking with hunspell: dictionaries, a
# user dictionary shared by all programs, and underlining misspelled words in
# the editor. Kept apart from the core library so that only applications that
# want it need hunspell and SQLite.

# The user's words, corrections and ignored words are kept in SQLite.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(HUNSPELL IMPORTED_TARGET hunspell)
    pkg_check_modules(SQLITE3 IMPORTED_TARGET sqlite3)
endif()
if(NOT HUNSPELL_FOUND OR NOT SQLITE3_FOUND)
    message(STATUS "qcodeedit: hunspell or SQLite not found (libhunspell-dev, libsqlite3-dev); "
                   "qcodeedit-spell is not built")
    return()
endif()

add_library(qcodeedit-spell STATIC
    src/SpellChecker.cpp
    src/CodeEditSpelling.cpp
    src/FormIndex.cpp
    src/BuiltinGrammar.cpp
    src/FormIndex.h
    src/SpellingReview.cpp
    src/UserDictionaryDialog.cpp
    src/UserStore.cpp
    src/UserStore.h
    include/qce/spell/SpellChecker.h
    include/qce/spell/CodeEditSpelling.h
    include/qce/spell/GrammarChecker.h
    include/qce/spell/SpellingReview.h
    include/qce/spell/UserDictionaryDialog.h
)

target_include_directories(qcodeedit-spell
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(qcodeedit-spell
    PUBLIC  qcodeedit::qcodeedit Qt6::Widgets
    PRIVATE PkgConfig::HUNSPELL PkgConfig::SQLITE3
)

# The built-in grammar (builtinGrammarChecker()), with qcodeedit-grammar.
set(QCE_SPELL_WITH_GRAMMAR OFF)
if(TARGET qcodeedit-grammar)
    set(QCE_SPELL_WITH_GRAMMAR ON)
    target_link_libraries(qcodeedit-spell PRIVATE qcodeedit-grammar)
    target_compile_definitions(qcodeedit-spell PRIVATE
        QCE_SPELL_GRAMMAR
        QCE_GRAMMAR_DATA_DIR="${QCE_GRAMMAR_DATA_DIR}")
endif()

add_library(qcodeedit::spell ALIAS qcodeedit-spell)
set_target_properties(qcodeedit-spell PROPERTIES EXPORT_NAME spell)

# --- Install ------------------------------------------------------------

install(TARGETS qcodeedit-spell
    EXPORT qcodeedit-spellTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES
    include/qce/spell/SpellChecker.h
    include/qce/spell/CodeEditSpelling.h
    include/qce/spell/GrammarChecker.h
    include/qce/spell/SpellingReview.h
    include/qce/spell/UserDictionaryDialog.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qce/spell
)
install(EXPORT qcodeedit-spellTargets
    FILE qcodeedit-spellTargets.cmake
    NAMESPACE qcodeedit::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcodeedit-spell
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${PROJECT_SOURCE_DIR}/cmake/qcodeedit-spellConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-spellConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcodeedit-spell
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-spellConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-spellConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-spellConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcodeedit-spell
)
