cmake_minimum_required(VERSION 3.28...3.30)

# Amal Studio - panneau « Bibliothèque » (hadiths, séquence, mes textes)

find_package(Qt6 REQUIRED Widgets)

add_library(amal-bibliotheque MODULE)
add_library(OBS::amal-bibliotheque ALIAS amal-bibliotheque)

target_sources(amal-bibliotheque PRIVATE amal-bibliotheque.cpp)

target_link_libraries(amal-bibliotheque PRIVATE OBS::frontend-api OBS::libobs Qt::Widgets)

if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in amal-bibliotheque.rc)
  target_sources(amal-bibliotheque PRIVATE amal-bibliotheque.rc)
endif()

set_target_properties_obs(amal-bibliotheque PROPERTIES FOLDER plugins PREFIX "" AUTOMOC ON)
