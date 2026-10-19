cmake_minimum_required(VERSION 3.16)

# Re-use the mlkem component from the original esp32s3 project.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../esp32s3/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mlkem_dualcore)
