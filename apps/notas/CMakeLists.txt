cmake_minimum_required(VERSION 3.21)
project(wonderland-notas VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 QuickDialogs2 Svg DBus)
find_package(ZLIB REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)
include(GNUInstallDirs)

# What the tests build too: everything but the window.
set(core_sources
    src/doc.cpp src/doc.h
    src/richdoc.cpp src/richdoc.h
    src/ink.cpp src/ink.h
    src/search.cpp src/search.h
    src/zip.cpp src/zip.h
    src/store.cpp src/store.h
    src/library.cpp src/library.h
    src/exchange.cpp src/exchange.h
)
qt_add_library(notas-core STATIC ${core_sources})
target_include_directories(notas-core PUBLIC src)
target_link_libraries(notas-core PUBLIC Qt6::Core Qt6::Gui ZLIB::ZLIB)

option(NOTAS_BUILD_APP "Build the application (off: only the tests)" ON)
if(NOTAS_BUILD_APP)
qt_add_executable(wonderland-notas
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
set_source_files_properties(assets/wonderland-notas.svg PROPERTIES QT_RESOURCE_ALIAS wonderland-notas.svg)

qt_add_qml_module(wonderland-notas
    URI WonderlandNotas
    VERSION 1.0
    QML_FILES ${qml_files}
    RESOURCES ${symbol_files} assets/wonderland-notas.svg
    SOURCES
        src/app.cpp src/app.h
        src/editor.cpp src/editor.h
        src/inkcanvas.cpp src/inkcanvas.h
)
target_include_directories(wonderland-notas PRIVATE src)
target_compile_definitions(wonderland-notas PRIVATE NOTAS_VERSION="${PROJECT_VERSION}")
target_link_libraries(wonderland-notas PRIVATE notas-core
    Qt6::Quick Qt6::QuickPrivate Qt6::QuickControls2 Qt6::QuickDialogs2 Qt6::Svg Qt6::DBus)

install(TARGETS wonderland-notas RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES wonderland-notas.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/wonderland-notas.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES wonderland-notas.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
endif()

option(NOTAS_BUILD_TESTS "Build the Notas tests" ON)
if(NOTAS_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    foreach(t zip search doc richdoc ink library exchange)
        qt_add_executable(tst_${t} tests/tst_${t}.cpp)
        target_include_directories(tst_${t} PRIVATE src)
        target_link_libraries(tst_${t} PRIVATE notas-core Qt6::Test)
        add_test(NAME ${t} COMMAND tst_${t})
        set_tests_properties(${t} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C.UTF-8;TZ=Europe/Madrid")
    endforeach()
endif()
