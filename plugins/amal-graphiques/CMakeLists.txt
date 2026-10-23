cmake_minimum_required(VERSION 3.28...3.30)

# Amal Studio - source « Amal Graphique » (habillages islamiques rendus par la source navigateur)

add_library(amal-graphiques MODULE)
add_library(OBS::amal-graphiques ALIAS amal-graphiques)

target_sources(amal-graphiques PRIVATE amal-graphiques.cpp)

target_link_libraries(amal-graphiques PRIVATE OBS::libobs)

if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in amal-graphiques.rc)
  target_sources(amal-graphiques PRIVATE amal-graphiques.rc)
endif()

set_target_properties_obs(amal-graphiques PROPERTIES FOLDER plugins PREFIX "")
