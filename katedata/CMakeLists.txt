# qcodeedit-katedata — optional downloader for Kate syntax definitions and
# themes. Kept apart from qcodeedit-kate so that parsing Kate files never
# pulls in Qt Network.

add_library(qcodeedit-katedata STATIC
    src/KateDataDownloader.cpp
    include/qce/kate/KateDataDownloader.h
)

target_include_directories(qcodeedit-katedata
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(qcodeedit-katedata
    PUBLIC  qcodeedit::kate Qt6::Core Qt6::Network
)

add_library(qcodeedit::katedata ALIAS qcodeedit-katedata)
set_target_properties(qcodeedit-katedata PROPERTIES EXPORT_NAME katedata)

# Command-line fetcher: qce-kate-fetch [--dir <dataDir>] [--check]
add_executable(qce-kate-fetch tools/qce-kate-fetch.cpp)
target_link_libraries(qce-kate-fetch PRIVATE qcodeedit::katedata)

# --- Install ------------------------------------------------------------

install(TARGETS qcodeedit-katedata
    EXPORT qcodeedit-katedataTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES
    include/qce/kate/KateDataDownloader.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qce/kate
)
install(EXPORT qcodeedit-katedataTargets
    FILE qcodeedit-katedataTargets.cmake
    NAMESPACE qcodeedit::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcodeedit-katedata
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_SOURCE_DIR}/cmake/qcodeedit-katedataConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-katedataConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcodeedit-katedata
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-katedataConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-katedataConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-katedataConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcodeedit-katedata
)
