cmake_minimum_required(VERSION 3.28...3.30)

# Amal Studio - panneau « Dispositions » (1 à 4 intervenants, cadres stylés)

find_package(Qt6 REQUIRED Widgets)

add_library(amal-dispositions MODULE)
add_library(OBS::amal-dispositions ALIAS amal-dispositions)

target_sources(amal-dispositions PRIVATE amal-dispositions.cpp)

target_link_libraries(amal-dispositions PRIVATE OBS::frontend-api OBS::libobs Qt::Widgets)

if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in amal-dispositions.rc)
  target_sources(amal-dispositions PRIVATE amal-dispositions.rc)
endif()

set_target_properties_obs(amal-dispositions PROPERTIES FOLDER plugins PREFIX "" AUTOMOC ON)
