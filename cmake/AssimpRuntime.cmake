# cmake/AssimpRuntime.cmake
# Fetch Assimp headers (raw from GitHub) + runtime (dll+lib).
# Strategy:
#  - If all required files exist -> do nothing.
#  - Else try runtime ZIP (DLL usually inside Release/). If .lib missing in ZIP -> build from source ZIP and copy .lib (and dll if needed).

option(RT_FETCH_ASSIMP "Download/build Assimp if missing" ON)

set(ASSIMP_VERSION "6.0.4" CACHE STRING "Assimp version (e.g. 6.0.4)")

# Auto arch detection (can be overridden)
set(ASSIMP_ARCH "auto" CACHE STRING "Assimp runtime zip arch: auto|x64|x86")
set_property(CACHE ASSIMP_ARCH PROPERTY STRINGS auto x64 x86)

# Default runtime ZIP URLs (can be overridden)
set(ASSIMP_ZIP_URL_X64
  "https://github.com/assimp/assimp/releases/download/v${ASSIMP_VERSION}/windows-x64-v${ASSIMP_VERSION}.zip"
  CACHE STRING "Assimp prebuilt Windows x64 zip URL"
)
set(ASSIMP_ZIP_URL_X86
  "https://github.com/assimp/assimp/releases/download/v${ASSIMP_VERSION}/windows-x86-v${ASSIMP_VERSION}.zip"
  CACHE STRING "Assimp prebuilt Windows x86 zip URL"
)
set(ASSIMP_ZIP_URL "" CACHE STRING "Override: explicit Assimp prebuilt zip URL (optional)")

# Source ZIP (fallback to build .lib)
set(ASSIMP_SOURCE_ZIP_URL
  "https://github.com/assimp/assimp/archive/refs/tags/v${ASSIMP_VERSION}.zip"
  CACHE STRING "Assimp source zip URL (fallback to build .lib)"
)

# Headers (raw) base URL
set(ASSIMP_HEADERS_BASE_URL
  "https://raw.githubusercontent.com/assimp/assimp/master/include/assimp"
  CACHE STRING "Base URL for Assimp headers (raw.githubusercontent.com)"
)

# Repo paths
set(RT_REPO_ROOT "${CMAKE_SOURCE_DIR}")

set(RT_ASSIMP_INCLUDE_DIR "${RT_REPO_ROOT}/RayTracing/Includes/assimp")
set(RT_DLLS_ROOT          "${RT_REPO_ROOT}/RayTracing/Dlls")
set(RT_ASSIMP_DLL_REL_DIR "${RT_DLLS_ROOT}/Release")
set(RT_ASSIMP_DLL_DBG_DIR "${RT_DLLS_ROOT}/Debug")
set(RT_ASSIMP_LIBS_DIR    "${RT_REPO_ROOT}/RayTracing/Libs")

# Required runtime files (your naming)
set(RT_ASSIMP_DLL_NAME "assimp-vc143-mt.dll")
set(RT_ASSIMP_LIB_NAME "assimp-vc143-mt.lib")

