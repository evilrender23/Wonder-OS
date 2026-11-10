cmake_minimum_required(VERSION 3.21)
project(wonderland-hojas VERSION 0.1.0 LANGUAGES CXX)

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

# What the tests build too: everything but the window. A static library, so
# the engine is compiled once however many tests there are.
set(core_sources
    src/value.h
    src/zip.cpp src/zip.h
    src/numfmt.cpp src/numfmt.h
    src/formula.cpp src/formula.h
    src/eval.cpp src/eval_p.h
    src/functions.cpp src/functions2.cpp src/fn_p.h
    src/workbook.cpp src/workbook.h
    src/csv.cpp src/csv.h
)
set(core_libs Qt6::Core Qt6::Gui ZLIB::ZLIB)
add_library(hojas-core STATIC ${core_sources})
target_include_directories(hojas-core PUBLIC src)
target_link_libraries(hojas-core PUBLIC ${core_libs})

option(HOJAS_BUILD_APP "Build the window" ON)
if(HOJAS_BUILD_APP AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
    include(app.cmake)
endif()

option(HOJAS_BUILD_TESTS "Build the Hojas tests" ON)
if(HOJAS_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    file(GLOB test_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS tests/tst_*.cpp)
    foreach(f IN LISTS test_files)
        get_filename_component(t ${f} NAME_WE)
        string(REPLACE "tst_" "" name ${t})
        qt_add_executable(${t} ${f})
        target_link_libraries(${t} PRIVATE hojas-core Qt6::Test)
        add_test(NAME ${name} COMMAND ${t})
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C.UTF-8;TZ=Europe/Madrid")
    endforeach()
endif()
