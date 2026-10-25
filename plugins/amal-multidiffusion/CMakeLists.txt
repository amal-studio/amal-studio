cmake_minimum_required(VERSION 3.28...3.30)

# Amal Studio - panneau « Multidiffusion » (plusieurs plateformes en même temps)

find_package(Qt6 REQUIRED Widgets)

add_library(amal-multidiffusion MODULE)
add_library(OBS::amal-multidiffusion ALIAS amal-multidiffusion)

target_sources(amal-multidiffusion PRIVATE amal-multidiffusion.cpp)

target_link_libraries(amal-multidiffusion PRIVATE OBS::frontend-api OBS::libobs Qt::Widgets)
target_include_directories(amal-multidiffusion PRIVATE "${CMAKE_SOURCE_DIR}/shared")

if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in amal-multidiffusion.rc)
  target_sources(amal-multidiffusion PRIVATE amal-multidiffusion.rc)
endif()

set_target_properties_obs(amal-multidiffusion PROPERTIES FOLDER plugins PREFIX "" AUTOMOC ON)
