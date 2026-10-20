# spelltune: fits the spelling suggestions' costs to real typing mistakes
# (see main.cpp). Not installed; built with QCE_BUILD_SPELLTUNE=ON.
add_executable(spelltune main.cpp)
target_include_directories(spelltune PRIVATE ${PROJECT_SOURCE_DIR}/spell/src)
target_link_libraries(spelltune PRIVATE qcodeedit::spell)
