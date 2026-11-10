cmake_minimum_required(VERSION 3.21)
project(wonderland-textos VERSION 0.1.0 LANGUAGES CXX)

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
set(core_sources
    src/zip.cpp src/zip.h
    src/docmodel.cpp src/docmodel.h
    src/docx.cpp src/docx.h
    src/odt.cpp src/odt.h
    src/formats.cpp src/formats.h
    src/rtf.cpp
    src/editor.cpp src/editor.h
    src/store.cpp src/store.h
)
set(core_libs Qt6::Core Qt6::Gui Qt6::Qml Qt6::Xml ZLIB::ZLIB)

option(TEXTOS_BUILD_APP "Build the Textos window" ON)
if(TEXTOS_BUILD_APP)
qt_add_executable(wonderland-textos
    src/main.cpp
    src/providers.cpp src/providers.h
    src/thumbnails.cpp src/thumbnails.h
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
set_source_files_properties(assets/wonderland-textos.svg PROPERTIES QT_RESOURCE_ALIAS wonderland-textos.svg)

qt_add_qml_module(wonderland-textos
    URI WonderlandTextos
    VERSION 1.0
    QML_FILES ${qml_files}
    RESOURCES ${symbol_files} assets/wonderland-textos.svg
    SOURCES ${core_sources}
        src/app.cpp src/app.h
        src/pageview.cpp src/pageview.h
)
target_include_directories(wonderland-textos PRIVATE src)
target_compile_definitions(wonderland-textos PRIVATE TEXTOS_VERSION="${PROJECT_VERSION}")
target_link_libraries(wonderland-textos PRIVATE ${core_libs}
    Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2 Qt6::Svg)

install(TARGETS wonderland-textos RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES wonderland-textos.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/wonderland-textos.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()

option(TEXTOS_BUILD_TESTS "Build the Textos tests" ON)
if(TEXTOS_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    # The engine once, for all of them.
    add_library(textos-core STATIC ${core_sources})
    target_include_directories(textos-core PUBLIC src)
    target_link_libraries(textos-core PUBLIC ${core_libs})
    foreach(t zip docmodel docx odt formats editor)
        qt_add_executable(tst_${t} tests/tst_${t}.cpp)
        target_compile_definitions(tst_${t} PRIVATE TEXTOS_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
        target_link_libraries(tst_${t} PRIVATE textos-core Qt6::Test)
        add_test(NAME ${t} COMMAND tst_${t})
        set_tests_properties(${t} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C.UTF-8")
    endforeach()
endif()
