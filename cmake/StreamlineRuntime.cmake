option(RT_FETCH_STREAMLINE "Download Streamline SDK zip if required files are missing" ON)

set(STREAMLINE_VERSION "2.10.0" CACHE STRING "Streamline SDK version")
set(STREAMLINE_ZIP_URL
  "https://github.com/NVIDIA-RTX/Streamline/releases/download/v${STREAMLINE_VERSION}/streamline-sdk-v${STREAMLINE_VERSION}.zip"
  CACHE STRING "Streamline SDK zip URL"
)

# Repo root = miejsce gdzie jest główny CMakeLists.txt
set(RT_REPO_ROOT "${CMAKE_SOURCE_DIR}")

# -------------------- Destinations (Twoje repo) ------------------------------
set(RT_DLLS_ROOT "${RT_REPO_ROOT}/RayTracing/Dlls")

# Streamline destinations
set(RT_DLLS_RELEASE_DIR "${RT_DLLS_ROOT}/Release/streamline")
set(RT_DLLS_DEBUG_DIR   "${RT_DLLS_ROOT}/Debug/streamline")
set(RT_SL_INCLUDE_DIR   "${RT_REPO_ROOT}/RayTracing/Includes/streamline")
set(RT_LIBS_DIR         "${RT_REPO_ROOT}/RayTracing/Libs")
set(RT_SL_INTERPOSER_LIB "${RT_LIBS_DIR}/sl.interposer.lib")

# NGX destinations
set(RT_NGX_INCLUDE_DIR      "${RT_REPO_ROOT}/RayTracing/Includes/ngx")
set(RT_NGX_LIBS_DIR         "${RT_REPO_ROOT}/RayTracing/Libs/ngx")
set(RT_NGX_DLLS_RELEASE_DIR "${RT_DLLS_ROOT}/Release/ngx")
set(RT_NGX_DLLS_DEBUG_DIR   "${RT_DLLS_ROOT}/Debug/ngx")

# -------------------- Required file lists ------------------------------------

# Streamline DLLs (Twoja lista)
set(RT_STREAMLINE_DLL_FILES
  "nis.license.txt"
  "NvLowLatencyVk.dll"
  "nvngx_deepdvc.dll"
  "nvngx_dlss.dll"
  "nvngx_dlss.license.txt"
  "nvngx_dlssd.dll"
  "nvngx_dlssg.dll"
  "reflex.license.txt"
  "sl.common.dll"
  "sl.deepdvc.dll"
  "sl.directsr.dll"
  "sl.dlss.dll"
  "sl.dlss_d.dll"
  "sl.dlss_g.dll"
  "sl.interposer.dll"
  "sl.nis.dll"
  "sl.nvperf.dll"
  "sl.pcl.dll"
  "sl.reflex.dll"
)

set(RT_STREAMLINE_HEADER_FILES
  "sl.h"
  "sl_appidentity.h"
  "sl_consts.h"
  "sl_core_api.h"
  "sl_core_types.h"
  "sl_deepdvc.h"
  "sl_device_wrappers.h"
  "sl_directsr.h"
  "sl_dlss.h"
  "sl_dlss_d.h"
  "sl_dlss_g.h"
  "sl_helpers.h"
  "sl_helpers_vk.h"
  "sl_hooks.h"
  "sl_matrix_helpers.h"
  "sl_nis.h"
  "sl_nvperf.h"
  "sl_pcl.h"
  "sl_reflex.h"
  "sl_result.h"
  "sl_security.h"
  "sl_struct.h"
  "sl_template.h"
  "sl_version.h"
)

# NGX headers (z Twojego screena)
set(RT_NGX_HEADER_FILES
  "nvsdk_ngx.h"
  "nvsdk_ngx_defs.h"
  "nvsdk_ngx_defs_deepdvc.h"
  "nvsdk_ngx_defs_dlssd.h"
  "nvsdk_ngx_helpers.h"
  "nvsdk_ngx_helpers_deepdvc.h"
  "nvsdk_ngx_helpers_deepdvc_vk.h"
  "nvsdk_ngx_helpers_dlssd.h"
  "nvsdk_ngx_helpers_dlssd_vk.h"
  "nvsdk_ngx_helpers_vk.h"
  "nvsdk_ngx_params.h"
  "nvsdk_ngx_params_dlssd.h"
  "nvsdk_ngx_vk.h"
)

