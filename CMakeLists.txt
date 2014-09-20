# 20140916 - cf-log project
cmake_minimum_required (VERSION 2.8)

project (cf-log)

# ### NOTE: *** UPDATE ME ***
set( cfl_MAJOR 1 )
set( cfl_MINOR 0 )
set( cfl_POINT 0 )

# Allow developer to select is Dynamic or static library built
# Presently there is no install of these libraries, so this option has NOT been tested
set( LIB_TYPE STATIC )  # set default static
option( BUILD_SHARED_LIB   "Set ON to build shared (dll) libraries"   OFF )
# Not really required, as aliternate matsh has been provided
option( USE_SIMGEAR_LIB    "Set ON to use SimGear core library."      OFF )
# Also not really required due  to alternate maths include, so NOT tested recently
option( USE_GEOGRAPHIC_LIB "Set ON to use Geographic library"         OFF )

# use some local cmake modules
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/CMake;${CMAKE_MODULE_PATH}")

add_definitions( -DVER=${cfl_MAJOR}${cfl_MINOR})
add_definitions( -DVERDOT="${cfl_MAJOR}.${cfl_MINOR}")

if (NOT SG_VERSION)
set( SG_VERSION "2.6") # FIXED: requiring 2.6+ should suffice here actually
endif ()

if(CMAKE_COMPILER_IS_GNUCXX)
    set( WARNING_FLAGS -Wall )
endif(CMAKE_COMPILER_IS_GNUCXX)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang") 
   set( WARNING_FLAGS "-Wall -Wno-overloaded-virtual" )
endif() 

if(WIN32 AND MSVC)
    set( CMAKE_DEBUG_POSTFIX "d" )
    # turn off various warnings
    set(WARNING_FLAGS "${WARNING_FLAGS} /wd4996")
    # foreach(warning 4244 4251 4267 4275 4290 4786 4305)
    #     set(WARNING_FLAGS "${WARNING_FLAGS} /wd${warning}")
    # endforeach(warning)
    set( MSVC_FLAGS "-DNOMINMAX -D_USE_MATH_DEFINES -D_CRT_SECURE_NO_WARNINGS -D_SCL_SECURE_NO_WARNINGS -D__CRT_NONSTDC_NO_WARNINGS" )
    # if (${MSVC_VERSION} EQUAL 1600)
    #    set( MSVC_LD_FLAGS "/FORCE:MULTIPLE" )
    # endif (${MSVC_VERSION} EQUAL 1600)
    # set( NOMINMAX 1 )
    list(APPEND add_LIBS ws2_32.lib)
endif()

