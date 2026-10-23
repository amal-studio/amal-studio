cmake_minimum_required(VERSION 3.28...3.30)

# Amal Studio - module « Projets » (fiches d'événement, modèle Conférence, sauvegarde automatique)

find_package(Qt6 REQUIRED Widgets)

add_library(amal-projets MODULE)
add_library(OBS::amal-projets ALIAS amal-projets)

target_sources(amal-projets PRIVATE amal-projets.cpp)

target_link_libraries(amal-projets PRIVATE OBS::frontend-api OBS::libobs Qt::Widgets)

if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in amal-projets.rc)
  target_sources(amal-projets PRIVATE amal-projets.rc)
endif()

set_target_properties_obs(amal-projets PROPERTIES FOLDER plugins PREFIX "" AUTOMOC ON)
