cmake_minimum_required(VERSION 3.14)
project(g723_1 VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Options
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(BUILD_STATIC_LIBS "Build static library alongside shared library" ON)
option(BUILD_TESTING "Build C test executable" ON)

# Sources
set(G723_SOURCES
    src/codec_g723_1.c
    src/backends/c/g723_backend_c.c
    src/backends/c/g723_basicop.c
    src/backends/c/g723_encoder.c
    src/backends/c/g723_header.c
    src/backends/c/g723_linepack.c
    src/backends/c/g723_qdec.c
    src/backends/c/g723_resample.c
    src/backends/c/g723_spec_exc.c
    src/backends/c/g723_spec_lsp.c
    src/backends/c/g723_tables.c
)

set(G723_PUBLIC_HEADERS
    include/codec_g723_1.h
)

# Shared library
add_library(g723_1 SHARED ${G723_SOURCES} ${G723_PUBLIC_HEADERS})
target_include_directories(g723_1
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        src
        src/backends/c
)
target_compile_definitions(g723_1 PRIVATE g723_1_EXPORTS G723_BUILDING_DLL)
if(NOT WIN32)
    target_link_libraries(g723_1 PRIVATE m)
endif()
set_target_properties(g723_1 PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER "${G723_PUBLIC_HEADERS}"
)

# Static library
if(BUILD_STATIC_LIBS)
    add_library(g723_1_static STATIC ${G723_SOURCES} ${G723_PUBLIC_HEADERS})
    target_include_directories(g723_1_static
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            src
            src/backends/c
    )
    target_compile_definitions(g723_1_static PUBLIC G723_STATIC)
    if(NOT WIN32)
        target_link_libraries(g723_1_static PRIVATE m)
    endif()
    set_target_properties(g723_1_static PROPERTIES
        OUTPUT_NAME g723_1
        PUBLIC_HEADER "${G723_PUBLIC_HEADERS}"
    )
endif()

# C Unit Tests
if(BUILD_TESTING)
    enable_testing()
    add_executable(test_c_api src/backends/c/tests/test_full_c_api.c)
    target_include_directories(test_c_api PRIVATE include src src/backends/c)
    target_link_libraries(test_c_api PRIVATE g723_1)
    if(NOT WIN32)
        target_link_libraries(test_c_api PRIVATE m)
    endif()
    add_test(NAME c_api_test COMMAND test_c_api)
endif()

# Installation rules
include(GNUInstallDirs)
install(TARGETS g723_1
    EXPORT g723_1_targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
if(BUILD_STATIC_LIBS)
    install(TARGETS g723_1_static
        EXPORT g723_1_targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()
