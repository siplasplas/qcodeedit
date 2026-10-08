# qcodeedit-encoding — optional text file encodings (legacy code pages and
# UTF-8/16/32) on top of the cpg library, with an editor guard that keeps a
# code page document storable. Kept apart from the core library so that only
# applications that want it need cpg (and through it ICU and zlib).

# An installed cpg 1.1 or later is used; otherwise the tag below is fetched
# and built in, library only.
find_package(cpg 1.1 QUIET)
if(NOT TARGET cpg::cpg)
    include(FetchContent)
    # Installed along with qcodeedit, so that qcodeedit-encoding's package
    # finds it (the static library links cpg).
    set(CPG_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        cpg
        GIT_REPOSITORY https://github.com/siplasplas/cpg.git
        GIT_TAG v1.1.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(cpg)
endif()

add_library(qcodeedit-encoding STATIC
    src/Encoding.cpp
    src/EncodingGuard.cpp
    include/qce/encoding/Encoding.h
    include/qce/encoding/EncodingGuard.h
)

target_include_directories(qcodeedit-encoding
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(qcodeedit-encoding
    PUBLIC  qcodeedit::qcodeedit Qt6::Widgets
    PRIVATE cpg::cpg
)

# Default detection data: cpg's languages.txt and models, from the installed
# package or the fetched sources.
target_compile_definitions(qcodeedit-encoding PRIVATE
    QCE_CPG_LANGUAGES_FILE="${CPG_LANGUAGES_FILE}"
    QCE_CPG_MODELS_DIR="${CPG_MODELS_DIR}"
)

add_library(qcodeedit::encoding ALIAS qcodeedit-encoding)
set_target_properties(qcodeedit-encoding PROPERTIES EXPORT_NAME encoding)

# --- Install ------------------------------------------------------------

install(TARGETS qcodeedit-encoding
    EXPORT qcodeedit-encodingTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES
    include/qce/encoding/Encoding.h
    include/qce/encoding/EncodingGuard.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qce/encoding
)
install(EXPORT qcodeedit-encodingTargets
    FILE qcodeedit-encodingTargets.cmake
    NAMESPACE qcodeedit::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcodeedit-encoding
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${PROJECT_SOURCE_DIR}/cmake/qcodeedit-encodingConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-encodingConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcodeedit-encoding
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-encodingConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-encodingConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-encodingConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcodeedit-encoding
)
