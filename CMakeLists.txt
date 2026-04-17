cmake_minimum_required(VERSION 3.21)
project(qcodeedit LANGUAGES CXX VERSION 1.1.0)

# --- Global options -----------------------------------------------------

option(QCE_BUILD_DEMO  "Build the qcodeedit demo application"  ON)
option(QCE_BUILD_TESTS "Build the qcodeedit test suite"        ON)
option(QCE_BUILD_KATE  "Build the qcodeedit-kate companion library" ON)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Qt -----------------------------------------------------------------

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)
if(QCE_BUILD_TESTS)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test)
    enable_testing()
endif()

# --- Library ------------------------------------------------------------

add_library(qcodeedit STATIC
    src/ITextDocument.cpp
    src/SimpleTextDocument.cpp
    src/CodeEditArea.cpp
    src/CodeEdit.cpp
    src/LineRenderer.cpp
    src/CursorController.cpp
    src/CaretPainter.cpp
    src/LineNumberGutter.cpp
    src/Rail.cpp
    src/EditCommands.cpp
    src/WrapLayout.cpp
    src/RulesHighlighter.cpp
    src/FoldState.cpp
    src/RuleBasedFoldingProvider.cpp
    src/FoldingGutter.cpp
    src/FillerState.cpp

    # Internal headers (in src/) are listed so AUTOMOC / IDEs pick them up.
    src/LineRenderer.h
    src/CursorController.h
    src/CaretPainter.h
    src/EditCommands.h
    src/WrapLayout.h

    # Public headers.
    include/qce/ViewportState.h
    include/qce/ITextDocument.h
    include/qce/IMargin.h
    include/qce/SimpleTextDocument.h
    include/qce/CodeEditArea.h
    include/qce/CodeEdit.h
    include/qce/TextCursor.h
    include/qce/TextAttribute.h
    include/qce/StyleSpan.h
    include/qce/HighlightState.h
    include/qce/IHighlighter.h
    include/qce/RulesHighlighter.h
    include/qce/FoldRegion.h
    include/qce/IFoldingProvider.h
    include/qce/FoldState.h
    include/qce/RuleBasedFoldingProvider.h
    include/qce/margins/LineNumberGutter.h
    include/qce/margins/FoldingGutter.h
    include/qce/FillerLine.h
    include/qce/FillerState.h
    include/qce/Rail.h
    include/qce/LeftRail.h
    include/qce/RightRail.h
)

# src/ is a private include directory so library .cpp files can include
# internal headers (CursorController.h, LineRenderer.h) without "../src/".
target_include_directories(qcodeedit
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_include_directories(qcodeedit PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(qcodeedit
    PUBLIC  Qt6::Core Qt6::Gui Qt6::Widgets
)

# Alias so users can write qcodeedit::qcodeedit in target_link_libraries,
# consistent with DiffMerge's diffcore::diffcore convention.
add_library(qcodeedit::qcodeedit ALIAS qcodeedit)

# --- Install ------------------------------------------------------------

install(TARGETS qcodeedit
    EXPORT qcodeeditTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# Public headers go under include/qce/. Preserve the two-level layout used
# internally (margins/ has its own subdirectory).
install(FILES
    include/qce/ViewportState.h
    include/qce/ITextDocument.h
    include/qce/IMargin.h
    include/qce/SimpleTextDocument.h
    include/qce/CodeEditArea.h
    include/qce/CodeEdit.h
    include/qce/TextCursor.h
    include/qce/TextAttribute.h
    include/qce/StyleSpan.h
    include/qce/HighlightState.h
    include/qce/IHighlighter.h
    include/qce/RulesHighlighter.h
    include/qce/FoldRegion.h
    include/qce/IFoldingProvider.h
    include/qce/FoldState.h
    include/qce/RuleBasedFoldingProvider.h
    include/qce/FillerLine.h
    include/qce/FillerState.h
    include/qce/Rail.h
    include/qce/LeftRail.h
    include/qce/RightRail.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qce
)
install(FILES
    include/qce/margins/LineNumberGutter.h
    include/qce/margins/FoldingGutter.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qce/margins
)

install(EXPORT qcodeeditTargets
    FILE qcodeeditTargets.cmake
    NAMESPACE qcodeedit::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcodeedit
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/qcodeeditConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeeditConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcodeedit
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeeditConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeeditConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/qcodeeditConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qcodeedit
)

# --- Subdirectories -----------------------------------------------------

if(QCE_BUILD_KATE)
    add_subdirectory(kate)
endif()

if(QCE_BUILD_DEMO)
    add_subdirectory(demo)
endif()

if(QCE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
