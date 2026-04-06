# cmake/SponzaResources.cmake
# Fetch Intel Main Sponza (glTF) only if required files are missing.

option(RT_FETCH_SPONZA "Download Intel main_sponza assets if missing" ON)

set(SPONZA_ZIP_URL
  "https://cdrdv2.intel.com/v1/dl/getContent/830833"
  CACHE STRING "Intel download URL for main_sponza zip"
)

set(RT_REPO_ROOT "${CMAKE_SOURCE_DIR}")
set(RT_SPONZA_DST_DIR "${RT_REPO_ROOT}/RayTracing/Resources/Objects/sponza")
set(RT_SPONZA_TEX_DST_DIR "${RT_SPONZA_DST_DIR}/textures")

# Required top-level files
set(RT_SPONZA_ROOT_FILES
  "NewSponza_Main_glTF_003.gltf"
  "NewSponza_Main_glTF_003.bin"
)

# Required texture files (relative under textures/)
set(RT_SPONZA_TEXTURE_FILES
  "arch_stone_wall_01_BaseColor.png"
  "arch_stone_wall_01_Metalness.png"
  "arch_stone_wall_01_Normal.png"
  "arch_stone_wall_01_Roughness.png"
  "arch_stone_wall_01_Roughnessarch_stone_wall_01_Metalness.png"
  "brickwall_01_BaseColor.png"
  "brickwall_01_Metalness.png"
  "brickwall_01_Normal.png"
  "brickwall_01_Roughness.png"
  "brickwall_01_Roughnessbrickwall_01_Metalness.png"
  "brickwall_02_BaseColor.png"
  "brickwall_02_Metalness.png"
  "brickwall_02_Normal.png"
  "brickwall_02_Roughness.png"
  "brickwall_02_Roughnessbrickwall_02_Metalness.png"
  "ceiling_plaster_01_BaseColor.png"
  "ceiling_plaster_01_Metalness.png"
  "ceiling_plaster_01_Normal.png"
  "ceiling_plaster_01_Roughness.png"
  "ceiling_plaster_01_Roughnessceiling_plaster_01_Metalness.png"
  "ceiling_plaster_02_BaseColor.png"
  "ceiling_plaster_02_Metalness.png"
  "ceiling_plaster_02_Normal.png"
  "ceiling_plaster_02_Roughness.png"
  "ceiling_plaster_02_Roughnessceiling_plaster_01_Metalness.png"
  "col_1stfloor_BaseColor.png"
  "col_1stfloor_Metalness.png"
  "col_1stfloor_Normal.png"
  "col_1stfloor_Roughness.png"
  "col_1stfloor_Roughnesscol_1stfloor_Metalness.png"
  "col_brickwall_01_BaseColor.png"
  "col_brickwall_01_Metalness.png"
  "col_brickwall_01_Normal.png"
  "col_brickwall_01_Roughness.png"
  "col_brickwall_01_Roughnesscolumn_brickwall_01_Metalness.png"
  "col_brickwall_01_Roughnesscol_brickwall_01_Metalness.png"
  "col_head_1stfloor_BaseColor.png"
  "col_head_1stfloor_Metalness.png"
  "col_head_1stfloor_Normal.png"
  "col_head_1stfloor_Roughness.png"
  "col_head_1stfloor_Roughnesscol_head_1stfloor_Metalness.png"
  "col_head_2ndfloor_02_BaseColor.png"
  "col_head_2ndfloor_02_Metalness.png"
  "col_head_2ndfloor_02_Normal.png"
  "col_head_2ndfloor_02_Roughness.png"
  "col_head_2ndfloor_02_Roughnesscol_head_2ndfloor_02_Metalness.png"
  "col_head_2ndfloor_03_BaseColor.png"
  "col_head_2ndfloor_03_Metalness.png"
  "col_head_2ndfloor_03_Normal.png"
  "col_head_2ndfloor_03_Roughness.png"
  "col_head_2ndfloor_03_Roughnesscol_head_2ndfloor_03_Metalness.png"
  "curtain_fabric_blue_BaseColor.png"
  "curtain_fabric_green_BaseColor.png"
  "curtain_fabric_Metalness.png"
  "curtain_fabric_Normal.png"
  "curtain_fabric_red_BaseColor.png"
  "curtain_fabric_Roughness.png"
  "dirt_decal_01.png"
  "dirt_decal_01_alpha.png"
  "dirt_decal_01_dirt_decal_01_mask_alpha_dirt_decal_Opacity.png"
  "dirt_decal_01_dirt_decal_01_mask_gltf_alpha_dirt_decal_Opacity.png"
  "dirt_decal_01_invmask.png"
  "dirt_decal_01_mask.png"
  "dirt_decal_01_mask_gltf.png"
  "dirt_decal_01_mask_usd.png"
  "door_stoneframe_01_BaseColor.png"
  "door_stoneframe_01_Metalness.png"
  "door_stoneframe_01_Normal.png"
  "door_stoneframe_01_Roughness.png"
  "door_stoneframe_01_Roughnessdoor_stoneframe_01_Metalness.png"
  "door_stoneframe_02_BaseColor.png"
  "door_stoneframe_02_Metalness.png"
  "door_stoneframe_02_Normal.png"
  "door_stoneframe_02_Roughness.png"
  "door_stoneframe_02_Roughnessdoor_stoneframe_02_Metalness.png"
  "floor_tiles_01_BaseColor.png"
  "floor_tiles_01_Metalness.png"
  "floor_tiles_01_Normal.png"
  "floor_tiles_01_Roughness.png"
  "floor_tiles_01_Roughnessfloor_tiles_01_Metalness.png"
  "lionhead_01_BaseColor.png"
  "lionhead_01_Metalness.png"
  "lionhead_01_Normal.png"
  "lionhead_01_Roughness.png"
  "lionhead_01_Roughnesslionhead_01_Metalness.png"
  "metal_door_01_BaseColor.png"
  "metal_door_01_Metalness.png"
  "metal_door_01_Normal.png"
  "metal_door_01_Roughness.png"
  "metal_door_01_Roughnessmetal_door_01_Metalness.png"
  "ornament_01_BaseColor.png"
  "ornament_01_Metalness.png"
  "ornament_01_Normal.png"
  "ornament_01_Roughness.png"
  "ornament_01_Roughnessornament_01_Metalness.png"
  "roof_tiles_01_BaseColor.png"
  "roof_tiles_01_Metalness.png"
  "roof_tiles_01_Normal.png"
  "roof_tiles_01_Roughness.png"
  "roof_tiles_01_Roughnessroof_tiles_01_Metalness.png"
  "stones_2ndfloor_01_BaseColor.png"
  "stones_2ndfloor_01_Metalness.png"
  "stones_2ndfloor_01_Normal.png"
  "stones_2ndfloor_01_Roughness.png"
  "stones_2ndfloor_01_Roughnessstones_2ndfloor_01_Metalness.png"
  "stone_01_tile_BaseColor.png"
  "stone_01_tile_Metalness.png"
  "stone_01_tile_Normal.png"
  "stone_01_tile_Roughness.png"
  "stone_01_tile_Roughnessstone_01_tile_Metalness.png"
  "stone_trims_01_BaseColor.png"
  "stone_trims_01_Metalness.png"
  "stone_trims_01_Normal.png"
  "stone_trims_01_Roughness.png"
  "stone_trims_01_Roughnessstone_trims_01_Metalness.png"
  "stone_trims_02_BaseColor.png"
  "stone_trims_02_Metalness.png"
  "stone_trims_02_Normal.png"
  "stone_trims_02_Roughness.png"
  "stone_trims_02_Roughnessstone_trims_02_Metalness.png"
  "window_frame_01_BaseColor.png"
  "window_frame_01_Metalness.png"
  "window_frame_01_Normal.png"
  "window_frame_01_Roughness.png"
  "window_frame_01_Roughnesswindow_frame_01_Metalness.png"
  "wood_door_01_BaseColor.png"
  "wood_door_01_Metalness.png"
  "wood_door_01_Normal.png"
  "wood_door_01_Roughness.png"
  "wood_door_01_Roughnesswood_door_01_Metalness.png"
  "wood_tile_01_BaseColor.png"
  "wood_tile_01_Metalness.png"
  "wood_tile_01_Normal.png"
  "wood_tile_01_Roughness.png"
  "wood_tile_01_Roughnesswood_tile_01_Metalness.png"
)

