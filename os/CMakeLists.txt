cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Release stamp baked into esp_app_desc (read by esptool image-info and the
# future system-status page); keep in sync with the git release tag.
set(PROJECT_VER "v0.1.0-test.1")
project(matter_home_panel)
