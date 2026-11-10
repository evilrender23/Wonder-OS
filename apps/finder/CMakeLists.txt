cmake_minimum_required(VERSION 3.21)
project(wonderland-finder VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 Widgets Svg DBus LinguistTools)
find_package(KF6KIO CONFIG REQUIRED)
find_package(KF6Solid CONFIG REQUIRED)
find_package(KF6Service CONFIG REQUIRED)
find_package(KF6Config CONFIG REQUIRED)
find_package(KF6CoreAddons CONFIG REQUIRED)

qt_standard_project_setup(REQUIRES 6.8 I18N_TRANSLATED_LANGUAGES es)
include(GNUInstallDirs)

set(core_sources
    src/format.cpp src/format.h
    src/tags.cpp src/tags.h
    src/folderlook.cpp src/folderlook.h
    src/dirmodel.cpp src/dirmodel.h
    src/fileops.cpp src/fileops.h
    src/places.cpp src/places.h
    src/layout.cpp src/layout.h
    src/app.cpp src/app.h
    src/dragnotice.cpp src/dragnotice.h
    src/look.cpp src/look.h
)
set(core_libs
    Qt6::Quick Qt6::Widgets Qt6::Svg Qt6::DBus
    KF6::KIOCore KF6::KIOGui KF6::KIOWidgets KF6::Solid KF6::Service KF6::ConfigCore KF6::CoreAddons
)

qt_add_executable(wonderland-finder
    src/main.cpp
    src/providers.cpp src/providers.h
)

file(GLOB qml_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS qml/*.qml)
file(GLOB symbol_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS assets/symbols/*.svg)
file(GLOB icon_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS assets/icons/*.svg)
foreach(f IN LISTS qml_files)
    get_filename_component(name ${f} NAME)
    set_source_files_properties(${f} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()
foreach(f IN LISTS symbol_files)
    get_filename_component(name ${f} NAME)
    set_source_files_properties(${f} PROPERTIES QT_RESOURCE_ALIAS symbols/${name})
endforeach()
foreach(f IN LISTS icon_files)
    get_filename_component(name ${f} NAME)
    set_source_files_properties(${f} PROPERTIES QT_RESOURCE_ALIAS icons/${name})
endforeach()
set_source_files_properties(qml/Theme.qml qml/Prefs.qml qml/DragState.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(wonderland-finder
    URI WonderlandFinder
    VERSION 1.0
    QML_FILES ${qml_files}
    RESOURCES ${symbol_files} ${icon_files}
    SOURCES ${core_sources}
)
target_include_directories(wonderland-finder PRIVATE src)
target_compile_definitions(wonderland-finder PRIVATE FINDER_VERSION="${PROJECT_VERSION}")
target_link_libraries(wonderland-finder PRIVATE ${core_libs} Qt6::QuickControls2)

qt_add_translations(wonderland-finder
    TS_FILES i18n/wonderland-finder_es.ts
    # English only needs its plurals ("1 item", "2 items").
    PLURALS_TS_FILE i18n/wonderland-finder_en.ts
    RESOURCE_PREFIX /i18n
)

install(TARGETS wonderland-finder RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES wonderland-finder.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
# Lets the session start us when an app asks to "Show in folder" (org.freedesktop.FileManager1).
configure_file(org.wonderland.Finder.FileManager1.service.in org.wonderland.Finder.FileManager1.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.wonderland.Finder.FileManager1.service DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
install(FILES assets/wonderland-finder.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

option(FINDER_BUILD_TESTS "Build the Finder tests" ON)
if(FINDER_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    foreach(t format layout dirmodel places openwith dragnotice look folderlook)
        qt_add_executable(tst_${t} tests/tst_${t}.cpp ${core_sources})
        target_include_directories(tst_${t} PRIVATE src)
        target_link_libraries(tst_${t} PRIVATE ${core_libs} Qt6::Test)
        add_test(NAME ${t} COMMAND tst_${t})
        set_tests_properties(${t} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C.UTF-8;LANGUAGE=en")
    endforeach()
endif()