# -------------------- Helpers ------------------------------------------------

function(rt_sponza_all_present out_var)
  set(_ok TRUE)

  foreach(_f IN LISTS RT_SPONZA_ROOT_FILES)
    if(NOT EXISTS "${RT_SPONZA_DST_DIR}/${_f}")
      set(_ok FALSE)
      break()
    endif()
  endforeach()

  if(_ok)
    foreach(_t IN LISTS RT_SPONZA_TEXTURE_FILES)
      if(NOT EXISTS "${RT_SPONZA_TEX_DST_DIR}/${_t}")
        set(_ok FALSE)
        break()
      endif()
    endforeach()
  endif()

  set(${out_var} ${_ok} PARENT_SCOPE)
endfunction()

function(rt_sponza_copy_if_missing src_path dst_path)
  if(EXISTS "${dst_path}")
    return()
  endif()
  get_filename_component(_dst_dir "${dst_path}" DIRECTORY)
  file(MAKE_DIRECTORY "${_dst_dir}")
  file(COPY_FILE "${src_path}" "${dst_path}" ONLY_IF_DIFFERENT)
endfunction()

# Find a file inside extracted dir by leaf name. Returns first match.
function(rt_sponza_find_file out_var root_dir leaf_name)
  set(${out_var} "" PARENT_SCOPE)
  file(GLOB_RECURSE _hits LIST_DIRECTORIES FALSE "${root_dir}/**/${leaf_name}")
  list(LENGTH _hits _n)
  if(_n GREATER 0)
    list(GET _hits 0 _first)
    set(${out_var} "${_first}" PARENT_SCOPE)
  endif()
