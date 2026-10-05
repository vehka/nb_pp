cmake_minimum_required(VERSION 3.13)
project(PlaitsPalette CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# The Plaits Palette sources, and the engine list its Seed3 port generates
# from the catalog (engine_table.h, sources.cmake).
set(PALETTE_REPO ${CMAKE_CURRENT_SOURCE_DIR}/eurorack)
set(PALETTE_GENERATED ${PALETTE_REPO}/alt_firmwares/palette_seed3/generated/all)
if(NOT EXISTS ${PALETTE_REPO}/stmlib/stmlib.h)
  message(FATAL_ERROR "eurorack sources missing; run: "
    "git submodule update --init && "
    "git -C ugen/eurorack submodule update --init stmlib")
endif()
include(${PALETTE_GENERATED}/sources.cmake)

# SuperCollider plugin headers: a source tree (-DSC_PATH=...) or the
# installed ones.
set(SC_PATH "" CACHE PATH "SuperCollider source tree (optional)")
if(SC_PATH)
  set(SC_INCLUDE ${SC_PATH}/include)
else()
  find_path(SC_INCLUDE plugin_interface/SC_PlugIn.h
    PATHS /usr/include/SuperCollider /usr/local/include/SuperCollider)
endif()
if(NOT EXISTS ${SC_INCLUDE}/plugin_interface/SC_PlugIn.h)
  message(FATAL_ERROR "SC_PlugIn.h not found; pass -DSC_PATH=<supercollider source>")
endif()

add_library(palette_dsp OBJECT ${PALETTE_ALL_SOURCES})
target_include_directories(palette_dsp PUBLIC
  ${PALETTE_REPO} ${PALETTE_GENERATED} ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(palette_dsp PUBLIC
  # stmlib's portable code paths instead of Cortex-M assembly
  TEST
  # samples play at exactly 48 kHz here, unlike on the module
  PLAITS_CORRECTED_SAMPLE_RATE=48000.0f)
target_compile_options(palette_dsp PUBLIC -fvisibility=hidden PRIVATE -w)

add_library(PlaitsPalette MODULE PlaitsPalette.cpp $<TARGET_OBJECTS:palette_dsp>)
target_link_libraries(PlaitsPalette PRIVATE palette_dsp)
target_include_directories(PlaitsPalette PRIVATE
  ${SC_INCLUDE}/plugin_interface ${SC_INCLUDE}/common)
# -Bsymbolic: other plugins (MiPlaits) carry their own, different plaits::
# and stmlib:: symbols; never bind to those.
target_link_options(PlaitsPalette PRIVATE -Wl,-Bsymbolic)
set_target_properties(PlaitsPalette PROPERTIES PREFIX "")
if(APPLE)
  set_target_properties(PlaitsPalette PROPERTIES SUFFIX ".scx")
endif()

add_executable(pp_host_test host_test.cc $<TARGET_OBJECTS:palette_dsp>)
target_link_libraries(pp_host_test PRIVATE palette_dsp m)
