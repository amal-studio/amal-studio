cmake_minimum_required(VERSION 3.28...3.30)

# Amal Studio - panneau « Studio graphique » (thème du projet, habillages en un clic)

find_package(Qt6 REQUIRED Widgets)

add_library(amal-habillage MODULE)
add_library(OBS::amal-habillage ALIAS amal-habillage)

target_sources(amal-habillage PRIVATE amal-habillage.cpp)

target_link_libraries(amal-habillage PRIVATE OBS::frontend-api OBS::libobs Qt::Widgets)

if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in amal-habillage.rc)
  target_sources(amal-habillage PRIVATE amal-habillage.rc)
endif()

set_target_properties_obs(amal-habillage PROPERTIES FOLDER plugins PREFIX "" AUTOMOC ON)
