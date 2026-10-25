cmake_minimum_required(VERSION 3.28...3.30)

# Amal Studio - assistant de démarrage (projet prêt à l'emploi)

find_package(Qt6 REQUIRED Widgets)

add_library(amal-assistant MODULE)
add_library(OBS::amal-assistant ALIAS amal-assistant)

target_sources(amal-assistant PRIVATE amal-assistant.cpp)

target_link_libraries(amal-assistant PRIVATE OBS::frontend-api OBS::libobs Qt::Widgets)

if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in amal-assistant.rc)
  target_sources(amal-assistant PRIVATE amal-assistant.rc)
endif()

set_target_properties_obs(amal-assistant PROPERTIES FOLDER plugins PREFIX "" AUTOMOC ON)
