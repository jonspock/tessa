# Copyright 2018 TessaCoin Developers
# See LICENSE file or http://opensource.org/licenses/MIT for terms

cmake_minimum_required(VERSION 2.8)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}")

add_library(lmdb mdb.c midl.c)
set_property(TARGET lmdb APPEND PROPERTY COMPILE_FLAGS "-fPIC")
