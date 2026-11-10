cmake_minimum_required(VERSION 3.21)
project(wonderland-sistema VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 QuickDialogs2 Svg)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)

qt_standard_project_setup(REQUIRES 6.8)
include(GNUInstallDirs)

qt_add_executable(wonderland-sistema
    src/main.cpp
    src/app.cpp src/app.h
    src/appearance.cpp src/appearance.h
    src/config.cpp src/config.h
    src/tomldoc.cpp src/tomldoc.h
)

file(GLOB qml_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS qml/*.qml qml/*.js)
foreach(f IN LISTS qml_files)
    get_filename_component(name ${f} NAME)
    set_source_files_properties(${f} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(assets/wonderland-sistema.svg PROPERTIES QT_RESOURCE_ALIAS wonderland-sistema.svg)

qt_add_qml_module(wonderland-sistema
    URI WonderlandSistema
    VERSION 1.0
    QML_FILES ${qml_files}
    RESOURCES assets/wonderland-sistema.svg
)

# Every default the pages show comes from the compositor's own reference, so they cannot drift.
set(reference ${CMAKE_CURRENT_SOURCE_DIR}/../../config.reference.toml)
set_source_files_properties(${reference} PROPERTIES QT_RESOURCE_ALIAS config.reference.toml)
qt_add_resources(wonderland-sistema reference PREFIX /reference FILES ${reference})

target_include_directories(wonderland-sistema PRIVATE src)
target_compile_definitions(wonderland-sistema PRIVATE SISTEMA_VERSION="${PROJECT_VERSION}")
target_link_libraries(wonderland-sistema PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2 Qt6::Svg PkgConfig::XKBCOMMON)

install(TARGETS wonderland-sistema RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES wonderland-sistema.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/wonderland-sistema.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

option(SISTEMA_BUILD_TESTS "Build the Sistema tests" ON)
if(SISTEMA_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    qt_add_executable(tst_tomldoc tests/tst_tomldoc.cpp src/tomldoc.cpp src/tomldoc.h)
    target_include_directories(tst_tomldoc PRIVATE src)
    target_compile_definitions(tst_tomldoc PRIVATE REFERENCE_FILE="${reference}")
    target_link_libraries(tst_tomldoc PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME tomldoc COMMAND tst_tomldoc)

    qt_add_executable(tst_config tests/tst_config.cpp src/config.cpp src/config.h src/tomldoc.cpp src/tomldoc.h)
    target_include_directories(tst_config PRIVATE src)
    target_link_libraries(tst_config PRIVATE Qt6::Core Qt6::Qml Qt6::Test)
    add_test(NAME config COMMAND tst_config)

    qt_add_executable(tst_appearance tests/tst_appearance.cpp src/appearance.cpp src/appearance.h)
    target_include_directories(tst_appearance PRIVATE src)
    target_link_libraries(tst_appearance PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME appearance COMMAND tst_appearance)

    qt_add_executable(tst_search tests/tst_search.cpp)
    target_compile_definitions(tst_search PRIVATE QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
    target_link_libraries(tst_search PRIVATE Qt6::Core Qt6::Qml Qt6::Test)
    add_test(NAME search COMMAND tst_search)
endif()
