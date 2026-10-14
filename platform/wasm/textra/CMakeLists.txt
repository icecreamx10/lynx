# Copyright 2026 The Lynx Authors. All rights reserved.
# Licensed under the Apache License Version 2.0 that can be found in the
# LICENSE file in the root directory of this source tree.
cmake_minimum_required(VERSION 3.20)
project(lynx_browser_tttext LANGUAGES CXX)
if(NOT DEFINED TEXTRA_SOURCE)
  message(FATAL_ERROR "Set TEXTRA_SOURCE to the pinned lynx-textra checkout")
endif()
# The engine and its substitute bidi implementation are platform independent.
# System/JNI/CoreText/ICU-dlopen ports are deliberately excluded.
file(GLOB_RECURSE engine_sources CONFIGURE_DEPENDS
  "${TEXTRA_SOURCE}/src/textlayout/*.cc"
  "${TEXTRA_SOURCE}/src/textlayout/*.cpp")
list(FILTER engine_sources EXCLUDE REGEX "/utils/icu_wrapper[^/]*\\.cc$")
list(FILTER engine_sources EXCLUDE REGEX "/test/")
set(shaper_dir "${TEXTRA_SOURCE}/src/ports/shaper/skshaper")
add_library(lynx_browser_tttext STATIC ${engine_sources}
  "${TEXTRA_SOURCE}/src/ports/platform_helper.cc"
  "${shaper_dir}/one_line_shaper.cc"
  "${shaper_dir}/run.cc"
  "${shaper_dir}/shaper_skshaper.cc"
  "${shaper_dir}/sk_shaper.cc"
  "${shaper_dir}/sk_shaper_harfbuzz.cc"
  "${shaper_dir}/sk_shaper_platform_dummy.cc"
  "${shaper_dir}/harfbuzz_lib.cc"
  "${shaper_dir}/harfbuzz_lib_builtin.cc")
target_compile_features(lynx_browser_tttext PUBLIC cxx_std_17)
target_include_directories(lynx_browser_tttext PUBLIC
  "${TEXTRA_SOURCE}/public" PRIVATE "${TEXTRA_SOURCE}"
  "${TEXTRA_SOURCE}/src/textlayout" "${TEXTRA_SOURCE}/src/ports")
target_compile_definitions(lynx_browser_tttext PRIVATE
  ENABLE_SKSHAPER SK_SHAPER_HARFBUZZ_AVAILABLE HAVE_PTHREADS USE_LRU_CACHE=1)
target_compile_options(lynx_browser_tttext PRIVATE
  -pthread -fno-exceptions -sUSE_HARFBUZZ=1)
