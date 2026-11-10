cmake_minimum_required(VERSION 3.21)
project(wonderland-musica VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 QuickDialogs2 Multimedia Svg DBus Concurrent LinguistTools)
find_package(PkgConfig REQUIRED)
# 2.0 brought the one call that reads the cover of any format (complexProperties).
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib>=2.0)

qt_standard_project_setup(REQUIRES 6.8 I18N_SOURCE_LANGUAGE es I18N_TRANSLATED_LANGUAGES en)
include(GNUInstallDirs)

# What the tests build too: everything but the window and the audio output.
set(core_sources
    src/track.cpp src/track.h
    src/tagreader.cpp src/tagreader.h
    src/store.cpp src/store.h
    src/library.cpp src/library.h
    src/playlists.cpp src/playlists.h
    src/queue.cpp src/queue.h
)
set(core_libs Qt6::Core Qt6::Gui Qt6::Concurrent PkgConfig::TAGLIB)

qt_add_executable(wonderland-musica
    src/main.cpp
    src/providers.cpp src/providers.h
    src/mpris.cpp src/mpris.h
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
set_source_files_properties(assets/wonderland-musica.svg PROPERTIES QT_RESOURCE_ALIAS wonderland-musica.svg)

qt_add_qml_module(wonderland-musica
    URI WonderlandMusica
    VERSION 1.0
    QML_FILES ${qml_files}
    RESOURCES ${symbol_files} assets/wonderland-musica.svg
    SOURCES ${core_sources}
        src/player.cpp src/player.h
        src/trackmodel.cpp src/trackmodel.h
        src/app.cpp src/app.h
)
target_include_directories(wonderland-musica PRIVATE src)
target_compile_definitions(wonderland-musica PRIVATE MUSICA_VERSION="${PROJECT_VERSION}")
target_link_libraries(wonderland-musica PRIVATE ${core_libs}
    Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2 Qt6::Multimedia Qt6::Svg Qt6::DBus)

# The texts are written in Spanish; English is the one translation.
qt_add_translations(wonderland-musica
    TS_FILES i18n/wonderland-musica_en.ts
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -locations none -no-obsolete
)

install(TARGETS wonderland-musica RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES wonderland-musica.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/wonderland-musica.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

option(MUSICA_BUILD_TESTS "Build the Música tests" ON)
if(MUSICA_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    foreach(t queue library playlists)
        qt_add_executable(tst_${t} tests/tst_${t}.cpp tests/sample.h ${core_sources})
        target_include_directories(tst_${t} PRIVATE src tests)
        target_link_libraries(tst_${t} PRIVATE ${core_libs} Qt6::Test)
        add_test(NAME ${t} COMMAND tst_${t})
        set_tests_properties(${t} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C.UTF-8")
    endforeach()
endif()
