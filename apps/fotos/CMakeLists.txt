cmake_minimum_required(VERSION 3.21)
project(wonderland-fotos VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 QuickDialogs2 Multimedia Svg DBus Concurrent LinguistTools)
find_package(PkgConfig REQUIRED)
# Reads the date, the camera and the orientation of a picture, and writes the
# orientation back without touching its pixels.
pkg_check_modules(EXIV2 REQUIRED IMPORTED_TARGET exiv2>=0.28)

# The texts are written in Spanish; English is the one translation.
qt_standard_project_setup(REQUIRES 6.8 I18N_SOURCE_LANGUAGE es I18N_TRANSLATED_LANGUAGES en)
include(GNUInstallDirs)

# What the tests build too: everything but the window.
set(core_sources
    src/media.cpp src/media.h
    src/metadata.cpp src/metadata.h
    src/thumbnailer.cpp src/thumbnailer.h
    src/store.cpp src/store.h
    src/library.cpp src/library.h
    src/albums.cpp src/albums.h
    src/photomodel.cpp src/photomodel.h
)
set(core_libs Qt6::Core Qt6::Gui Qt6::Qml Qt6::Concurrent PkgConfig::EXIV2)

qt_add_executable(wonderland-fotos
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
set_source_files_properties(assets/wonderland-fotos.svg PROPERTIES QT_RESOURCE_ALIAS wonderland-fotos.svg)

qt_add_qml_module(wonderland-fotos
    URI WonderlandFotos
    VERSION 1.0
    QML_FILES ${qml_files}
    RESOURCES ${symbol_files} assets/wonderland-fotos.svg
    SOURCES ${core_sources}
        src/app.cpp src/app.h
)
target_include_directories(wonderland-fotos PRIVATE src)
target_compile_definitions(wonderland-fotos PRIVATE FOTOS_VERSION="${PROJECT_VERSION}")
target_link_libraries(wonderland-fotos PRIVATE ${core_libs}
    Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2 Qt6::Multimedia Qt6::Svg Qt6::DBus)

qt_add_translations(wonderland-fotos
    TS_FILES i18n/wonderland-fotos_en.ts
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -locations none -no-obsolete
)

install(TARGETS wonderland-fotos RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES wonderland-fotos.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/wonderland-fotos.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

option(FOTOS_BUILD_TESTS "Build the Fotos tests" ON)
if(FOTOS_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    foreach(t media library albums)
        qt_add_executable(tst_${t} tests/tst_${t}.cpp tests/sample.h ${core_sources})
        target_include_directories(tst_${t} PRIVATE src tests)
        target_link_libraries(tst_${t} PRIVATE ${core_libs} Qt6::Test)
        add_test(NAME ${t} COMMAND tst_${t})
        set_tests_properties(${t} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C.UTF-8")
    endforeach()
endif()