# NGX libs (z Twojego screena)
set(RT_NGX_LIB_FILES
  "nvsdk_ngx_d.lib"
  "nvsdk_ngx_d_dbg.lib"
)

# NGX runtime DLLs (jak na screenie; licencja ma zwykle .txt)
# Jeśli Explorer ukrywa ".txt", nadal plik na dysku będzie miał .txt.
set(RT_NGX_DLL_FILES
  "nvngx_deepdvc.dll"
  "nvngx_dlss.dll"
  "nvngx_dlss.license.txt"
  "nvngx_dlssd.dll"
  "nvngx_dlssg.dll"
)

# -------------------- Helpers -------------------------------------------------

function(rt_all_files_exist out_var base_dir)
  set(_ok TRUE)
  foreach(_f IN LISTS ARGN)
    if(NOT EXISTS "${base_dir}/${_f}")
      set(_ok FALSE)
      break()
    endif()
  endforeach()
  set(${out_var} ${_ok} PARENT_SCOPE)
endfunction()

# Copy exact list; special-case for nvngx_dlss.license (if vendor changes extension)
function(rt_copy_required_files src_dir dst_dir)
  file(MAKE_DIRECTORY "${dst_dir}")
  foreach(_f IN LISTS ARGN)
    set(_src_file "${src_dir}/${_f}")

    if(NOT EXISTS "${_src_file}")
      # fallback: nvngx_dlss.license (without .txt) if it exists
      if(_f STREQUAL "nvngx_dlss.license.txt" AND EXISTS "${src_dir}/nvngx_dlss.license")
        set(_src_file "${src_dir}/nvngx_dlss.license")
      else()
        message(FATAL_ERROR "Missing in SDK: ${_src_file}")
      endif()
    endif()

    file(COPY "${_src_file}" DESTINATION "${dst_dir}")
  endforeach()
endfunction()

# -------------------- Main ----------------------------------------------------

