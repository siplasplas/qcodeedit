# qcodeedit-spelldata — optional downloader of word frequency lists for
# spelling suggestions. Kept apart from qcodeedit-spell so that spell
# checking never pulls in Qt Network.

add_library(qcodeedit-spelldata STATIC
    src/WordFrequencyDownloader.cpp
    include/qce/spell/WordFrequencyDownloader.h
)

target_include_directories(qcodeedit-spelldata
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(qcodeedit-spelldata
    PUBLIC  qcodeedit::spell Qt6::Core Qt6::Network Qt6::Concurrent
)

add_library(qcodeedit::spelldata ALIAS qcodeedit-spelldata)
set_target_properties(qcodeedit-spelldata PROPERTIES EXPORT_NAME spelldata)

# --- Install ------------------------------------------------------------

install(TARGETS qcodeedit-spelldata
    EXPORT qcodeedit-spelldataTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES
    include/qce/spell/WordFrequencyDownloader.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qce/spell
)
install(EXPORT qcodeedit-spelldataTargets
    FILE qcodeedit-spelldataTargets.cmake
    NAMESPACE qcodeedit::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcodeedit-spelldata
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${PROJECT_SOURCE_DIR}/cmake/qcodeedit-spelldataConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-spelldataConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcodeedit-spelldata
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-spelldataConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-spelldataConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-spelldataConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcodeedit-spelldata
)
