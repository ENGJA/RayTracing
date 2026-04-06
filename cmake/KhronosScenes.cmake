# cmake/KhronosScenes.cmake
# Fetch selected Khronos glTF-Sample-Assets models (per-file) only if missing.

option(RT_FETCH_KHRONOS_SCENES "Download Khronos glTF sample scenes if missing" ON)

set(KHRONOS_ASSETS_REPO "KhronosGroup/glTF-Sample-Assets" CACHE STRING "GitHub repo for glTF sample assets")
set(KHRONOS_ASSETS_REF  "main" CACHE STRING "Git ref/branch/tag for glTF sample assets")
set(KHRONOS_RAW_BASE
  "https://raw.githubusercontent.com/${KHRONOS_ASSETS_REPO}/${KHRONOS_ASSETS_REF}"
  CACHE STRING "Base URL for raw files"
)

set(RT_REPO_ROOT "${CMAKE_SOURCE_DIR}")
set(RT_OBJECTS_ROOT "${RT_REPO_ROOT}/RayTracing/Resources/Objects")

# -------------------- Scene definitions -------------------------------------
# For each scene:
#  - destination: RayTracing/Resources/Objects/<Scene>/glTF
#  - source:      Models/<Scene>/glTF/<file>

set(RT_SCENE_ABEAUTIFULGAME "ABeautifulGame")
set(RT_SCENE_ALPHABLENDMODETEST "AlphaBlendModeTest")

set(RT_ABEAUTIFULGAME_FILES
  "ABeautifulGame.bin"
  "ABeautifulGame.gltf"
  "Bishop_black_base_color.jpg"
  "Bishop_black_normal.jpg"
  "Bishop_black_ORM.jpg"
  "Bishop_white_base_color.jpg"
  "Bishop_white_normal.jpg"
  "Bishop_white_ORM.jpg"
  "Castle_black_base_color.jpg"
  "Castle_normal.jpg"
  "Castle_ORM.jpg"
  "Castle_white_base_color.jpg"
  "Chessboard_base_color.jpg"
  "Chessboard_normal.jpg"
  "Chessboard_ORM.jpg"
  "King_black_base_color.jpg"
  "King_black_normal.jpg"
  "King_black_ORM.jpg"
  "King_white_base_color.jpg"
  "King_white_normal.jpg"
  "King_white_ORM.jpg"
  "Knight_black_base_color.jpg"
  "Knight_normal.jpg"
  "Knight_ORM.jpg"
  "Knight_white_base_color.jpg"
  "Pawn_black_base_color.jpg"
  "Pawn_normal.jpg"
  "Pawn_ORM.jpg"
  "Pawn_white_base_color.jpg"
  "Queen_black_base_color.jpg"
  "Queen_black_normal.jpg"
  "Queen_black_ORM.jpg"
  "Queen_white_base_color.jpg"
  "Queen_white_normal.jpg"
  "Queen_white_ORM.jpg"
)

set(RT_ALPHABLENDMODETEST_FILES
  "AlphaBlendLabels.png"
  "AlphaBlendModeTest.bin"
  "AlphaBlendModeTest.gltf"
  "MatBed_baseColor.jpg"
  "MatBed_normal.jpg"
  "MatBed_occlusionRoughnessMetallic.jpg"
)

# -------------------- Helpers ------------------------------------------------

function(rt_khronos_all_files_exist out_var dst_dir)
  set(_ok TRUE)
  foreach(_f IN LISTS ARGN)
    if(NOT EXISTS "${dst_dir}/${_f}")
      set(_ok FALSE)
      break()
    endif()
  endforeach()
  set(${out_var} ${_ok} PARENT_SCOPE)
endfunction()

function(rt_khronos_download_file url dst_path)
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
    message(FATAL_ERROR "Khronos scene: download failed (${_code}): ${url}\n  -> ${dst_path}\n  msg: ${_msg}")
  endif()
endfunction()

function(rt_khronos_fetch_scene scene_name file_list_var)
  set(_dst "${RT_OBJECTS_ROOT}/${scene_name}/glTF")
  rt_khronos_all_files_exist(_have_all "${_dst}" ${${file_list_var}})
  if(_have_all)
    message(STATUS "Khronos scene '${scene_name}': already present. Skipping.")
    return()
  endif()

  file(MAKE_DIRECTORY "${_dst}")
  message(STATUS "Khronos scene '${scene_name}': installing missing files -> ${_dst}")

  foreach(_f IN LISTS ${file_list_var})
    set(_dst_file "${_dst}/${_f}")
    if(EXISTS "${_dst_file}")
      continue()
    endif()

    # Source path in repo:
    #   Models/<Scene>/glTF/<file>
    set(_src_path "Models/${scene_name}/glTF/${_f}")
    set(_url "${KHRONOS_RAW_BASE}/${_src_path}")

    message(STATUS "Khronos scene '${scene_name}': downloading ${_f}")
    rt_khronos_download_file("${_url}" "${_dst_file}")
  endforeach()

  rt_khronos_all_files_exist(_have_all2 "${_dst}" ${${file_list_var}})
  if(NOT _have_all2)
    message(FATAL_ERROR "Khronos scene '${scene_name}': after install, some required files are still missing under: ${_dst}")
  endif()

  message(STATUS "Khronos scene '${scene_name}': done.")
endfunction()

# -------------------- Main ---------------------------------------------------

function(rt_fetch_khronos_scenes_if_missing)
  if(NOT RT_FETCH_KHRONOS_SCENES)
    message(STATUS "Khronos scenes: auto-fetch disabled (RT_FETCH_KHRONOS_SCENES=OFF)")
    return()
  endif()

  rt_khronos_fetch_scene("${RT_SCENE_ABEAUTIFULGAME}" RT_ABEAUTIFULGAME_FILES)
  rt_khronos_fetch_scene("${RT_SCENE_ALPHABLENDMODETEST}" RT_ALPHABLENDMODETEST_FILES)
endfunction()

rt_fetch_khronos_scenes_if_missing()
