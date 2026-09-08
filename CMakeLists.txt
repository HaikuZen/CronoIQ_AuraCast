# CronoIQ_AuraCast — Waveshare ESP32-S3-RLCD-4.2 smart clock
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cronoiq_auracast)
