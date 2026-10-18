# Accept either a shared FFmpeg SDK (include/lib/bin) or pkg-config packages.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND AND NOT FFmpeg_ROOT)
    pkg_check_modules(PC_FFMPEG QUIET IMPORTED_TARGET
        libavformat libavcodec libavutil libswscale libswresample libavfilter)
endif()
if(PC_FFMPEG_FOUND)
    add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
    target_link_libraries(FFmpeg::FFmpeg INTERFACE PkgConfig::PC_FFMPEG)
    set(FFmpeg_FOUND TRUE)
    return()
endif()
find_path(FFmpeg_INCLUDE_DIR libavformat/avformat.h
    HINTS "${FFmpeg_ROOT}" "$ENV{FFMPEG_ROOT}" PATH_SUFFIXES include)
set(_ffmpeg_required FFmpeg_INCLUDE_DIR)
set(_ffmpeg_libs)
foreach(_component avformat avcodec avfilter swresample swscale avutil)
    find_library(FFmpeg_${_component}_LIBRARY NAMES ${_component}
        HINTS "${FFmpeg_ROOT}" "$ENV{FFMPEG_ROOT}" PATH_SUFFIXES lib)
    list(APPEND _ffmpeg_required FFmpeg_${_component}_LIBRARY)
    list(APPEND _ffmpeg_libs "${FFmpeg_${_component}_LIBRARY}")
endforeach()
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFmpeg REQUIRED_VARS ${_ffmpeg_required})
if(FFmpeg_FOUND AND NOT TARGET FFmpeg::FFmpeg)
    add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
    set_target_properties(FFmpeg::FFmpeg PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${FFmpeg_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${_ffmpeg_libs}")
endif()
