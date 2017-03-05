cmake_minimum_required(VERSION 2.8.7)

project(klee-1.3.0)
include(ExternalProject)

execute_process(COMMAND llvm-config-3.4 --src-root
  OUTPUT_VARIABLE LLVMSRC_PATH
  OUTPUT_STRIP_TRAILING_WHITESPACE)

execute_process(COMMAND llvm-config-3.4 --obj-root
  OUTPUT_VARIABLE LLVMOBJ_PATH
  OUTPUT_STRIP_TRAILING_WHITESPACE)

ExternalProject_Add(
  klee-1.3.0

  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}
  BUILD_IN_SOURCE 1

  DOWNLOAD_COMMAND ""
  UPDATE_COMMAND ""

  CONFIGURE_COMMAND
  ./configure
  --with-stp=${CMAKE_BINARY_DIR}/lib/stp/stp-2.1.2-prefix/src/stp-2.1.2-build/
  --with-llvmsrc=${LLVMSRC_PATH}
  --with-llvmobj=${LLVMOBJ_PATH}
  CXX=clang-3.4

  BUILD_COMMAND
  . ./crete_klee_prebuild.sh  &&
  make -j7

  INSTALL_COMMAND ln -sf ${CMAKE_CURRENT_SOURCE_DIR}/Release+Asserts/bin/klee ${CMAKE_BINARY_DIR}/bin/crete-klee-1.3.0
  )

add_dependencies(klee-1.3.0 stp-2.1.2)

add_custom_target(klee-1.3.0-remake ALL
  COMMAND
  . ./crete_klee_prebuild.sh &&
  make -j7

  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS klee-1.3.0)
