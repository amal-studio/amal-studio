cmake_minimum_required(VERSION 3.28...3.30)

# Amal Studio - Licence Amal Studio Pro (essai de 30 jours, clés signées Ed25519)
# Monocypher 4.0.3 (BSD 2 clauses / CC0) : vérification des signatures

find_package(Qt6 REQUIRED Widgets Network)

add_library(amal-licence MODULE)
add_library(OBS::amal-licence ALIAS amal-licence)

target_sources(
  amal-licence
  PRIVATE
    amal-licence.cpp
    monocypher/monocypher.c
    monocypher/monocypher.h
    monocypher/monocypher-ed25519.c
    monocypher/monocypher-ed25519.h
)

target_link_libraries(amal-licence PRIVATE OBS::frontend-api OBS::libobs Qt::Widgets Qt::Network)
target_include_directories(amal-licence PRIVATE "${CMAKE_SOURCE_DIR}/shared")

if(MSVC)
  set_source_files_properties(
    monocypher/monocypher.c
    monocypher/monocypher-ed25519.c
    PROPERTIES COMPILE_OPTIONS "/wd4146;/wd4244;/wd4267;/wd4701"
  )
endif()

if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in amal-licence.rc)
  target_sources(amal-licence PRIVATE amal-licence.rc)
endif()

set_target_properties_obs(amal-licence PROPERTIES FOLDER plugins PREFIX "" AUTOMOC ON)
