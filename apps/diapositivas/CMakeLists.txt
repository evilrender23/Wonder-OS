cmake_minimum_required(VERSION 3.21)
project(wonderland-diapositivas VERSION 0.1.0 LANGUAGES CXX)

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
    src/model.cpp src/model.h
    src/richtext.cpp src/richtext.h
    src/render.cpp src/render.h
    src/pptx_read.cpp src/pptx_write.cpp src/pptx.h
    src/odp.cpp src/odp_write.cpp src/odp.h
    src/outline.cpp src/outline.h
    src/versions.cpp src/versions.h
    src/xmlutil.h
    src/deck.cpp src/deck.h
    src/store.cpp src/store.h
)
set(core_libs Qt6::Core Qt6::Gui Qt6::Qml Qt6::Xml ZLIB::ZLIB)

qt_add_executable(wonderland-diapositivas
    src/main.cpp
    src/providers.cpp src/providers.h
    src/preview.cpp src/preview.h
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
set_source_files_properties(assets/wonderland-diapositivas.svg PROPERTIES QT_RESOURCE_ALIAS wonderland-diapositivas.svg)

qt_add_qml_module(wonderland-diapositivas
    URI WonderlandDiapositivas
    VERSION 1.0
    QML_FILES ${qml_files}
    RESOURCES ${symbol_files} assets/wonderland-diapositivas.svg
    SOURCES ${core_sources}
        src/app.cpp src/app.h
        src/slideview.cpp src/slideview.h
)
target_include_directories(wonderland-diapositivas PRIVATE src)
target_compile_definitions(wonderland-diapositivas PRIVATE DIAPOSITIVAS_VERSION="${PROJECT_VERSION}")
target_link_libraries(wonderland-diapositivas PRIVATE ${core_libs}
    Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2 Qt6::Svg)

install(TARGETS wonderland-diapositivas RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES wonderland-diapositivas.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/wonderland-diapositivas.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

option(DIAPOSITIVAS_BUILD_TESTS "Build the Diapositivas tests" ON)
if(DIAPOSITIVAS_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    foreach(t zip model richtext deck pptx odp render outline versions)
        qt_add_executable(tst_${t} tests/tst_${t}.cpp ${core_sources})
        target_include_directories(tst_${t} PRIVATE src)
        target_link_libraries(tst_${t} PRIVATE ${core_libs} Qt6::Test)
        target_compile_definitions(tst_${t} PRIVATE TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
        add_test(NAME ${t} COMMAND tst_${t})
        set_tests_properties(${t} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C.UTF-8")
    endforeach()
endif()
