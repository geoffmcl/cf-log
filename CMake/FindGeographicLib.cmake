# Look for GeographicLib
#
# Set
#  GeographicLib_FOUND = GEOGRAPHICLIB_FOUND = TRUE
#  GeographicLib_INCLUDE_DIRS = /usr/local/include
#  GeographicLib_LIBRARIES = /usr/local/lib/libGeographic.so
#  GeographicLib_LIBRARY_DIRS = /usr/local/lib
# 20170311 - Separate DEBUG and RELEASE for MSVC

if (MSVC)
    #define GEOGRAPHICLIB_VERSION_STRING "1.47"
    find_path( GeographicLib_INCLUDE_DIRS GeographicLib/Config.h
        HINTS $ENV{GEOGRAPHICLIB_DIR}
        PATH_SUFFIXES include 
        )
    find_library (GeographicLib_LIB_DBG Geographic_d
        PATH_SUFFIXES lib
        PATHS "${CMAKE_INSTALL_PREFIX}"
        )
    find_library (GeographicLib_LIB_REL Geographic
        PATH_SUFFIXES lib
        PATHS "${CMAKE_INSTALL_PREFIX}"
        )
    if (GeographicLib_LIB_DBG AND GeographicLib_LIB_REL)
        set(GeographicLib_LIBRARIES
            optimized ${GeographicLib_LIB_REL}
            debug ${GeographicLib_LIB_DBG}
            )
    elseif(GeographicLib_LIB_REL)
        set(GeographicLib_LIBRARIES ${GeographicLib_LIB_REL})
    endif ()
    if (GeographicLib_LIBRARIES)
        get_filename_component (GeographicLib_LIBRARY_DIRS
            "${GeographicLib_LIB_REL}" PATH
        )
    endif ()
else ()
    find_library (GeographicLib_LIBRARIES Geographic
        PATHS "${CMAKE_INSTALL_PREFIX}/../GeographicLib/lib")
    if (GeographicLib_LIBRARIES)
      get_filename_component (GeographicLib_LIBRARY_DIRS
        "${GeographicLib_LIBRARIES}" PATH)
      get_filename_component (_ROOT_DIR "${GeographicLib_LIBRARY_DIRS}" PATH)
      set (GeographicLib_INCLUDE_DIRS "${_ROOT_DIR}/include")
      set (GeographicLib_BINARY_DIRS "${_ROOT_DIR}/bin")
      if (NOT EXISTS "${GeographicLib_INCLUDE_DIRS}/GeographicLib/Config.h")
        # On Debian systems the library is in e.g.,
        #   /usr/lib/x86_64-linux-gnu/libGeographic.so
        # so try stripping another element off _ROOT_DIR
        get_filename_component (_ROOT_DIR "${_ROOT_DIR}" PATH)
        set (GeographicLib_INCLUDE_DIRS "${_ROOT_DIR}/include")
        set (GeographicLib_BINARY_DIRS "${_ROOT_DIR}/bin")
        if (NOT EXISTS "${GeographicLib_INCLUDE_DIRS}/GeographicLib/Config.h")
          unset (GeographicLib_INCLUDE_DIRS)
          unset (GeographicLib_LIBRARIES)
          unset (GeographicLib_LIBRARY_DIRS)
          unset (GeographicLib_BINARY_DIRS)
        endif ()
      endif ()
      unset (_ROOT_DIR)
    endif ()
endif ()

include (FindPackageHandleStandardArgs)
find_package_handle_standard_args (GeographicLib DEFAULT_MSG
  GeographicLib_LIBRARY_DIRS GeographicLib_LIBRARIES GeographicLib_INCLUDE_DIRS)
mark_as_advanced (GeographicLib_LIBRARY_DIRS GeographicLib_LIBRARIES
  GeographicLib_INCLUDE_DIRS)

# eof