endfunction()

# Try to locate the extracted "sponza root" by finding the .gltf file
function(rt_sponza_detect_root out_var extracted_dir)
  set(${out_var} "" PARENT_SCOPE)
  rt_sponza_find_file(_gltf "${extracted_dir}" "NewSponza_Main_glTF_003.gltf")
  if(NOT _gltf STREQUAL "")
    get_filename_component(_root "${_gltf}" DIRECTORY)
    set(${out_var} "${_root}" PARENT_SCOPE)
  endif()
endfunction()

function(rt_sponza_extract_zip zip_path dst_dir)
  file(REMOVE_RECURSE "${dst_dir}")
  file(MAKE_DIRECTORY "${dst_dir}")

  # 1) Try with CMake tar
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar xvf "${zip_path}"
    WORKING_DIRECTORY "${dst_dir}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE  _err
  )

  if(_rc EQUAL 0)
    return()
  endif()

  message(WARNING
    "Sponza: cmake -E tar failed (rc=${_rc}). Trying PowerShell Expand-Archive...\n"
    "CMake tar stderr:\n${_err}\n"
  )

  # 2) Fallback: PowerShell Expand-Archive (often handles Windows zip quirks better)
  # Escape single quotes for PowerShell string literal
  set(_zip_ps "${zip_path}")
  set(_dst_ps "${dst_dir}")
  string(REPLACE "'" "''" _zip_ps "${_zip_ps}")
  string(REPLACE "'" "''" _dst_ps "${_dst_ps}")

  execute_process(
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
      -Command "Expand-Archive -LiteralPath '${_zip_ps}' -DestinationPath '${_dst_ps}' -Force"
    RESULT_VARIABLE _rc2
    OUTPUT_VARIABLE _out2
    ERROR_VARIABLE  _err2
  )

  if(NOT _rc2 EQUAL 0)
    message(FATAL_ERROR
      "Sponza: extraction failed with both methods.\n\n"
      "CMake tar error:\n${_err}\n\n"
      "PowerShell error:\n${_err2}\n\n"
      "Typical fixes on Windows:\n"
      " - move repo/build to a shorter path (e.g. C:/rt) OR\n"
      " - enable Win32 long paths in Windows settings/group policy.\n"
    )
  endif()
endfunction()

# -------------------- Main ---------------------------------------------------

