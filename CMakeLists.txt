cmake_minimum_required(VERSION 3.16)
project(MHWVroidHunter VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(VH_BUILD_TESTS "Build unit tests" ON)

# ---------------------------------------------------------------- core library
add_library(ooz STATIC
  third_party/ooz/kraken.cpp
  third_party/ooz/lzna.cpp
  third_party/ooz/bitknit.cpp)
target_include_directories(ooz PUBLIC third_party/ooz)
target_compile_options(ooz PRIVATE -w -mssse3)

add_library(vh_core STATIC
  src/core/mod3.cpp
  src/core/mrl3.cpp
  src/core/tex.cpp
  src/core/vrm.cpp
  src/core/hunter.cpp
  src/core/retarget.cpp
  src/core/chunk.cpp
  src/core/builder.cpp
  src/core/settings.cpp
  src/core/fsutil.cpp
  src/core/third_party_impl.cpp)
target_include_directories(vh_core PUBLIC src/core
  third_party/cgltf third_party/json third_party/stb)
target_link_libraries(vh_core PUBLIC ooz)
target_compile_definitions(vh_core PRIVATE VH_KEYSEQ_PATH="${CMAKE_SOURCE_DIR}/data/chunk_keyseq.bin")
set_source_files_properties(src/core/chunk.cpp PROPERTIES OBJECT_DEPENDS ${CMAKE_SOURCE_DIR}/data/chunk_keyseq.bin)
target_compile_options(vh_core PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
if(WIN32)
  target_compile_definitions(vh_core PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# ---------------------------------------------------------------- command line tool
add_executable(vroidhunter-cli src/cli/main.cpp)
target_link_libraries(vroidhunter-cli PRIVATE vh_core)
if(WIN32)
  target_link_options(vroidhunter-cli PRIVATE -static -municode)
endif()

# ---------------------------------------------------------------- Windows parts
if(WIN32)
  add_library(minhook STATIC
    third_party/minhook/src/buffer.c
    third_party/minhook/src/hook.c
    third_party/minhook/src/trampoline.c
    third_party/minhook/src/hde/hde64.c
    third_party/minhook/src/hde/hde32.c)
  target_include_directories(minhook PUBLIC third_party/minhook/include)
  target_compile_options(minhook PRIVATE -w)

  add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_win32.cpp
    third_party/imgui/backends/imgui_impl_dx11.cpp)
  target_include_directories(imgui PUBLIC third_party/imgui third_party/imgui/backends)
  target_compile_definitions(imgui PUBLIC UNICODE _UNICODE)
  target_compile_options(imgui PRIVATE -w)

  # The plugin: loaded into MonsterHunterWorld.exe (by our dinput8 proxy or by Stracker's Loader)
  add_library(MHWVroidHunter SHARED
    src/plugin/dllmain.cpp
    src/plugin/overlay.cpp
    src/plugin/ui.cpp
    src/plugin/plugin.rc)
  target_link_libraries(MHWVroidHunter PRIVATE vh_core imgui minhook d3d11 dxgi d3dcompiler_47 comdlg32 ole32 shell32 uuid dwmapi)
  set_target_properties(MHWVroidHunter PROPERTIES PREFIX "")
  target_link_options(MHWVroidHunter PRIVATE -static -static-libgcc -static-libstdc++ -s)

  # dinput8.dll proxy that loads the plugin when Stracker's Loader is not installed
  add_library(dinput8_proxy SHARED src/proxy/dinput8.cpp src/proxy/dinput8.def src/proxy/proxy.rc)
  set_target_properties(dinput8_proxy PROPERTIES PREFIX "" OUTPUT_NAME "dinput8")
  target_compile_definitions(dinput8_proxy PRIVATE UNICODE _UNICODE)
  target_link_options(dinput8_proxy PRIVATE -static -static-libgcc -static-libstdc++ -s)

  # Installer: a single exe that embeds the two DLLs above
  set(VH_PLUGIN_DLL "${CMAKE_BINARY_DIR}/MHWVroidHunter.dll")
  set(VH_PROXY_DLL "${CMAKE_BINARY_DIR}/dinput8.dll")
  set(VH_MANIFEST "${CMAKE_SOURCE_DIR}/src/installer/installer.manifest")
  configure_file(src/installer/installer.rc.in ${CMAKE_BINARY_DIR}/installer.rc @ONLY)
  add_executable(MHWVroidHunterSetup WIN32 src/installer/installer.cpp ${CMAKE_BINARY_DIR}/installer.rc)
  set_source_files_properties(${CMAKE_BINARY_DIR}/installer.rc PROPERTIES
    OBJECT_DEPENDS "${VH_PLUGIN_DLL};${VH_PROXY_DLL};${VH_MANIFEST}")
  add_dependencies(MHWVroidHunterSetup MHWVroidHunter dinput8_proxy)
  target_link_libraries(MHWVroidHunterSetup PRIVATE vh_core comctl32 comdlg32 shell32 ole32 uuid)
  target_link_options(MHWVroidHunterSetup PRIVATE -static -static-libgcc -static-libstdc++ -municode -s)

  # Stand-in for the game used to test the loader + overlay under Windows/Wine
  add_executable(fakegame tests/fakegame/fakegame.cpp)
  set_target_properties(fakegame PROPERTIES OUTPUT_NAME "MonsterHunterWorld" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/fakegame")
  target_link_libraries(fakegame PRIVATE d3d11 dxgi)
  target_link_options(fakegame PRIVATE -static)
endif()

# ---------------------------------------------------------------- tests
if(VH_BUILD_TESTS AND NOT WIN32)
  add_executable(vh_tests tests/test_main.cpp)
  target_link_libraries(vh_tests PRIVATE vh_core)
  enable_testing()
  add_test(NAME vh_tests COMMAND vh_tests ${CMAKE_SOURCE_DIR}/tests/data)
endif()
