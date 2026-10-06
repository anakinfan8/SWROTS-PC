# Writes the build's version (git describe: the release tag, or the commit) to a header, at every build,
# rewriting it only when it changed. cmake -DSOURCE=<repo> -DOUTPUT=<header> -P version.cmake
execute_process(COMMAND git describe --tags --always --dirty
  WORKING_DIRECTORY "${SOURCE}" OUTPUT_VARIABLE version OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT version)
  set(version "unknown")
endif()
set(content "#pragma once\n#define SWROTS_VERSION \"${version}\"\n")
if(EXISTS "${OUTPUT}")
  file(READ "${OUTPUT}" current)
endif()
if(NOT current STREQUAL content)
  file(WRITE "${OUTPUT}" "${content}")
endif()
