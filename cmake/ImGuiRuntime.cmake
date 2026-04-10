# cmake/ImGuiRuntime.cmake
# Fetch Dear ImGui sources/headers (core + DX12/Win32 backends) into RayTracing/Includes/imgui

option(RT_FETCH_IMGUI "Download Dear ImGui files if missing" ON)

# Pin to a tag for reproducible builds (can be overridden to "master")
set(IMGUI_REF "v1.92.5" CACHE STRING "ImGui git ref/tag/branch (e.g. v1.92.5 or master)") # latest release as of Nov 20, 2025
set(IMGUI_REPO "ocornut/imgui" CACHE STRING "GitHub repo for ImGui")
set(IMGUI_RAW_BASE
  "https://raw.githubusercontent.com/${IMGUI_REPO}/${IMGUI_REF}"
  CACHE STRING "Base URL for raw ImGui files"
)

set(RT_REPO_ROOT "${CMAKE_SOURCE_DIR}")
set(RT_IMGUI_INCLUDE_DIR "${RT_REPO_ROOT}/RayTracing/Includes/imgui")

# Map: DEST_FILENAME | SOURCE_PATH_IN_REPO
# Destination is always flat in RayTracing/Includes/imgui
set(RT_IMGUI_FILE_MAP
  "imconfig.h|imconfig.h"
  "imgui.cpp|imgui.cpp"
  "imgui.h|imgui.h"
  "imgui_draw.cpp|imgui_draw.cpp"
  "imgui_internal.h|imgui_internal.h"
  "imgui_tables.cpp|imgui_tables.cpp"
  "imgui_widgets.cpp|imgui_widgets.cpp"
  "imstb_rectpack.h|imstb_rectpack.h"
  "imstb_textedit.h|imstb_textedit.h"
  "imstb_truetype.h|imstb_truetype.h"

  "imgui_impl_dx12.cpp|backends/imgui_impl_dx12.cpp"
  "imgui_impl_dx12.h|backends/imgui_impl_dx12.h"
  "imgui_impl_win32.cpp|backends/imgui_impl_win32.cpp"
  "imgui_impl_win32.h|backends/imgui_impl_win32.h"
)

# -------------------- Helpers ------------------------------------------------

function(rt_imgui_all_files_exist out_var base_dir)
  set(_ok TRUE)
  foreach(_entry IN LISTS RT_IMGUI_FILE_MAP)
    string(REPLACE "|" ";" _parts "${_entry}")
    list(GET _parts 0 _dst_name)
    if(NOT EXISTS "${base_dir}/${_dst_name}")
      set(_ok FALSE)
      break()
    endif()
  endforeach()
  set(${out_var} ${_ok} PARENT_SCOPE)
endfunction()

function(rt_imgui_download_file url dst_path)
  get_filename_component(_dst_dir "${dst_path}" DIRECTORY)
  file(MAKE_DIRECTORY "${_dst_dir}")

  file(DOWNLOAD
    "${url}"
    "${dst_path}"
    STATUS _dl_status
    TLS_VERIFY ON
  )
  list(GET _dl_status 0 _code)
  list(GET _dl_status 1 _msg)
  if(NOT _code EQUAL 0)
    message(FATAL_ERROR "ImGui: download failed (${_code}): ${url}\n  -> ${dst_path}\n  msg: ${_msg}")
  endif()
endfunction()

# -------------------- Main ---------------------------------------------------

function(rt_fetch_imgui_if_missing)
  if(NOT RT_FETCH_IMGUI)
    message(STATUS "ImGui: auto-fetch disabled (RT_FETCH_IMGUI=OFF)")
    return()
  endif()

  rt_imgui_all_files_exist(_have_all "${RT_IMGUI_INCLUDE_DIR}")
  if(_have_all)
    message(STATUS "ImGui: all required files already present. Skipping.")
    return()
  endif()

  file(MAKE_DIRECTORY "${RT_IMGUI_INCLUDE_DIR}")

  message(STATUS "ImGui: installing missing files -> ${RT_IMGUI_INCLUDE_DIR} (ref: ${IMGUI_REF})")

  foreach(_entry IN LISTS RT_IMGUI_FILE_MAP)
    string(REPLACE "|" ";" _parts "${_entry}")
    list(GET _parts 0 _dst_name)
    list(GET _parts 1 _src_path)

    set(_dst "${RT_IMGUI_INCLUDE_DIR}/${_dst_name}")
    if(EXISTS "${_dst}")
      continue()
    endif()

    set(_url "${IMGUI_RAW_BASE}/${_src_path}")
    message(STATUS "ImGui: downloading ${_dst_name}  <-  ${_src_path}")
    rt_imgui_download_file("${_url}" "${_dst}")
  endforeach()

  rt_imgui_all_files_exist(_have_all2 "${RT_IMGUI_INCLUDE_DIR}")
  if(NOT _have_all2)
    message(FATAL_ERROR "ImGui: after install, some required files are still missing under: ${RT_IMGUI_INCLUDE_DIR}")
  endif()

  message(STATUS "ImGui: done.")
endfunction()

rt_fetch_imgui_if_missing()
