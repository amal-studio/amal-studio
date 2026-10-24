cmake_minimum_required(VERSION 3.28...3.30)

# Amal Studio - panneau « Caméra téléphone » (réception SRT, QR code Larix)

find_package(Qt6 REQUIRED Widgets Network)

set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)
find_package(qrcodegencpp REQUIRED)
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG OFF)

add_library(amal-telephone MODULE)
add_library(OBS::amal-telephone ALIAS amal-telephone)

target_sources(amal-telephone PRIVATE amal-telephone.cpp)

target_link_libraries(
  amal-telephone
  PRIVATE OBS::frontend-api OBS::libobs Qt::Widgets Qt::Network qrcodegencpp::qrcodegencpp
)

if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in amal-telephone.rc)
  target_sources(amal-telephone PRIVATE amal-telephone.rc)
endif()

set_target_properties_obs(amal-telephone PROPERTIES FOLDER plugins PREFIX "" AUTOMOC ON)
