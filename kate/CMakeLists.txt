# qcodeedit-kate — optional companion library that parses Kate Syntax XML
# and produces a ready-to-use qce::RulesHighlighter. Separate from the core
# so applications that don't need Kate files (or that use another source
# for highlighting rules) can skip the XML dependency.

add_library(qcodeedit-kate STATIC
    src/KateXmlReader.cpp
    src/KateTheme.cpp
    include/qce/kate/KateXmlReader.h
    include/qce/kate/KateTheme.h
)

target_include_directories(qcodeedit-kate
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(qcodeedit-kate
    PUBLIC  qcodeedit::qcodeedit
    PRIVATE Qt6::Core
)

add_library(qcodeedit::kate ALIAS qcodeedit-kate)

# Install-tree export name: qcodeedit::kate (matches the build-tree alias).
set_target_properties(qcodeedit-kate PROPERTIES EXPORT_NAME kate)

# --- Install ------------------------------------------------------------

install(TARGETS qcodeedit-kate
    EXPORT qcodeedit-kateTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES
    include/qce/kate/KateXmlReader.h
    include/qce/kate/KateTheme.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qce/kate
)
install(EXPORT qcodeedit-kateTargets
    FILE qcodeedit-kateTargets.cmake
    NAMESPACE qcodeedit::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcodeedit-kate
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_SOURCE_DIR}/cmake/qcodeedit-kateConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-kateConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcodeedit-kate
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-kateConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-kateConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-kateConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcodeedit-kate
)
