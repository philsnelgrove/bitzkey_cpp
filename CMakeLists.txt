cmake_minimum_required(VERSION 3.25)
# The version lives in the VERSION file: the app's details, the command-line tool and the installer's
# name all take it from there.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" BITZKEY_VERSION LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS VERSION)
project(Bitzkey VERSION ${BITZKEY_VERSION} LANGUAGES CXX)
configure_file(packaging/bitzkey.rc.in "${CMAKE_CURRENT_BINARY_DIR}/bitzkey.rc" @ONLY)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Link Microsoft's C++ runtime into the executables, so they need no Visual C++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)

# JSON for reading exported Bitzkeys.
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# The Vulkan headers, for the GPU brains: Vulkan itself is loaded when the program runs, so neither
# building nor running needs the Vulkan SDK.
FetchContent_Declare(vulkan_headers
    URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.3.296.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(vulkan_headers)

# doctest for the unit tests.
FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.12.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(doctest)

if(MSVC)
    # Strict IEEE floating point (no contraction into fused multiply-adds), so the simulation
    # computes exactly what the Python version computes.
    add_compile_options(/W4 /permissive- /fp:precise /fp:contract- /utf-8)
else()
    add_compile_options(-Wall -Wextra -ffp-contract=off)
endif()

# The simulation core: everything but the user interface.
add_library(bitzkey_core STATIC
    src/core/PyRandom.cpp
    src/core/PyFormat.cpp
    src/core/Genome.cpp
    src/core/Brain.cpp
    src/core/Insights.cpp
    src/core/Census.cpp
    src/core/Exchange.cpp
    src/core/Simulation.cpp
    src/core/Rules.cpp
    src/core/Catalog.cpp
    src/core/GpuBrains.cpp
    src/core/WorldFile.cpp
)
target_include_directories(bitzkey_core PUBLIC src)
target_link_libraries(bitzkey_core PUBLIC nlohmann_json::nlohmann_json PRIVATE Vulkan::Headers)
target_compile_definitions(bitzkey_core PUBLIC BITZKEY_VERSION="${BITZKEY_VERSION}")  # for every screen's header

# Workers: sharing a world's thinking with other Bitzkeys (this app or the Linux daemon) over the network, as their
# main service or as one of their workers. src/net is copied to and from bitzkey_daemon, as the core is. HTTP is one
# header.
FetchContent_Declare(httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(httplib)
add_library(bitzkey_net STATIC
    src/net/Brains.cpp
    src/net/Pool.cpp
    src/net/Probe.cpp
    src/net/ProbeServer.cpp
    src/net/Worker.cpp
)
target_link_libraries(bitzkey_net PUBLIC bitzkey_core httplib::httplib)

# The window app: SDL2 for the window, input and clipboard, SDL_ttf for text. The official
# prebuilt Visual C++ packages are fetched; their DLLs are copied beside the executable.
FetchContent_Declare(sdl2
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-VC.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(sdl2_ttf
    URL https://github.com/libsdl-org/SDL_ttf/releases/download/release-2.24.0/SDL2_ttf-devel-2.24.0-VC.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(sdl2 sdl2_ttf)
set(SDL2_DIR ${sdl2_SOURCE_DIR}/cmake)
set(SDL2_ttf_DIR ${sdl2_ttf_SOURCE_DIR}/cmake)
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_ttf CONFIG REQUIRED)

add_executable(bitzkey WIN32
    src/ui/main.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/bitzkey.rc"
    src/ui/Ui.cpp
    src/ui/Canvas.cpp
    src/ui/Window.cpp
    src/ui/Look.cpp
    src/ui/Icons.cpp
    src/ui/Dialog.cpp
    src/ui/Report.cpp
    src/ui/Setup.cpp
    src/ui/Inspector.cpp
    src/ui/Advanced.cpp
    src/ui/Designer.cpp
    src/ui/CatalogScreen.cpp
    src/ui/ColourGraph.cpp
    src/ui/App.cpp
    src/ui/Platform.cpp
    src/ui/Language.cpp
    src/ui/LanguageMenu.cpp
    src/ui/Fields.cpp
    src/ui/WorkersScreen.cpp
    src/ui/WorkerScreen.cpp
)
target_link_libraries(bitzkey PRIVATE bitzkey_net SDL2::SDL2 SDL2::SDL2main SDL2_ttf::SDL2_ttf comdlg32 shell32 ole32)
add_custom_command(TARGET bitzkey POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:bitzkey> $<TARGET_FILE_DIR:bitzkey>
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/i18n $<TARGET_FILE_DIR:bitzkey>/i18n
    COMMAND_EXPAND_LISTS)

# Screenshots of every screen, drawn off-screen, for checking the interface.
add_executable(bitzkey_screens tools/screens.cpp
    src/ui/Ui.cpp src/ui/Canvas.cpp src/ui/Window.cpp src/ui/Look.cpp src/ui/Icons.cpp src/ui/Dialog.cpp src/ui/Report.cpp src/ui/Setup.cpp
    src/ui/Advanced.cpp src/ui/Inspector.cpp src/ui/Designer.cpp src/ui/CatalogScreen.cpp src/ui/ColourGraph.cpp src/ui/App.cpp src/ui/Platform.cpp
    src/ui/Language.cpp src/ui/LanguageMenu.cpp src/ui/Fields.cpp src/ui/WorkersScreen.cpp src/ui/WorkerScreen.cpp)
target_include_directories(bitzkey_screens PRIVATE src)
add_custom_command(TARGET bitzkey_screens POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/i18n $<TARGET_FILE_DIR:bitzkey_screens>/i18n)
target_link_libraries(bitzkey_screens PRIVATE bitzkey_net SDL2::SDL2 SDL2::SDL2main SDL2_ttf::SDL2_ttf comdlg32 shell32 ole32)

# A command-line runner: run worlds headless, benchmark, trace, export.
add_executable(bitzkey_cli src/cli/main.cpp)
target_link_libraries(bitzkey_cli PRIVATE bitzkey_core)

# Unit tests.
add_executable(bitzkey_tests
    tests/test_main.cpp
    tests/test_random.cpp
    tests/test_genome.cpp
    tests/test_brain.cpp
    tests/test_simulation.cpp
    tests/test_exchange.cpp
    tests/test_rules.cpp
    tests/test_climate.cpp
    tests/test_catalog.cpp
    tests/test_gpu.cpp
    tests/test_worldfile.cpp
    tests/test_net.cpp
    tests/test_probe.cpp
    tests/test_sea.cpp
    tests/test_day.cpp
)
target_link_libraries(bitzkey_tests PRIVATE bitzkey_net doctest::doctest)
target_compile_definitions(bitzkey_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)

enable_testing()
add_test(NAME bitzkey_tests COMMAND bitzkey_tests)
