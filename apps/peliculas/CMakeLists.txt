cmake_minimum_required(VERSION 3.21)
project(wonderland-peliculas VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 QuickDialogs2 Multimedia Svg DBus)

qt_standard_project_setup(REQUIRES 6.8)
include(GNUInstallDirs)

# What the tests build too: everything that needs neither a window nor a film.
set(core_sources
    src/media.cpp src/media.h
    src/subtitles.cpp src/subtitles.h
    src/playlist.cpp src/playlist.h
    src/history.cpp src/history.h
    src/store.cpp src/store.h
)
set(core_libs Qt6::Core)

qt_add_executable(wonderland-peliculas
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
set_source_files_properties(assets/wonderland-peliculas.svg PROPERTIES QT_RESOURCE_ALIAS wonderland-peliculas.svg)

qt_add_qml_module(wonderland-peliculas
    URI WonderlandPeliculas
    VERSION 1.0
    QML_FILES ${qml_files}
    RESOURCES ${symbol_files} assets/wonderland-peliculas.svg
    SOURCES ${core_sources}
        src/player.cpp src/player.h
        src/app.cpp src/app.h
)
target_include_directories(wonderland-peliculas PRIVATE src)
target_compile_definitions(wonderland-peliculas PRIVATE PELICULAS_VERSION="${PROJECT_VERSION}")
target_link_libraries(wonderland-peliculas PRIVATE ${core_libs}
    Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2 Qt6::Multimedia Qt6::Svg Qt6::DBus)

install(TARGETS wonderland-peliculas RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES wonderland-peliculas.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/wonderland-peliculas.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

option(PELICULAS_BUILD_TESTS "Build the Películas tests" ON)
if(PELICULAS_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    foreach(t media subtitles playlist history)
        qt_add_executable(tst_${t} tests/tst_${t}.cpp ${core_sources})
        target_include_directories(tst_${t} PRIVATE src)
        target_link_libraries(tst_${t} PRIVATE ${core_libs} Qt6::Test)
        add_test(NAME ${t} COMMAND tst_${t})
        set_tests_properties(${t} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C.UTF-8")
    endforeach()
endif()
