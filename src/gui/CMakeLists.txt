# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.16)

project(afc_gui C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
pkg_check_modules(GIO gio-2.0)

# On macOS, build a .app bundle so users can grant Full Disk Access
add_executable(afc_gui MACOSX_BUNDLE
    main.c
    main_window.c
    main_window.h
    drop_area.c
    drop_area.h
    job_thread.c
    job_thread.h
)

target_include_directories(afc_gui PRIVATE ${GTK3_INCLUDE_DIRS} ${GIO_INCLUDE_DIRS})
target_compile_options(afc_gui PRIVATE ${GTK3_CFLAGS_OTHER} ${GIO_CFLAGS_OTHER})
target_link_directories(afc_gui PRIVATE ${GTK3_LIBRARY_DIRS} ${GIO_LIBRARY_DIRS})
target_link_libraries(afc_gui PRIVATE ${GTK3_LIBRARIES} ${GIO_LIBRARIES})

# Provide a minimal Info.plist for the macOS bundle
if(APPLE)
  set_target_properties(afc_gui PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "Advanced File Compressor"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
  )
endif()

# Embed absolute project source dir so runtime can locate CLI regardless of cwd
target_compile_definitions(afc_gui PRIVATE AFC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# Runtime note: CLI binaries expected in ${CMAKE_SOURCE_DIR}/build/cli/

# On macOS, bundle CLI binaries inside the .app for reliable discovery
if(APPLE)
  set(AFC_APP_BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/afc_gui.app/Contents/MacOS")
  # Copy the unified CLI if it exists
  if (EXISTS "${CMAKE_SOURCE_DIR}/build/cli/universal")
    add_custom_command(TARGET afc_gui POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/build/cli/universal" "${AFC_APP_BIN_DIR}/universal"
      COMMENT "Bundling universal CLI into afc_gui.app")
  endif()
  # Also copy legacy fallbacks if present
  if (EXISTS "${CMAKE_SOURCE_DIR}/build/cli/compress_file")
    add_custom_command(TARGET afc_gui POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/build/cli/compress_file" "${AFC_APP_BIN_DIR}/compress_file"
      COMMENT "Bundling compress_file into afc_gui.app")
  endif()
  if (EXISTS "${CMAKE_SOURCE_DIR}/build/cli/decompress_file")
    add_custom_command(TARGET afc_gui POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/build/cli/decompress_file" "${AFC_APP_BIN_DIR}/decompress_file"
      COMMENT "Bundling decompress_file into afc_gui.app")
  endif()
  # Copy bin/* fallbacks if they exist (Windows-style names built on macOS)
  if (EXISTS "${CMAKE_SOURCE_DIR}/bin/universal_comp.exe")
    add_custom_command(TARGET afc_gui POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/bin/universal_comp.exe" "${AFC_APP_BIN_DIR}/universal_comp.exe"
      COMMENT "Bundling universal_comp.exe into afc_gui.app")
  endif()
  if (EXISTS "${CMAKE_SOURCE_DIR}/bin/universal_decompressor.exe")
    add_custom_command(TARGET afc_gui POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/bin/universal_decompressor.exe" "${AFC_APP_BIN_DIR}/universal_decompressor.exe"
      COMMENT "Bundling universal_decompressor.exe into afc_gui.app")
  endif()
endif()