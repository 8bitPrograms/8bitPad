include(${CMAKE_SOURCE_DIR}/cmake/CPM.cmake)

CPMAddPackage(
    NAME ads
    GITHUB_REPOSITORY githubuser0xFFFF/Qt-Advanced-Docking-System
    GIT_TAG v5.1.1
    OPTIONS
        "BUILD_STATIC ON"
        "BUILD_EXAMPLES OFF"
)

set(QAPPLICATION_CLASS QApplication CACHE STRING "Inheritance class for SingleApplication" FORCE)
CPMAddPackage(
    NAME SingleApplication
    GITHUB_REPOSITORY itay-grudev/SingleApplication
    VERSION 3.3.4
    GIT_TAG a3ed916f591c300e97b873fde36863fa37b49fa9
    CUSTOM_CACHE_KEY mit-v3.3.4
    OPTIONS "QT_DEFAULT_MAJOR_VERSION 6"
)

CPMAddPackage(
    NAME editorconfig_core_qt
    GITHUB_REPOSITORY editorconfig/editorconfig-core-qt
    GIT_TAG ab62f0554abf2bbe4d45427b36a8b2f81ca7b4ab
)

add_subdirectory(lua)
add_subdirectory(scintilla)
add_subdirectory(lexilla)
add_subdirectory(uchardet)

target_link_libraries(lexilla PRIVATE scintilla)
