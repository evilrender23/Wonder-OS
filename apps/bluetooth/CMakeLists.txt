cmake_minimum_required(VERSION 3.21)
project(wonderland-bluetooth VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 Svg DBus LinguistTools)
find_package(KF6BluezQt REQUIRED)

qt_standard_project_setup(REQUIRES 6.8 I18N_SOURCE_LANGUAGE es I18N_TRANSLATED_LANGUAGES en)
include(GNUInstallDirs)

# What the tests build too: everything but BlueZ and the window.
set(core_sources
    src/devices.cpp src/devices.h
    src/devicemodel.cpp src/devicemodel.h
    src/backend.h
    src/fakebackend.cpp src/fakebackend.h
    src/bluetooth.cpp src/bluetooth.h
)

qt_add_executable(wonderland-bluetooth
    src/main.cpp
    src/app.cpp src/app.h
    src/bluezbackend.cpp src/bluezbackend.h
    src/providers.cpp src/providers.h
    ${core_sources}
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
set_source_files_properties(assets/wonderland-bluetooth.svg PROPERTIES QT_RESOURCE_ALIAS wonderland-bluetooth.svg)

qt_add_qml_module(wonderland-bluetooth
    URI WonderlandBluetooth
    VERSION 1.0
    QML_FILES ${qml_files}
    RESOURCES ${symbol_files} assets/wonderland-bluetooth.svg
)

target_include_directories(wonderland-bluetooth PRIVATE src)
target_compile_definitions(wonderland-bluetooth PRIVATE BLUETOOTH_VERSION="${PROJECT_VERSION}")
target_link_libraries(wonderland-bluetooth PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Svg Qt6::DBus KF6::BluezQt)

# The texts are written in Spanish; English is the one translation.
qt_add_translations(wonderland-bluetooth
    TS_FILES i18n/wonderland-bluetooth_en.ts
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -locations none -no-obsolete
)

install(TARGETS wonderland-bluetooth RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES wonderland-bluetooth.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/wonderland-bluetooth.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

option(BLUETOOTH_BUILD_TESTS "Build the Bluetooth tests" ON)
if(BLUETOOTH_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    foreach(t devices devicemodel bluetooth)
        qt_add_executable(tst_${t} tests/tst_${t}.cpp ${core_sources})
        target_include_directories(tst_${t} PRIVATE src)
        target_compile_definitions(tst_${t} PRIVATE SYMBOLS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/symbols")
        target_link_libraries(tst_${t} PRIVATE Qt6::Core Qt6::Test)
        add_test(NAME ${t} COMMAND tst_${t})
        set_tests_properties(${t} PROPERTIES ENVIRONMENT "LC_ALL=C.UTF-8")
    endforeach()
endif()
