# cmake/NRIRuntime.cmake

option(RT_FETCH_NRI "Build/download NRI.dll if missing" ON)

set(NRI_VERSION "177" CACHE STRING "NRI version (e.g. 177)")
set(NRI_ZIP_URL
  "https://github.com/NVIDIA-RTX/NRI/archive/refs/tags/v${NRI_VERSION}.zip"
  CACHE STRING "NRI source zip url"
)

set(RT_REPO_ROOT "${CMAKE_SOURCE_DIR}")
set(RT_DLLS_ROOT "${RT_REPO_ROOT}/RayTracing/Dlls")

set(RT_NRI_RELEASE_DST "${RT_DLLS_ROOT}/Release")
set(RT_NRI_DEBUG_DST   "${RT_DLLS_ROOT}/Debug")

set(RT_NRI_RELEASE_MARKER "${RT_NRI_RELEASE_DST}/NRI.dll")
set(RT_NRI_DEBUG_MARKER   "${RT_NRI_DEBUG_DST}/NRI.dll")

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
      "Command failed (rc=${_rc}): ${ARGN}\n\n"
      "STDOUT:\n${_out}\n\nSTDERR:\n${_err}\n"
    )
  endif()
endfunction()

function(rt_fetch_nri_if_missing)
  if(NOT RT_FETCH_NRI)
    message(STATUS "NRI: auto-fetch disabled (RT_FETCH_NRI=OFF)")
    return()
  endif()

  if(NOT WIN32)
    message(FATAL_ERROR "NRI auto-build currently implemented for Windows only.")
  endif()

  # sprawdzamy niezależnie
  set(_need_release FALSE)
  set(_need_debug   FALSE)
  if(NOT EXISTS "${RT_NRI_RELEASE_MARKER}")
    set(_need_release TRUE)
  endif()
  if(NOT EXISTS "${RT_NRI_DEBUG_MARKER}")
    set(_need_debug TRUE)
  endif()

  if(NOT (_need_release OR _need_debug))
    message(STATUS "NRI: NRI.dll already present in Release+Debug. Skipping.")
    return()
  endif()

  set(_base "${CMAKE_BINARY_DIR}/_deps/nri")
  set(_zip  "${_base}/nri-v${NRI_VERSION}.zip")
  set(_src  "${_base}/src")

  file(MAKE_DIRECTORY "${_base}")
  file(MAKE_DIRECTORY "${_src}")

  # download jeśli potrzebne
  if(NOT EXISTS "${_zip}")
    message(STATUS "NRI: downloading ${NRI_ZIP_URL} -> ${_zip}")
    file(DOWNLOAD
      "${NRI_ZIP_URL}"
      "${_zip}"
      SHOW_PROGRESS
      STATUS _dl_status
      TLS_VERIFY ON
    )
    list(GET _dl_status 0 _dl_code)
    list(GET _dl_status 1 _dl_msg)
    if(NOT _dl_code EQUAL 0)
      message(FATAL_ERROR "NRI: download failed (${_dl_code}): ${_dl_msg}")
    endif()
  else()
    message(STATUS "NRI: zip already downloaded: ${_zip}")
  endif()

  # extract jeśli brak
  if(NOT EXISTS "${_src}/CMakeLists.txt")
    message(STATUS "NRI: extracting ${_zip} -> ${_src}")
    file(ARCHIVE_EXTRACT INPUT "${_zip}" DESTINATION "${_src}")
  endif()

  # wykryj root (w ZIP jest NRI-177/)
  set(_nri_root "")
  if(EXISTS "${_src}/CMakeLists.txt")
    set(_nri_root "${_src}")
  else()
    file(GLOB _roots LIST_DIRECTORIES true "${_src}/*")
    foreach(r IN LISTS _roots)
      if(EXISTS "${r}/CMakeLists.txt")
        set(_nri_root "${r}")
        break()
      endif()
    endforeach()
  endif()

  if(_nri_root STREQUAL "")
    message(FATAL_ERROR "NRI: could not find extracted root containing CMakeLists.txt under: ${_src}")
  endif()

  message(STATUS "NRI: detected source root: ${_nri_root}")

  # <<< KLUCZ: budujemy jak ich baty: _Build w root repo
  set(_build "${_nri_root}/_Build")
  file(MAKE_DIRECTORY "${_build}")

  # konfiguracja (jak 1-Deploy.bat = cmake .. %*)
  set(_gen "${CMAKE_GENERATOR}")
  set(_plat "${CMAKE_GENERATOR_PLATFORM}")

  message(STATUS "NRI: configuring -> ${_build}")
  if(_plat)
    rt_run_checked(
      "${CMAKE_COMMAND}" -S "${_nri_root}" -B "${_build}"
      -G "${_gen}" -A "${_plat}"
      -DNRI_STATIC_LIBRARY=OFF
      -DBUILD_SHARED_LIBS=ON
      -DNRI_ENABLE_VK_SUPPORT=OFF
      -DNRI_ENABLE_NVAPI=OFF
      -DNRI_ENABLE_AMDAGS=OFF
      -DNRI_ENABLE_NVTX_SUPPORT=OFF
      -DNRI_ENABLE_AGILITY_SDK_SUPPORT=OFF
    )
  else()
    rt_run_checked(
      "${CMAKE_COMMAND}" -S "${_nri_root}" -B "${_build}"
      -G "${_gen}"
      -DNRI_STATIC_LIBRARY=OFF
      -DBUILD_SHARED_LIBS=ON
      -DNRI_ENABLE_VK_SUPPORT=OFF
      -DNRI_ENABLE_NVAPI=OFF
      -DNRI_ENABLE_AMDAGS=OFF
      -DNRI_ENABLE_NVTX_SUPPORT=OFF
      -DNRI_ENABLE_AGILITY_SDK_SUPPORT=OFF
    )
  endif()

  # build tylko brakujące
  if(_need_release)
    message(STATUS "NRI: building Release")
    rt_run_checked("${CMAKE_COMMAND}" --build "${_build}" --config Release)
  else()
    message(STATUS "NRI: Release already present. Skipping build.")
  endif()

  if(_need_debug)
    message(STATUS "NRI: building Debug")
    rt_run_checked("${CMAKE_COMMAND}" --build "${_build}" --config Debug)
  else()
    message(STATUS "NRI: Debug already present. Skipping build.")
  endif()

  # <<< KLUCZ: artefakty są w _Bin/<config>/NRI.dll (wg 3-PrepareSDK.bat)
  set(_rel_out "${_nri_root}/_Bin/Release/NRI.dll")
  set(_dbg_out "${_nri_root}/_Bin/Debug/NRI.dll")

  if(_need_release)
    if(NOT EXISTS "${_rel_out}")
      message(FATAL_ERROR "NRI: expected Release DLL not found: ${_rel_out}")
    endif()
    file(MAKE_DIRECTORY "${RT_NRI_RELEASE_DST}")
    message(STATUS "NRI: installing Release -> ${RT_NRI_RELEASE_DST}/NRI.dll")
    file(COPY_FILE "${_rel_out}" "${RT_NRI_RELEASE_DST}/NRI.dll" ONLY_IF_DIFFERENT)
  endif()

  if(_need_debug)
    if(NOT EXISTS "${_dbg_out}")
      message(FATAL_ERROR "NRI: expected Debug DLL not found: ${_dbg_out}")
    endif()
    file(MAKE_DIRECTORY "${RT_NRI_DEBUG_DST}")
    message(STATUS "NRI: installing Debug -> ${RT_NRI_DEBUG_DST}/NRI.dll")
    file(COPY_FILE "${_dbg_out}" "${RT_NRI_DEBUG_DST}/NRI.dll" ONLY_IF_DIFFERENT)
  endif()

  if(NOT EXISTS "${RT_NRI_RELEASE_MARKER}" AND _need_release)
    message(FATAL_ERROR "NRI: Release marker missing after install: ${RT_NRI_RELEASE_MARKER}")
  endif()
  if(NOT EXISTS "${RT_NRI_DEBUG_MARKER}" AND _need_debug)
    message(FATAL_ERROR "NRI: Debug marker missing after install: ${RT_NRI_DEBUG_MARKER}")
  endif()

  message(STATUS "NRI: done.")
endfunction()

rt_fetch_nri_if_missing()
