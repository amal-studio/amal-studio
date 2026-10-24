cmake_minimum_required(VERSION 3.28...3.30)

# Amal Studio - panneau « Coran » (texte Tanzil original, sourate par sourate, verset par verset)

find_package(Qt6 REQUIRED Widgets)

add_library(amal-coran MODULE)
add_library(OBS::amal-coran ALIAS amal-coran)

target_sources(amal-coran PRIVATE amal-coran.cpp)

target_link_libraries(amal-coran PRIVATE OBS::frontend-api OBS::libobs Qt::Widgets)

if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in amal-coran.rc)
  target_sources(amal-coran PRIVATE amal-coran.rc)
endif()

set_target_properties_obs(amal-coran PROPERTIES FOLDER plugins PREFIX "" AUTOMOC ON)