# -------------------- Required header file list ------------------------------
# Relative paths under: include/assimp/...
set(RT_ASSIMP_HEADER_FILES
  ".editorconfig"
  "aabb.h"
  "ai_assert.h"
  "anim.h"
  "AssertHandler.h"
  "Base64.hpp"
  "BaseImporter.h"
  "Bitmap.h"
  "BlobIOSystem.h"
  "ByteSwapper.h"
  "camera.h"
  "cexport.h"
  "cfileio.h"
  "cimport.h"
  "ColladaMetaData.h"
  "color4.h"
  "color4.inl"
  "commonMetaData.h"
  "config.h.in"
  "CreateAnimMesh.h"
  "DefaultIOStream.h"
  "DefaultIOSystem.h"
  "DefaultLogger.hpp"
  "defs.h"
  "Exceptional.h"
  "Exporter.hpp"
  "fast_atof.h"
  "GenericProperty.h"
  "GltfMaterial.h"
  "Hash.h"
  "Importer.hpp"
  "importerdesc.h"
  "IOStream.hpp"
  "IOStreamBuffer.h"
  "IOSystem.hpp"
  "light.h"
  "LineSplitter.h"
  "LogAux.h"
  "Logger.hpp"
  "LogStream.hpp"
  "material.h"
  "material.inl"
  "MathFunctions.h"
  "matrix3x3.h"
  "matrix3x3.inl"
  "matrix4x4.h"
  "matrix4x4.inl"
  "MemoryIOWrapper.h"
  "mesh.h"
  "metadata.h"
  "module.modulemap"
  "NullLogger.hpp"
  "ObjMaterial.h"
  "ParsingUtils.h"
  "pbrmaterial.h"
  "postprocess.h"
  "Profiler.h"
  "ProgressHandler.hpp"
  "qnan.h"
  "quaternion.h"
  "quaternion.inl"
  "RemoveComments.h"
  "revision.h.in"
  "scene.h"
  "SceneCombiner.h"
  "SGSpatialSort.h"
  "SkeletonMeshBuilder.h"
  "SmallVector.h"
  "SmoothingGroups.h"
  "SmoothingGroups.inl"
  "SpatialSort.h"
  "StandardShapes.h"
  "StreamReader.h"
  "StreamWriter.h"
  "StringComparison.h"
  "StringUtils.h"
  "Subdivision.h"
  "texture.h"
  "TinyFormatter.h"
  "types.h"
  "vector2.h"
  "vector2.inl"
  "vector3.h"
  "vector3.inl"
  "version.h"
  "Vertex.h"
  "XmlParser.h"
  "XMLTools.h"
  "ZipArchiveIOSystem.h"

  "Compiler/poppack1.h"
  "Compiler/pstdint.h"
  "Compiler/pushpack1.h"

  "port/AndroidJNI/AndroidJNIIOSystem.h"
  "port/AndroidJNI/BundledAssetIOSystem.h"
)

# -------------------- Helpers ------------------------------------------------

function(rt_run_checked)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE  _err
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
      "Command failed (rc=${_rc}): ${ARGN}\n\nSTDOUT:\n${_out}\n\nSTDERR:\n${_err}\n"
    )
  endif()
endfunction()

function(rt_assimp_all_files_exist out_var base_dir)
  set(_ok TRUE)
  foreach(_f IN LISTS ARGN)
    if(NOT EXISTS "${base_dir}/${_f}")
      set(_ok FALSE)
      break()
    endif()
  endforeach()
  set(${out_var} ${_ok} PARENT_SCOPE)
endfunction()

function(rt_assimp_download_file url dst_path)
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
    message(FATAL_ERROR "Assimp: download failed (${_code}): ${url}\n  -> ${dst_path}\n  msg: ${_msg}")
  endif()
endfunction()

function(rt_assimp_find_single_file out_var root_dir filename)
  set(${out_var} "" PARENT_SCOPE)
  file(GLOB_RECURSE _hits LIST_DIRECTORIES FALSE "${root_dir}/**/${filename}")
  list(LENGTH _hits _n)
  if(_n GREATER 0)
    list(GET _hits 0 _first)
    set(${out_var} "${_first}" PARENT_SCOPE)
  endif()
endfunction()

