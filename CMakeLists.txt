cmake_minimum_required(VERSION 3.15)
project(jtjson)

option(JSON_CPP_BUILD_TESTS "Enable building tests" ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(
    -fno-exceptions
    -fno-rtti
    -fno-unwind-tables
    -fno-asynchronous-unwind-tables
    -fvisibility=hidden
    -fno-math-errno
    -fno-trapping-math
)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(
        -Wno-c99-designator
        -Wno-c23-extensions
        -Wno-vla-cxx-extension
        -Wno-address-of-temporary
        -Wno-missing-field-initializers
    )
else()
    add_compile_options(
        -Wno-pedantic
        -Wno-vla
        -Wno-missing-field-initializers
    )
endif()
if(NOT APPLE)
    add_compile_options(-fno-semantic-interposition)
    add_link_options(-static-libgcc)
endif()
add_link_options(-nostdlib++)

# Main JSON library
# BUILD_SHARED_LIBS is a standard CMake variable that
# controls whether libraries are built as shared or static
add_library(json
    flat_json.cpp
)
set_target_properties(json PROPERTIES PUBLIC_HEADER "flat_json.hpp;flat_file.hpp")

# Tests
if (JSON_CPP_BUILD_TESTS)
    add_executable(tests tests.cpp)
    target_link_libraries(tests PRIVATE json)

    # Copy test data to build directory
    add_custom_command(
        TARGET tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/JSONTestSuite
            ${CMAKE_CURRENT_BINARY_DIR}/JSONTestSuite
    )

    enable_testing()
    add_test(
        NAME tests
        COMMAND tests
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endif()

include(GNUInstallDirs)
install(TARGETS json
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(FILES LICENSE THIRD_PARTY_NOTICES.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/jtjson
)
