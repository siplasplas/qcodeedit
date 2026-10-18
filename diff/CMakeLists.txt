# qcodeedit-diff — optional: SimpleTextDocument::setText() changes only the
# lines that differ, found by DiffMerge's libdiffcore, so that a new text
# (e.g. a file changed on disk and read again) is one undo step and keeps the
# undo history, and the cursor follows the text it was in. Linking
# qcodeedit::diff is enough; nothing is called in the program. Kept apart
# from the core library, which DiffMerge itself uses.

# An installed DiffMerge 1.6 or later is used. Otherwise libdiffcore's
# sources of the tag below are built into qcodeedit-diff itself: installing
# a DiffMerge package with libdiffcore alone would clash with projects that
# fetch the whole of DiffMerge (e.g. agentin).
find_package(DiffMerge 1.6 CONFIG QUIET COMPONENTS Core)
if(TARGET DiffMerge::Core)
    set(QCE_DIFF_USES_DIFFMERGE ON)
else()
    set(QCE_DIFF_USES_DIFFMERGE OFF)
    include(FetchContent)
    FetchContent_Declare(
        diffmerge_sources
        GIT_REPOSITORY https://github.com/siplasplas/diffmerge.git
        GIT_TAG v1.6.0
        GIT_SHALLOW TRUE
        # Sources only: no CMake project of DiffMerge is added.
        SOURCE_SUBDIR sources-only
    )
    FetchContent_MakeAvailable(diffmerge_sources)
    set(QCE_DIFFCORE_DIR ${diffmerge_sources_SOURCE_DIR}/libdiffcore)
endif()

add_library(qcodeedit-diff STATIC
    src/LineDiff.cpp
    include/qce/diff/LineDiff.h
)

target_include_directories(qcodeedit-diff
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(qcodeedit-diff PUBLIC qcodeedit::qcodeedit)
if(QCE_DIFF_USES_DIFFMERGE)
    target_link_libraries(qcodeedit-diff PRIVATE DiffMerge::Core)
else()
    target_sources(qcodeedit-diff PRIVATE
        ${QCE_DIFFCORE_DIR}/src/ConflictMarkers.cpp
        ${QCE_DIFFCORE_DIR}/src/ConflictResolution.cpp
        ${QCE_DIFFCORE_DIR}/src/DiffEngine.cpp
        ${QCE_DIFFCORE_DIR}/src/LineInterner.cpp
        ${QCE_DIFFCORE_DIR}/src/SequenceDiff.cpp
        ${QCE_DIFFCORE_DIR}/src/SliderHeuristics.cpp
    )
    target_include_directories(qcodeedit-diff PRIVATE
        $<BUILD_INTERFACE:${QCE_DIFFCORE_DIR}/include>
        $<BUILD_INTERFACE:${QCE_DIFFCORE_DIR}/src>
    )
endif()

# Built into each program that links the library: installs the comparison
# before main().
set(QCE_DIFF_AUTO_INSTALL_DIR ${CMAKE_INSTALL_DATADIR}/qcodeedit-diff)
target_sources(qcodeedit-diff INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/AutoInstall.cpp>
    $<INSTALL_INTERFACE:$<INSTALL_PREFIX>/${QCE_DIFF_AUTO_INSTALL_DIR}/AutoInstall.cpp>
)

add_library(qcodeedit::diff ALIAS qcodeedit-diff)
set_target_properties(qcodeedit-diff PROPERTIES EXPORT_NAME diff)

# --- Install ------------------------------------------------------------

install(TARGETS qcodeedit-diff
    EXPORT qcodeedit-diffTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES include/qce/diff/LineDiff.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qce/diff
)
install(FILES src/AutoInstall.cpp
    DESTINATION ${QCE_DIFF_AUTO_INSTALL_DIR}
)
install(EXPORT qcodeedit-diffTargets
    FILE qcodeedit-diffTargets.cmake
    NAMESPACE qcodeedit::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcodeedit-diff
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${PROJECT_SOURCE_DIR}/cmake/qcodeedit-diffConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-diffConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcodeedit-diff
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-diffConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-diffConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeedit-diffConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcodeedit-diff
)
