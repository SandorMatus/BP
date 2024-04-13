# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)
# This component includes modbus example common definitions
set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/system/console/advanced/components
                         $ENV{IDF_PATH}/examples/common_components/iperf)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ip_internal_network)

spiffs_create_partition_image(storage font FLASH_IN_PROJECT)
