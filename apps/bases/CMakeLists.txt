cmake_minimum_required(VERSION 3.21)
project(wonderland-bases VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 QuickDialogs2 Svg Sql PrintSupport)

qt_standard_project_setup(REQUIRES 6.8)
include(GNUInstallDirs)

# What the tests build too: everything but the window.
set(core_sources
    src/sqlutil.cpp src/sqlutil.h
    src/database.cpp src/database.h
    src/designer.cpp src/designer.h
    src/gridmodel.cpp src/gridmodel.h
    src/transfer.cpp src/transfer.h
    src/builder.cpp src/builder.h
    src/report.cpp src/report.h
    src/templates.cpp src/templates.h
    src/versions.cpp src/versions.h
    src/store.cpp src/store.h
)
set(core_libs Qt6::Core Qt6::Gui Qt6::Qml Qt6::Sql)

qt_add_executable(wonderland-bases
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
set_source_files_properties(assets/wonderland-bases.svg PROPERTIES QT_RESOURCE_ALIAS wonderland-bases.svg)

qt_add_qml_module(wonderland-bases
    URI WonderlandBases
    VERSION 1.0
    QML_FILES ${qml_files}
    RESOURCES ${symbol_files} assets/wonderland-bases.svg
    SOURCES ${core_sources}
        src/app.cpp src/app.h
        src/highlighter.cpp src/highlighter.h
        src/reportview.cpp src/reportview.h
)
target_include_directories(wonderland-bases PRIVATE src)
target_compile_definitions(wonderland-bases PRIVATE BASES_VERSION="${PROJECT_VERSION}")
target_link_libraries(wonderland-bases PRIVATE ${core_libs}
    Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2 Qt6::Svg Qt6::PrintSupport)

install(TARGETS wonderland-bases RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES wonderland-bases.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/wonderland-bases.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

option(BASES_BUILD_TESTS "Build the Bases tests" ON)
if(BASES_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    # The modules are compiled once for all the tests.
    add_library(bases_core STATIC ${core_sources})
    target_include_directories(bases_core PUBLIC src)
    target_link_libraries(bases_core PUBLIC ${core_libs})
    foreach(t sqlutil database designer gridmodel transfer builder report templates versions)
        qt_add_executable(tst_${t} tests/tst_${t}.cpp)
        target_link_libraries(tst_${t} PRIVATE bases_core Qt6::Test)
        add_test(NAME ${t} COMMAND tst_${t})
        set_tests_properties(${t} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C.UTF-8;TZ=Europe/Madrid")
    endforeach()
endif()