function(rt_fetch_sponza_if_missing)
  if(NOT RT_FETCH_SPONZA)
    message(STATUS "Sponza: auto-fetch disabled (RT_FETCH_SPONZA=OFF)")
    return()
  endif()

  rt_sponza_all_present(_have_all)
  if(_have_all)
    message(STATUS "Sponza: all required files already present. Skipping download.")
    return()
  endif()

  set(_base "${CMAKE_BINARY_DIR}/_deps/sponza")
  set(_zip  "${_base}/main_sponza.zip")
  set(_src  "${CMAKE_BINARY_DIR}/_spz")

  file(MAKE_DIRECTORY "${_base}")

  # Download only because something is missing
  if(NOT EXISTS "${_zip}")
    message(STATUS "Sponza: downloading zip (large) -> ${_zip}")
    file(DOWNLOAD
      "${SPONZA_ZIP_URL}"
      "${_zip}"
      SHOW_PROGRESS
      STATUS _dl_status
      TLS_VERIFY ON
    )
    list(GET _dl_status 0 _dl_code)
    list(GET _dl_status 1 _dl_msg)
    if(NOT _dl_code EQUAL 0)
      message(FATAL_ERROR "Sponza: zip download failed (${_dl_code}): ${_dl_msg}")
    endif()
  else()
    message(STATUS "Sponza: zip already downloaded: ${_zip}")
  endif()

  # Extract only if not extracted
  if(NOT IS_DIRECTORY "${_src}")
    file(MAKE_DIRECTORY "${_src}")
  endif()

  # marker to avoid re-extracting every configure
 set(_marker "${_src}/.extracted_marker")

  if(NOT EXISTS "${_marker}")
    message(STATUS "Sponza: extracting zip -> ${_src}")
    rt_sponza_extract_zip("${_zip}" "${_src}")
    file(WRITE "${_marker}" "ok")
  else()
    message(STATUS "Sponza: already extracted (marker present).")
  endif()

  # Detect root inside extracted content
  rt_sponza_detect_root(_sponza_root "${_src}")
  if(_sponza_root STREQUAL "")
    message(FATAL_ERROR "Sponza: could not find NewSponza_Main_glTF_003.gltf in extracted zip: ${_src}")
  endif()
  message(STATUS "Sponza: detected extracted root: ${_sponza_root}")

  file(MAKE_DIRECTORY "${RT_SPONZA_DST_DIR}")
  file(MAKE_DIRECTORY "${RT_SPONZA_TEX_DST_DIR}")

  # Copy root files
  foreach(_f IN LISTS RT_SPONZA_ROOT_FILES)
    rt_sponza_find_file(_src_file "${_sponza_root}" "${_f}")
    if(_src_file STREQUAL "")
      # fallback search whole extracted tree
      rt_sponza_find_file(_src_file "${_src}" "${_f}")
    endif()
    if(_src_file STREQUAL "")
      message(FATAL_ERROR "Sponza: missing in zip: ${_f}")
    endif()
    rt_sponza_copy_if_missing("${_src_file}" "${RT_SPONZA_DST_DIR}/${_f}")
  endforeach()

  # Copy textures
  foreach(_t IN LISTS RT_SPONZA_TEXTURE_FILES)
    # most likely under <root>/textures/<name>
    set(_expected "${_sponza_root}/textures/${_t}")
    if(EXISTS "${_expected}")
      rt_sponza_copy_if_missing("${_expected}" "${RT_SPONZA_TEX_DST_DIR}/${_t}")
    else()
      # fallback search
      rt_sponza_find_file(_src_tex "${_src}" "${_t}")
      if(_src_tex STREQUAL "")
        message(FATAL_ERROR "Sponza: missing texture in zip: ${_t}")
      endif()
      rt_sponza_copy_if_missing("${_src_tex}" "${RT_SPONZA_TEX_DST_DIR}/${_t}")
    endif()
  endforeach()

  # Final sanity
  rt_sponza_all_present(_have_all2)
  if(NOT _have_all2)
    message(FATAL_ERROR "Sponza: after install, some required files are still missing under: ${RT_SPONZA_DST_DIR}")
  endif()

  message(STATUS "Sponza: done.")
endfunction()

rt_fetch_sponza_if_missing()
