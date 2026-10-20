# qcodeedit-grammar — grammar checking of nearby words by their readings:
# the morphology file (a minimal automaton of every form's readings, made
# from a morphological dictionary such as PoliMorf), the rules (government
# of prepositions, agreement, two verbs) with the words of a language from
# its rules file (data/pl.grammar), and words one typing slip away that fit.
# Plain C++: no Qt, no other library; qcodeedit-spell shows it in the editor.

add_library(qcodeedit-grammar STATIC
    src/Grammar.cpp
    src/Morphology.cpp
    src/Tag.cpp
    src/Text.cpp
    include/qce/grammar/Grammar.h
    include/qce/grammar/Morphology.h
    include/qce/grammar/Tag.h
    include/qce/grammar/Text.h
)

target_include_directories(qcodeedit-grammar
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(qcodeedit-grammar PROPERTIES
    EXPORT_NAME grammar
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC OFF
)

add_library(qcodeedit::grammar ALIAS qcodeedit-grammar)

# The rules files of the languages, in the source tree (tests) and installed.
set(QCE_GRAMMAR_SOURCE_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data CACHE INTERNAL "")
set(QCE_GRAMMAR_DATA_DIR ${CMAKE_INSTALL_FULL_DATADIR}/qcodeedit/grammar CACHE INTERNAL "")

# --- Install ------------------------------------------------------------

install(TARGETS qcodeedit-grammar
    EXPORT qcodeedit-grammarTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES
    include/qce/grammar/Grammar.h
    include/qce/grammar/Morphology.h
    include/qce/grammar/Tag.h
    include/qce/grammar/Text.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qce/grammar
)
install(FILES data/pl.grammar DESTINATION ${CMAKE_INSTALL_DATADIR}/qcodeedit/grammar)
install(EXPORT qcodeedit-grammarTargets
    FILE qcodeedit-grammarTargets.cmake
    NAMESPACE qcodeedit::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcodeedit-grammar
)

include(CMakePackageConfigHelpers)
set(QCE_GRAMMAR_INSTALL_DATA_DIR ${CMAKE_INSTALL_DATADIR}/qcodeedit/grammar)
configure_package_config_file(
    ${PROJECT_SOURCE_DIR}/cmake/qcodeedit-grammarConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-grammarConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcodeedit-grammar
    PATH_VARS QCE_GRAMMAR_INSTALL_DATA_DIR
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-grammarConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-grammarConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-grammarConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcodeedit-grammar
)
