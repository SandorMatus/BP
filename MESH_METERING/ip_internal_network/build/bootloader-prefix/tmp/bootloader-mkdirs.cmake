# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "D:/ESP-IDF/esp-idf/components/bootloader/subproject"
  "D:/ESP-IDF/MESH_METERING/ip_internal_network/build/bootloader"
  "D:/ESP-IDF/MESH_METERING/ip_internal_network/build/bootloader-prefix"
  "D:/ESP-IDF/MESH_METERING/ip_internal_network/build/bootloader-prefix/tmp"
  "D:/ESP-IDF/MESH_METERING/ip_internal_network/build/bootloader-prefix/src/bootloader-stamp"
  "D:/ESP-IDF/MESH_METERING/ip_internal_network/build/bootloader-prefix/src"
  "D:/ESP-IDF/MESH_METERING/ip_internal_network/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/ESP-IDF/MESH_METERING/ip_internal_network/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/ESP-IDF/MESH_METERING/ip_internal_network/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