set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${WARNING_FLAGS} ${MSVC_FLAGS} -D_REENTRANT" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${WARNING_FLAGS} ${MSVC_FLAGS} -D_REENTRANT" )
set( CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${MSVC_LD_FLAGS}" )

if (BUILD_SHARED_LIB)
    set(LIB_TYPE SHARED)
    message(STATUS "*** Building libraies as shared (DLL)")
else ()
    message(STATUS "*** Building libraies as static")
endif ()


if (USE_SIMGEAR_LIB)
    # SIMGEAR_CORE_LIBRARIES, a list of the core static libraries
    # SIMGEAR_LIBRARIES, a list of all the static libraries (core + scene)
    # SIMGEAR_FOUND, if false, do not try to link to SimGear
    # SIMGEAR_INCLUDE_DIR, where to find the headers
    # also finds
    #  ZLIB_INCLUDE_DIRS   - where to find zlib.h, etc.
    #  ZLIB_LIBRARIES      - List of libraries when using zlib.
    #  ZLIB_FOUND          - True if zlib found.
    # and threads
    #  CMAKE_THREAD_LIBS_INIT     - the thread library
    #  CMAKE_USE_SPROC_INIT       - are we using sproc?
    #  CMAKE_USE_WIN32_THREADS_INIT - using WIN32 threads?
    #  CMAKE_USE_PTHREADS_INIT    - are we using pthreads
    message(STATUS "*** Seeking SG VERSION greater than ${SG_VERSION}")
    find_package(SimGear ${SG_VERSION})
endif ()
if (SIMGEAR_FOUND)
    include_directories( ${SIMGEAR_INCLUDE_DIR} ${ZLIB_INCLUDE_DIRS} )
    message(STATUS "*** added include_directories( ${SIMGEAR_INCLUDE_DIR} ${ZLIB_INCLUDE_DIRS} )")
    list(APPEND add_LIBS ${SIMGEAR_LIBRARIES} ${ZLIB_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} )
    message(STATUS "*** added list(APPEND add_LIBS ${SIMGEAR_LIBRARIES} ${ZLIB_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} )" )
    add_definitions( -DUSE_SIMGEAR )
else ()
    if (USE_SIMGEAR_LIB)
        message(STATUS "*** Simgear NOT found!")
        set(USE_SIMGEAR_LIB NO)
    else ()
        message(STATUS "*** NOT using Simgear library")
    endif ()
endif ()

# configure_file( ${CMAKE_BINARY_DIR}/config-msvc.h ${CMAKE_BINARY_DIR}/config.h )
# add_definitions( -DHAVE_CONFIG_H )

##################################################################################
# CROSSFEED 'MISC' LIBRARY
set (dir src/cf_lib )
include_directories( ${dir} )
set (lib_SRCS
    ${dir}/sprtf.cxx 
    ${dir}/cf_misc.cxx
    )
set (lib_HDRS
    ${dir}/sprtf.hxx 
    ${dir}/cf_misc.hxx
    )
list(APPEND lib_SRCS
    ${dir}/netSocket.cxx
    ${dir}/mpKeyboard.cxx
    ${dir}/test_data.cxx
    )
list(APPEND lib_HDRS
    ${dir}/netSocket.h
    ${dir}/mpKeyboard.hxx
    ${dir}/test_data.hxx
    ${dir}/typcnvt.hxx
    ${dir}/mpMsgs.hxx
    ${dir}/tiny_xdr.hxx
    )
# if (UNIX) - include in all
    list (APPEND lib_SRCS ${dir}/daemon.cxx)
    list (APPEND lib_HDRS ${dir}/daemon.hxx)
# endif (UNIX)
if (WIN32)
    list(APPEND lib_SRCS ${dir}/win_strptime.cxx)
endif ()

if (USE_GEOGRAPHIC_LIB)
    list(APPEND lib_SRCS ${dir}/geod.cxx)
    list(APPEND lib_HDRS ${dir}/geod.hxx)
endif ()
if (NOT USE_SIMGEAR_LIB)
    list(APPEND lib_SRCS
        ${dir}/fg_geometry.cxx 
        ${dir}/cf_euler.cxx
        )
    list(APPEND lib_HDRS
        ${dir}/fg_geometry.hxx
        ${dir}/cf_euler.hxx
        ${dir}/SGMath2.hxx
        ) 
endif ()

add_library(cf_lib ${LIB_TYPE} ${lib_SRCS} ${lib_HDRS})
list(APPEND EXTRA_LIBS cf_lib)

if(UNIX AND NOT APPLE)
    list(APPEND EXTRA_LIBS rt)
endif()

###################################################################################
# 20140920: Add mongoose to do the http handling
set(name mongoose)
set(dir src/${name})
include_directories( ${dir} )
add_library( ${name} ${LIB_TYPE} ${dir}/${name}.c ${dir}/${name}.h ) 
list(APPEND EXTRA_LIBS ${name})

###################################################################################
# single EXE
set(dir src)
set(name cf-log)
set( ${name}_SRCS
    ${dir}/cf-log.cxx
    ${dir}/cf-pilot.cxx
    ${dir}/cf-server.cxx
    )
set( ${name}_HDRS
    ${dir}/cf-log.hxx
    ${dir}/cf-pilot.hxx
    ${dir}/cf-server.hxx
    )
add_executable( ${name} ${${name}_SRCS} ${${name}_HDRS} )
if (MSVC)
    set_target_properties( ${name} PROPERTIES DEBUG_POSTFIX d )
endif ()
target_link_libraries ( ${name} ${add_LIBS} ${EXTRA_LIBS} )

##########################################################
# NOTE: NO INSTALL PROVIDED FOR APP NOR LIBRARIES
##########################################################

# eof