function(rt_assimp_detect_extracted_root out_var extracted_dir)
  set(${out_var} "" PARENT_SCOPE)

  if(EXISTS "${extracted_dir}/CMakeLists.txt")
    set(${out_var} "${extracted_dir}" PARENT_SCOPE)
    return()
  endif()

  file(GLOB _roots LIST_DIRECTORIES true "${extracted_dir}/*")
  foreach(r IN LISTS _roots)
    if(EXISTS "${r}/CMakeLists.txt")
      set(${out_var} "${r}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
endfunction()

# -------------------- Headers ------------------------------------------------

function(rt_fetch_assimp_headers_if_missing)
  rt_assimp_all_files_exist(_have_headers "${RT_ASSIMP_INCLUDE_DIR}" ${RT_ASSIMP_HEADER_FILES})
  rt_assimp_generate_generated_headers_from_source()
  if(_have_headers)
    message(STATUS "Assimp: headers already present. Skipping.")
    return()
  endif()

  message(STATUS "Assimp: installing headers -> ${RT_ASSIMP_INCLUDE_DIR}")
  foreach(_rel IN LISTS RT_ASSIMP_HEADER_FILES)
    set(_dst "${RT_ASSIMP_INCLUDE_DIR}/${_rel}")
    if(EXISTS "${_dst}")
      continue()
    endif()

    set(_url "${ASSIMP_HEADERS_BASE_URL}/${_rel}")
    message(STATUS "Assimp: downloading header: ${_rel}")
    rt_assimp_download_file("${_url}" "${_dst}")
  endforeach()

  rt_assimp_all_files_exist(_have_headers2 "${RT_ASSIMP_INCLUDE_DIR}" ${RT_ASSIMP_HEADER_FILES})
  if(NOT _have_headers2)
    message(FATAL_ERROR "Assimp: after header install, some headers are still missing under: ${RT_ASSIMP_INCLUDE_DIR}")
  endif()
endfunction()

function(rt_assimp_file_nonempty out_ok path)
  set(${out_ok} FALSE PARENT_SCOPE)
  if(EXISTS "${path}")
    file(SIZE "${path}" _sz)
    if(_sz GREATER 16) # >16 bajtów = prawie na pewno nie pusty placeholder
      set(${out_ok} TRUE PARENT_SCOPE)
    endif()
  endif()
endfunction()

function(rt_assimp_pick_generated_header out_path build_dir leaf_name)
  # 1) kanoniczne ścieżki (najczęstsze)
  set(_candidates
    "${build_dir}/include/assimp/${leaf_name}"
    "${build_dir}/include/${leaf_name}"
    "${build_dir}/${leaf_name}"
  )

  foreach(p IN LISTS _candidates)
    rt_assimp_file_nonempty(_ok "${p}")
    if(_ok)
      set(${out_path} "${p}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  # 2) fallback: szukaj rekurencyjnie, ale wybierz NAJWIĘKSZY niepusty plik w ścieżce zawierającej "/assimp/"
  set(_best "")
  set(_best_sz -1)

  file(GLOB_RECURSE _hits LIST_DIRECTORIES FALSE "${build_dir}/**/${leaf_name}")
  foreach(h IN LISTS _hits)
    if(NOT h MATCHES "assimp")
      continue()
    endif()
    if(EXISTS "${h}")
      file(SIZE "${h}" _sz)
      if(_sz GREATER 16 AND _sz GREATER _best_sz)
        set(_best "${h}")
        set(_best_sz "${_sz}")
      endif()
    endif()
  endforeach()

  set(${out_path} "${_best}" PARENT_SCOPE)
endfunction()

function(rt_assimp_generate_generated_headers_from_source)
  set(_dst_cfg "${RT_ASSIMP_INCLUDE_DIR}/config.h")
  set(_dst_rev "${RT_ASSIMP_INCLUDE_DIR}/revision.h")

  # Jeśli istnieją i są niepuste -> nic nie robimy
  rt_assimp_file_nonempty(_cfg_ok "${_dst_cfg}")
  rt_assimp_file_nonempty(_rev_ok "${_dst_rev}")
  if(_cfg_ok AND _rev_ok)
    message(STATUS "Assimp: generated headers (config.h, revision.h) already present (non-empty). Skipping generation.")
    return()
  endif()

  if(NOT WIN32)
    message(FATAL_ERROR "Assimp generated headers via upstream CMake are currently set up for Windows only.")
  endif()

  # Download/extract assimp sources (tag zip)
  set(_sbase "${CMAKE_BINARY_DIR}/_deps/assimp_src_cfg")
  set(_szip  "${_sbase}/assimp-${ASSIMP_VERSION}-src.zip")
  set(_sextract "${_sbase}/src")

  file(MAKE_DIRECTORY "${_sbase}")

  if(NOT EXISTS "${_szip}")
    message(STATUS "Assimp: downloading source zip for generated headers: ${ASSIMP_SOURCE_ZIP_URL}")
    file(DOWNLOAD
      "${ASSIMP_SOURCE_ZIP_URL}"
      "${_szip}"
      SHOW_PROGRESS
      STATUS _dl_status
      TLS_VERIFY ON
    )
    list(GET _dl_status 0 _dl_code)
    list(GET _dl_status 1 _dl_msg)
    if(NOT _dl_code EQUAL 0)
      message(FATAL_ERROR "Assimp: source zip download failed (${_dl_code}): ${_dl_msg}")
    endif()
  else()
    message(STATUS "Assimp: source zip already downloaded: ${_szip}")
  endif()

  if(NOT IS_DIRECTORY "${_sextract}/.ok")
    message(STATUS "Assimp: extracting source zip -> ${_sextract}")
    file(REMOVE_RECURSE "${_sextract}")
    file(MAKE_DIRECTORY "${_sextract}")
    file(ARCHIVE_EXTRACT INPUT "${_szip}" DESTINATION "${_sextract}")
    file(MAKE_DIRECTORY "${_sextract}/.ok")
  endif()

  rt_assimp_detect_extracted_root(_assimp_root "${_sextract}")
  if(_assimp_root STREQUAL "")
    message(FATAL_ERROR "Assimp: could not find source root with CMakeLists.txt under: ${_sextract}")
  endif()
  message(STATUS "Assimp: detected source root for generated headers: ${_assimp_root}")

  # Configure only (no build needed)
  set(_build "${_assimp_root}/_BuildConfigOnly")
  file(MAKE_DIRECTORY "${_build}")

  set(_gen "${CMAKE_GENERATOR}")
  set(_plat "${CMAKE_GENERATOR_PLATFORM}")

  message(STATUS "Assimp: configuring (to generate config.h/revision.h) -> ${_build}")
  if(_plat)
    rt_run_checked(
      "${CMAKE_COMMAND}" -S "${_assimp_root}" -B "${_build}"
      -G "${_gen}" -A "${_plat}"
      -DASSIMP_BUILD_TESTS=OFF
      -DASSIMP_BUILD_ASSIMP_TOOLS=OFF
      -DASSIMP_BUILD_SAMPLES=OFF
      -DASSIMP_NO_EXPORT=ON
    )
  else()
    rt_run_checked(
      "${CMAKE_COMMAND}" -S "${_assimp_root}" -B "${_build}"
      -G "${_gen}"
      -DASSIMP_BUILD_TESTS=OFF
      -DASSIMP_BUILD_ASSIMP_TOOLS=OFF
      -DASSIMP_BUILD_SAMPLES=OFF
      -DASSIMP_NO_EXPORT=ON
    )
  endif()

  # Pick correct generated headers from build dir
  set(_gen_cfg "")
  set(_gen_rev "")

  rt_assimp_pick_generated_header(_gen_cfg "${_build}" "config.h")
  rt_assimp_pick_generated_header(_gen_rev "${_build}" "revision.h")

  if(_gen_cfg STREQUAL "")
    message(FATAL_ERROR "Assimp: configure finished but could not locate a non-empty generated config.h under: ${_build}")
  endif()
  if(_gen_rev STREQUAL "")
    message(FATAL_ERROR "Assimp: configure finished but could not locate a non-empty generated revision.h under: ${_build}")
  endif()

  file(MAKE_DIRECTORY "${RT_ASSIMP_INCLUDE_DIR}")

  message(STATUS "Assimp: installing generated config.h from: ${_gen_cfg}")
  file(COPY_FILE "${_gen_cfg}" "${_dst_cfg}" ONLY_IF_DIFFERENT)

  message(STATUS "Assimp: installing generated revision.h from: ${_gen_rev}")
  file(COPY_FILE "${_gen_rev}" "${_dst_rev}" ONLY_IF_DIFFERENT)

  # Final sanity: must be non-empty
  rt_assimp_file_nonempty(_cfg_ok2 "${_dst_cfg}")
  rt_assimp_file_nonempty(_rev_ok2 "${_dst_rev}")
  if(NOT _cfg_ok2 OR NOT _rev_ok2)
    message(FATAL_ERROR "Assimp: generated config.h/revision.h are still empty after copy. Check picked paths above.")
  endif()
endfunction()



# -------------------- Runtime (dll+lib) -------------------------------------

function(rt_fetch_assimp_runtime_if_missing)
  if(NOT WIN32)
    message(FATAL_ERROR "Assimp auto-fetch runtime implemented for Windows only.")
  endif()

  set(_rel_dll_marker "${RT_ASSIMP_DLL_REL_DIR}/${RT_ASSIMP_DLL_NAME}")
  set(_dbg_dll_marker "${RT_ASSIMP_DLL_DBG_DIR}/${RT_ASSIMP_DLL_NAME}")
  set(_lib_marker     "${RT_ASSIMP_LIBS_DIR}/${RT_ASSIMP_LIB_NAME}")

  if(EXISTS "${_rel_dll_marker}" AND EXISTS "${_dbg_dll_marker}" AND EXISTS "${_lib_marker}")
    message(STATUS "Assimp: runtime dll+lib already present. Skipping.")
    return()
  endif()

  set(_need_rel_dll FALSE)
  set(_need_dbg_dll FALSE)
  set(_need_lib FALSE)

  if(NOT EXISTS "${_rel_dll_marker}")
    set(_need_rel_dll TRUE)
  endif()
  if(NOT EXISTS "${_dbg_dll_marker}")
    set(_need_dbg_dll TRUE)
  endif()
  if(NOT EXISTS "${_lib_marker}")
    set(_need_lib TRUE)
  endif()

  # Pick runtime ZIP url
  set(_zip_url "")
  if(NOT ASSIMP_ZIP_URL STREQUAL "")
    set(_zip_url "${ASSIMP_ZIP_URL}")
  else()
    set(_arch "${ASSIMP_ARCH}")
    if(_arch STREQUAL "auto")
      if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_arch "x64")
      else()
        set(_arch "x86")
      endif()
    endif()

    if(_arch STREQUAL "x64")
      set(_zip_url "${ASSIMP_ZIP_URL_X64}")
    elseif(_arch STREQUAL "x86")
      set(_zip_url "${ASSIMP_ZIP_URL_X86}")
    else()
      message(FATAL_ERROR "Assimp: invalid ASSIMP_ARCH='${ASSIMP_ARCH}'. Use auto|x64|x86.")
    endif()
  endif()

  # Runtime zip workspace
  set(_base "${CMAKE_BINARY_DIR}/_deps/assimp")
  set(_zip  "${_base}/assimp-${ASSIMP_VERSION}-runtime.zip")
  set(_src  "${_base}/runtime_src")

  file(MAKE_DIRECTORY "${_base}")

  if(NOT EXISTS "${_zip}")
    message(STATUS "Assimp: downloading runtime zip: ${_zip_url}")
    file(DOWNLOAD
      "${_zip_url}"
      "${_zip}"
      SHOW_PROGRESS
      STATUS _dl_status
      TLS_VERIFY ON
    )
    list(GET _dl_status 0 _dl_code)
    list(GET _dl_status 1 _dl_msg)
    if(NOT _dl_code EQUAL 0)
      message(FATAL_ERROR "Assimp: runtime zip download failed (${_dl_code}): ${_dl_msg}")
    endif()
  else()
    message(STATUS "Assimp: runtime zip already downloaded: ${_zip}")
  endif()

  if(NOT IS_DIRECTORY "${_src}/Release")
    message(STATUS "Assimp: extracting runtime zip -> ${_src}")
    file(REMOVE_RECURSE "${_src}")
    file(MAKE_DIRECTORY "${_src}")
    file(ARCHIVE_EXTRACT INPUT "${_zip}" DESTINATION "${_src}")
  else()
    message(STATUS "Assimp: already extracted (found ${_src}/Release).")
  endif()

  set(_search_root "${_src}/Release")
  if(NOT IS_DIRECTORY "${_search_root}")
    message(FATAL_ERROR "Assimp: expected folder not found after extract: ${_search_root}")
  endif()

  set(_dll_path "")
  set(_lib_path "")

  rt_assimp_find_single_file(_dll_path "${_search_root}" "${RT_ASSIMP_DLL_NAME}")
  rt_assimp_find_single_file(_lib_path "${_search_root}" "${RT_ASSIMP_LIB_NAME}")

  # DLL fallback search in whole extracted runtime zip
  if((_need_rel_dll OR _need_dbg_dll) AND (_dll_path STREQUAL ""))
    rt_assimp_find_single_file(_dll_path "${_src}" "${RT_ASSIMP_DLL_NAME}")
  endif()

  if((_need_rel_dll OR _need_dbg_dll) AND (_dll_path STREQUAL ""))
    message(FATAL_ERROR "Assimp: could not find ${RT_ASSIMP_DLL_NAME} in extracted runtime zip: ${_src}")
  endif()

  # If .lib missing in runtime zip -> build from sources
  if(_need_lib AND (_lib_path STREQUAL ""))
    message(STATUS "Assimp: ${RT_ASSIMP_LIB_NAME} not present in runtime zip -> building from sources to obtain .lib")

    set(_sbase "${CMAKE_BINARY_DIR}/_deps/assimp_src")
    set(_szip  "${_sbase}/assimp-${ASSIMP_VERSION}-src.zip")
    set(_sextract "${_sbase}/src")

    file(MAKE_DIRECTORY "${_sbase}")

    if(NOT EXISTS "${_szip}")
      message(STATUS "Assimp: downloading source zip: ${ASSIMP_SOURCE_ZIP_URL}")
      file(DOWNLOAD
        "${ASSIMP_SOURCE_ZIP_URL}"
        "${_szip}"
        SHOW_PROGRESS
        STATUS _dl_status
        TLS_VERIFY ON
      )
      list(GET _dl_status 0 _dl_code)
      list(GET _dl_status 1 _dl_msg)
      if(NOT _dl_code EQUAL 0)
        message(FATAL_ERROR "Assimp: source zip download failed (${_dl_code}): ${_dl_msg}")
      endif()
    else()
      message(STATUS "Assimp: source zip already downloaded: ${_szip}")
    endif()

    if(NOT IS_DIRECTORY "${_sextract}/.ok")
      message(STATUS "Assimp: extracting source zip -> ${_sextract}")
      file(REMOVE_RECURSE "${_sextract}")
      file(MAKE_DIRECTORY "${_sextract}")
      file(ARCHIVE_EXTRACT INPUT "${_szip}" DESTINATION "${_sextract}")
      file(MAKE_DIRECTORY "${_sextract}/.ok")
    endif()

    rt_assimp_detect_extracted_root(_assimp_root "${_sextract}")
    if(_assimp_root STREQUAL "")
      message(FATAL_ERROR "Assimp: could not find source root with CMakeLists.txt under: ${_sextract}")
    endif()
    message(STATUS "Assimp: detected source root: ${_assimp_root}")

    set(_build "${_assimp_root}/_Build")
    file(MAKE_DIRECTORY "${_build}")

    set(_gen "${CMAKE_GENERATOR}")
    set(_plat "${CMAKE_GENERATOR_PLATFORM}")

    message(STATUS "Assimp: configuring source build -> ${_build}")
    if(_plat)
      rt_run_checked(
        "${CMAKE_COMMAND}" -S "${_assimp_root}" -B "${_build}"
        -G "${_gen}" -A "${_plat}"
        -DBUILD_SHARED_LIBS=ON
        -DASSIMP_BUILD_TESTS=OFF
        -DASSIMP_BUILD_ASSIMP_TOOLS=OFF
        -DASSIMP_BUILD_SAMPLES=OFF
        -DASSIMP_NO_EXPORT=ON
      )
    else()
      rt_run_checked(
        "${CMAKE_COMMAND}" -S "${_assimp_root}" -B "${_build}"
        -G "${_gen}"
        -DBUILD_SHARED_LIBS=ON
        -DASSIMP_BUILD_TESTS=OFF
        -DASSIMP_BUILD_ASSIMP_TOOLS=OFF
        -DASSIMP_BUILD_SAMPLES=OFF
        -DASSIMP_NO_EXPORT=ON
      )
    endif()

    message(STATUS "Assimp: building Release (to get .lib)")
    rt_run_checked("${CMAKE_COMMAND}" --build "${_build}" --config Release)

    set(_lib_path2 "")
    rt_assimp_find_single_file(_lib_path2 "${_build}" "${RT_ASSIMP_LIB_NAME}")
    if(_lib_path2 STREQUAL "")
      rt_assimp_find_single_file(_lib_path2 "${_assimp_root}" "${RT_ASSIMP_LIB_NAME}")
    endif()

    if(_lib_path2 STREQUAL "")
      message(FATAL_ERROR
        "Assimp: built from source but could not find ${RT_ASSIMP_LIB_NAME}.\n"
        "Most likely output name differs (often 'assimp.lib'). Update RT_ASSIMP_LIB_NAME or add mapping."
      )
    endif()

    set(_lib_path "${_lib_path2}")
  endif()

  # Install/copy to repo
  if(_need_rel_dll)
    file(MAKE_DIRECTORY "${RT_ASSIMP_DLL_REL_DIR}")
    message(STATUS "Assimp: installing Release DLL -> ${_rel_dll_marker}")
    file(COPY_FILE "${_dll_path}" "${_rel_dll_marker}" ONLY_IF_DIFFERENT)
  endif()

  if(_need_dbg_dll)
    file(MAKE_DIRECTORY "${RT_ASSIMP_DLL_DBG_DIR}")
    message(STATUS "Assimp: installing Debug DLL -> ${_dbg_dll_marker}")
    file(COPY_FILE "${_dll_path}" "${_dbg_dll_marker}" ONLY_IF_DIFFERENT)
  endif()

  if(_need_lib)
    if(_lib_path STREQUAL "")
      message(FATAL_ERROR "Assimp: internal error: need lib but _lib_path is empty.")
    endif()
    file(MAKE_DIRECTORY "${RT_ASSIMP_LIBS_DIR}")
    message(STATUS "Assimp: installing LIB -> ${_lib_marker}")
    file(COPY_FILE "${_lib_path}" "${_lib_marker}" ONLY_IF_DIFFERENT)
  endif()

  if(NOT EXISTS "${_rel_dll_marker}")
    message(FATAL_ERROR "Assimp: missing after install: ${_rel_dll_marker}")
  endif()
  if(NOT EXISTS "${_dbg_dll_marker}")
    message(FATAL_ERROR "Assimp: missing after install: ${_dbg_dll_marker}")
  endif()
  if(NOT EXISTS "${_lib_marker}")
    message(FATAL_ERROR "Assimp: missing after install: ${_lib_marker}")
  endif()

  message(STATUS "Assimp: runtime done.")
endfunction()

# -------------------- Entry --------------------------------------------------

function(rt_fetch_assimp_if_missing)
  if(NOT RT_FETCH_ASSIMP)
    message(STATUS "Assimp: auto-fetch disabled (RT_FETCH_ASSIMP=OFF)")
    return()
  endif()

  rt_assimp_all_files_exist(_have_headers "${RT_ASSIMP_INCLUDE_DIR}" ${RT_ASSIMP_HEADER_FILES})

  if(_have_headers AND
     EXISTS "${RT_ASSIMP_DLL_REL_DIR}/${RT_ASSIMP_DLL_NAME}" AND
     EXISTS "${RT_ASSIMP_DLL_DBG_DIR}/${RT_ASSIMP_DLL_NAME}" AND
     EXISTS "${RT_ASSIMP_LIBS_DIR}/${RT_ASSIMP_LIB_NAME}")
    message(STATUS "Assimp: all headers + runtime already present. Skipping everything.")
    return()
  endif()

  rt_fetch_assimp_headers_if_missing()
  rt_fetch_assimp_runtime_if_missing()
endfunction()

rt_fetch_assimp_if_missing()