function(rt_fetch_streamline_if_missing)
  if(NOT RT_FETCH_STREAMLINE)
    message(STATUS "Auto-download disabled (RT_FETCH_STREAMLINE=OFF)")
    return()
  endif()

  # 1) Check if everything we need is already present (Streamline + NGX)
  rt_all_files_exist(_have_sl_rel "${RT_DLLS_RELEASE_DIR}" ${RT_STREAMLINE_DLL_FILES})
  rt_all_files_exist(_have_sl_dbg "${RT_DLLS_DEBUG_DIR}"   ${RT_STREAMLINE_DLL_FILES})
  rt_all_files_exist(_have_sl_inc "${RT_SL_INCLUDE_DIR}"   ${RT_STREAMLINE_HEADER_FILES})
  set(_have_sl_lib FALSE)
  if(EXISTS "${RT_SL_INTERPOSER_LIB}")
    set(_have_sl_lib TRUE)
  endif()

  rt_all_files_exist(_have_ngx_inc "${RT_NGX_INCLUDE_DIR}"      ${RT_NGX_HEADER_FILES})
  rt_all_files_exist(_have_ngx_lib "${RT_NGX_LIBS_DIR}"         ${RT_NGX_LIB_FILES})
  rt_all_files_exist(_have_ngx_rel "${RT_NGX_DLLS_RELEASE_DIR}" ${RT_NGX_DLL_FILES})
  rt_all_files_exist(_have_ngx_dbg "${RT_NGX_DLLS_DEBUG_DIR}"   ${RT_NGX_DLL_FILES})

  if(_have_sl_rel AND _have_sl_dbg AND _have_sl_inc AND _have_sl_lib AND
     _have_ngx_inc AND _have_ngx_lib AND _have_ngx_rel AND _have_ngx_dbg)
    message(STATUS "Streamline+NGX: all required files already present. Skipping download.")
    return()
  endif()

  if(NOT WIN32)
    message(FATAL_ERROR "This auto-download is currently implemented for Windows only.")
  endif()

  # 2) Download/Extract zip (only because something is missing)
  set(_dl_dir "${CMAKE_BINARY_DIR}/_deps/streamline")
  set(_zip    "${_dl_dir}/streamline-sdk-v${STREAMLINE_VERSION}.zip")
  set(_src    "${_dl_dir}/src")

  file(MAKE_DIRECTORY "${_dl_dir}")
  file(MAKE_DIRECTORY "${_src}")

  if(NOT EXISTS "${_zip}")
    message(STATUS "Downloading: ${STREAMLINE_ZIP_URL}")
    file(DOWNLOAD
      "${STREAMLINE_ZIP_URL}"
      "${_zip}"
      SHOW_PROGRESS
      STATUS _dl_status
      TLS_VERIFY ON
    )
    list(GET _dl_status 0 _dl_code)
    list(GET _dl_status 1 _dl_msg)
    if(NOT _dl_code EQUAL 0)
      message(FATAL_ERROR "Download failed (${_dl_code}): ${_dl_msg}")
    endif()
  else()
    message(STATUS "Zip already downloaded: ${_zip}")
  endif()

  # Extract only if not extracted yet (flat layout expected: ${_src}/bin/x64)
  if(NOT IS_DIRECTORY "${_src}/bin/x64")
    message(STATUS "Extracting: ${_zip}")
    file(ARCHIVE_EXTRACT INPUT "${_zip}" DESTINATION "${_src}")
  else()
    message(STATUS "Already extracted (found ${_src}/bin/x64).")
  endif()

  # 3) SDK root detection (your case: root == ${_src})
  set(_sdk_root "")
  if(IS_DIRECTORY "${_src}/bin/x64")
    set(_sdk_root "${_src}")
  endif()
  if(_sdk_root STREQUAL "")
    message(FATAL_ERROR "Could not find SDK root containing 'bin/x64'. Check: ${_src}")
  endif()
  message(STATUS "Detected SDK root: ${_sdk_root}")

  # 4) Source dirs inside zip
  set(_src_release "${_sdk_root}/bin/x64")
  set(_src_debug   "${_sdk_root}/bin/x64/development")

  set(_src_sl_include "${_sdk_root}/include")
  set(_src_sl_lib_dir "${_sdk_root}/lib/x64")
  set(_src_sl_interposer_lib "${_src_sl_lib_dir}/sl.interposer.lib")

  # NGX from external/ngx-sdk
  set(_src_ngx_include "${_sdk_root}/external/ngx-sdk/include")
  set(_src_ngx_lib_dir "${_sdk_root}/external/ngx-sdk/lib/Windows_x86_64")

  if(NOT IS_DIRECTORY "${_src_debug}")
    message(FATAL_ERROR "Missing expected dir: ${_src_debug}")
  endif()
  if(NOT IS_DIRECTORY "${_src_sl_include}")
    message(FATAL_ERROR "Missing expected dir: ${_src_sl_include}")
  endif()
  if(NOT EXISTS "${_src_sl_interposer_lib}")
    message(FATAL_ERROR "Missing expected lib: ${_src_sl_interposer_lib}")
  endif()

  if(NOT IS_DIRECTORY "${_src_ngx_include}")
    message(FATAL_ERROR "Missing expected NGX include dir: ${_src_ngx_include}")
  endif()
  if(NOT IS_DIRECTORY "${_src_ngx_lib_dir}")
    message(FATAL_ERROR "Missing expected NGX lib dir: ${_src_ngx_lib_dir}")
  endif()

  # 5) Copy Streamline (only if missing)
  if(NOT _have_sl_rel)
    message(STATUS "Installing Streamline Release DLLs -> ${RT_DLLS_RELEASE_DIR}")
    rt_copy_required_files("${_src_release}" "${RT_DLLS_RELEASE_DIR}" ${RT_STREAMLINE_DLL_FILES})
  endif()

  if(NOT _have_sl_dbg)
    message(STATUS "Installing Streamline Debug DLLs -> ${RT_DLLS_DEBUG_DIR}")
    rt_copy_required_files("${_src_debug}" "${RT_DLLS_DEBUG_DIR}" ${RT_STREAMLINE_DLL_FILES})
  endif()

  if(NOT _have_sl_inc)
    message(STATUS "Installing Streamline headers -> ${RT_SL_INCLUDE_DIR}")
    rt_copy_required_files("${_src_sl_include}" "${RT_SL_INCLUDE_DIR}" ${RT_STREAMLINE_HEADER_FILES})
  endif()

  if(NOT _have_sl_lib)
    message(STATUS "Installing sl.interposer.lib -> ${RT_LIBS_DIR}")
    file(MAKE_DIRECTORY "${RT_LIBS_DIR}")
    file(COPY "${_src_sl_interposer_lib}" DESTINATION "${RT_LIBS_DIR}")
  endif()

  # 6) Copy NGX headers + libs (only if missing)
  if(NOT _have_ngx_inc)
    message(STATUS "Installing NGX headers -> ${RT_NGX_INCLUDE_DIR}")
    rt_copy_required_files("${_src_ngx_include}" "${RT_NGX_INCLUDE_DIR}" ${RT_NGX_HEADER_FILES})
  endif()

  if(NOT _have_ngx_lib)
    message(STATUS "Installing NGX libs -> ${RT_NGX_LIBS_DIR}")
    rt_copy_required_files("${_src_ngx_lib_dir}" "${RT_NGX_LIBS_DIR}" ${RT_NGX_LIB_FILES})
  endif()

  # 7) Copy NGX runtime DLLs to ngx folders (from same bin/x64 + development)
  if(NOT _have_ngx_rel)
    message(STATUS "Installing NGX Release DLLs -> ${RT_NGX_DLLS_RELEASE_DIR}")
    rt_copy_required_files("${_src_release}" "${RT_NGX_DLLS_RELEASE_DIR}" ${RT_NGX_DLL_FILES})
  endif()

  if(NOT _have_ngx_dbg)
    message(STATUS "Installing NGX Debug DLLs -> ${RT_NGX_DLLS_DEBUG_DIR}")
    rt_copy_required_files("${_src_debug}" "${RT_NGX_DLLS_DEBUG_DIR}" ${RT_NGX_DLL_FILES})
  endif()

  # 8) Final sanity
  rt_all_files_exist(_have_sl_rel2 "${RT_DLLS_RELEASE_DIR}" ${RT_STREAMLINE_DLL_FILES})
  rt_all_files_exist(_have_sl_dbg2 "${RT_DLLS_DEBUG_DIR}"   ${RT_STREAMLINE_DLL_FILES})
  rt_all_files_exist(_have_sl_inc2 "${RT_SL_INCLUDE_DIR}"   ${RT_STREAMLINE_HEADER_FILES})

  rt_all_files_exist(_have_ngx_inc2 "${RT_NGX_INCLUDE_DIR}"      ${RT_NGX_HEADER_FILES})
  rt_all_files_exist(_have_ngx_lib2 "${RT_NGX_LIBS_DIR}"         ${RT_NGX_LIB_FILES})
  rt_all_files_exist(_have_ngx_rel2 "${RT_NGX_DLLS_RELEASE_DIR}" ${RT_NGX_DLL_FILES})
  rt_all_files_exist(_have_ngx_dbg2 "${RT_NGX_DLLS_DEBUG_DIR}"   ${RT_NGX_DLL_FILES})

  if(NOT (_have_sl_rel2 AND _have_sl_dbg2 AND _have_sl_inc2 AND EXISTS "${RT_SL_INTERPOSER_LIB}" AND
          _have_ngx_inc2 AND _have_ngx_lib2 AND _have_ngx_rel2 AND _have_ngx_dbg2))
    message(FATAL_ERROR "After install, some required Streamline/NGX files are still missing.")
  endif()

  message(STATUS "Streamline+NGX: done.")
endfunction()

rt_fetch_streamline_if_missing()
