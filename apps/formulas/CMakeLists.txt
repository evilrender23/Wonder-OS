cmake_minimum_required(VERSION 3.21)
project(wonderland-formulas VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 QuickDialogs2 Svg Xml)
find_package(ZLIB REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)
include(GNUInstallDirs)

# What the tests build too: everything but the window.
qt_add_library(formulas-core STATIC
    src/symbols.cpp src/symbols.h
    src/latex.cpp src/latex.h
    src/mathfont.cpp src/mathfont.h
    src/layout.cpp src/layout.h
    src/mathml.cpp
    src/formats.cpp src/formats.h
    src/zip.cpp src/zip.h
    src/store.cpp src/store.h
    src/session.cpp src/session.h
    src/io.cpp src/io.h
    src/library.cpp src/library.h
)
target_include_directories(formulas-core PUBLIC src)
target_link_libraries(formulas-core PUBLIC Qt6::Core Qt6::Gui Qt6::Xml ZLIB::ZLIB)

qt_add_executable(wonderland-formulas
    src/main.cpp
    src/providers.cpp src/providers.h
)

file(GLOB qml_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS qml/*.qml)
file(GLOB symbol_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS assets/symbols/*.svg)
foreach(f IN LISTS qml_files)
    get_filename_component(name ${f} NAME)
    set_source_files_properties(${f} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()
foreach(f IN LISTS symbol_files)
    get_filename_component(name ${f} NAME)
    set_source_files_properties(${f} PROPERTIES QT_RESOURCE_ALIAS symbols/${name})
endforeach()
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(assets/wonderland-formulas.svg PROPERTIES QT_RESOURCE_ALIAS wonderland-formulas.svg)

qt_add_qml_module(wonderland-formulas
    URI WonderlandFormulas
    VERSION 1.0
    QML_FILES ${qml_files}
    RESOURCES ${symbol_files} assets/wonderland-formulas.svg
    SOURCES src/app.cpp src/app.h
        src/formulaitem.cpp src/formulaitem.h
        src/highlighter.cpp src/highlighter.h
)
target_include_directories(wonderland-formulas PRIVATE src)
target_compile_definitions(wonderland-formulas PRIVATE FORMULAS_VERSION="${PROJECT_VERSION}")
target_link_libraries(wonderland-formulas PRIVATE formulas-core
    Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2 Qt6::Svg)

install(TARGETS wonderland-formulas RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES wonderland-formulas.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/wonderland-formulas.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES wonderland-formulas.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)

option(FORMULAS_BUILD_TESTS "Build the Fórmulas tests" ON)
if(FORMULAS_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    file(GLOB test_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS tests/tst_*.cpp)
    foreach(f IN LISTS test_files)
        get_filename_component(t ${f} NAME_WE)
        string(REPLACE "tst_" "" name ${t})
        qt_add_executable(${t} ${f})
        target_link_libraries(${t} PRIVATE formulas-core Qt6::Test)
        add_test(NAME ${name} COMMAND ${t})
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C.UTF-8;TZ=Europe/Madrid")
    endforeach()
endif()
