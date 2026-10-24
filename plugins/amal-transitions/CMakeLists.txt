cmake_minimum_required(VERSION 3.28...3.30)

# Amal Studio - « Transition Amal » (logo ou vidéo) et panneau « Relecture » (ralenti)

find_package(Qt6 REQUIRED Widgets)

add_library(amal-transitions MODULE)
add_library(OBS::amal-transitions ALIAS amal-transitions)

target_sources(amal-transitions PRIVATE amal-transitions.cpp)

target_link_libraries(amal-transitions PRIVATE OBS::frontend-api OBS::libobs Qt::Widgets)

if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in amal-transitions.rc)
  target_sources(amal-transitions PRIVATE amal-transitions.rc)
endif()

set_target_properties_obs(amal-transitions PROPERTIES FOLDER plugins PREFIX "" AUTOMOC ON)
