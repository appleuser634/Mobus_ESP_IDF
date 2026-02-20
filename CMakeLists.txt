cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(EXTRA_COMPONENT_DIRS
    $ENV{HOME}/Mo-Bus/Mobus_ESP_IDF/components
    $ENV{HOME}/Mo-Bus/Mobus_ESP_IDF/components/drivers
    $ENV{HOME}/Mo-Bus/Mobus_ESP_IDF/components/services)
project(Mobus_V2)
