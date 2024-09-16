#CMake file for non-mixer-xt

cmake_minimum_required(VERSION 3.5.1)
project (non-mixer-xt)

set (PACKAGE_VERSION "2.0.4")

set (CMAKE_SKIP_RULE_DEPENDENCY OFF)

add_definitions(-D'VERSION="${PACKAGE_VERSION}"')
add_definitions(-D'WEBSITE="https://github.com/Stazed/non-mixer-xt"')
add_definitions(-D'PACKAGE="non-mixer-xt"')
add_definitions(-D'PIXMAP_PATH="${CMAKE_INSTALL_PREFIX}/share/pixmaps"')
add_definitions(-D'DOCUMENT_PATH="${CMAKE_INSTALL_PREFIX}/share/doc"')
add_definitions(-D'NMXT_CONFIG_DIRECTORY="non-mixer-xt/"')

include(CheckCSourceCompiles)
include(CheckCXXCompilerFlag)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

check_cxx_compiler_flag("-msse2" SUPPORT_SSE)

option (BuildForDebug "Include gdb debugging support" OFF)
option (EnableOptimizations "Enable optimizations" ON)
option (EnableSSE "Enable SSE (default=automatic check)" ON)
option (EnableSSE2 "Enable SSE2 (default=automatic check)" ON)
option (NativeOptimizations "Enable native CPU optimizations" ON)
option (EnableNTK "Use NTK toolkit for build" OFF)
option (EnableLADSPASupport "Enable LADSPA plugin support" ON)
option (EnableLV2Support "Enable LV2 plugin support" ON)
option (EnableCLAPSupport "Enable CLAP plugin support" ON)
option (EnableVST2Support "Enable VST(2) plugin support" ON)
option (EnableVST3Support "Enable VST3 plugin support" ON)
option (EnablePangoCairo "Optional: Enable PangoCairo needed by some plugins" ON)


set(CMAKE_BUILD_TYPE "Release")

set (BuildOptions_SSE
    "-msse -mfpmath=sse"
    CACHE STRING "SSE compiler options"
)

set (BuildOptions_SSE2
    "-msse -msse2 -mfpmath=sse"
    CACHE STRING "SSE2 compiler options"
)

set (BuildOptions_Native
    "-march=native -mtune=native"
    CACHE STRING "Native compiler options"
)
set (BuildOptionsBasic
    "-std=c++17 -Wno-unused-parameter -Wno-unused-result -O3 -fomit-frame-pointer -ffast-math -pipe"
    CACHE STRING "basic X86 complier options"
)

set (BuildOptionsDebug
    "-std=c++17 -Wno-cast-function-type -O0 -g3 -ggdb -Wall -Wextra -Wpointer-arith"
#    "-std=c++17 -Wno-cast-function-type -Wno-unused-result -O3 -g3 -ggdb -Wall -Wextra -Wpointer-arith -fomit-frame-pointer -ffast-math -pipe -flto=auto -ffat-lto-objects -fexceptions -g -grecord-gcc-switches -pipe -Wall -Werror=format-security -Wp,-U_FORTIFY_SOURCE,-D_FORTIFY_SOURCE=3 -Wp,-D_GLIBCXX_ASSERTIONS -m64 -march=x86-64 -mtune=generic -fasynchronous-unwind-tables -fstack-clash-protection -fcf-protection -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer"
    CACHE STRING "Debug build flags"
)

find_package (PkgConfig REQUIRED)

#dependencies
find_package(X11 REQUIRED)

find_package(Threads REQUIRED)
if(THREADS_HAVE_PTHREAD_ARG)
  set_property(TARGET non-mixer-xt PROPERTY COMPILE_OPTIONS "-pthread")
  set_property(TARGET non-mixer-xt PROPERTY INTERFACE_COMPILE_OPTIONS "-pthread")
endif()

#Being proactive here. Some plugins (X42 X11 embedded) will unload the pangocairo
#library when the plugin is removed and then cannot find it on a second add causing
#a freeze or crash. So this will make the library available to any plugin that needs
#it. Though this should really be fixed by the plugin developer.
if (EnablePangoCairo)
    set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})
    find_package(PangoCairo REQUIRED)
endif (EnablePangoCairo)

pkg_check_modules(JACK REQUIRED jack>=0.115.6)
pkg_check_modules(LIBLO liblo>=0.26 REQUIRED)

if(EnableNTK)
    pkg_check_modules(NTK ntk REQUIRED)
    pkg_check_modules(NTK_IMAGES ntk_images REQUIRED)
    add_definitions(-D'BUILD_TYPE="Built with NTK Library"')
else(EnableNTK) #FLTK Build
    SET(FLTK_SKIP_OPENGL TRUE)
    find_library(FLTK_STATIC libfltk.a REQUIRED)
    find_library(FLTK_STATIC_IMAGES libfltk_images.a REQUIRED)
    find_package(FLTK REQUIRED)
    if(FLTK_FOUND)
        mark_as_advanced(FORCE FLTK_BASE_LIBRARY)
        mark_as_advanced(FORCE FLTK_CONFIG_SCRIPT)
        mark_as_advanced(FORCE FLTK_DIR)
        mark_as_advanced(FORCE FLTK_FLUID_EXECUTABLE)
        mark_as_advanced(FORCE FLTK_FORMS_LIBRARY)
        mark_as_advanced(FORCE FLTK_GL_LIBRARY)
        mark_as_advanced(FORCE FLTK_IMAGES_LIBRARY)
        mark_as_advanced(FORCE FLTK_INCLUDE_DIR)
        mark_as_advanced(FORCE FLTK_MATH_LIBRARY)
        add_definitions(-D'FLTK_SUPPORT=1')
    endif(FLTK_FOUND)
    pkg_check_modules(XFIXES REQUIRED xfixes)
    pkg_check_modules(XINERAMA REQUIRED xinerama)
    pkg_check_modules(FONTCONFIG REQUIRED fontconfig>=0.22)
    pkg_check_modules(XFT REQUIRED xft)
    pkg_check_modules(XRENDER REQUIRED xrender)
    add_definitions(-D'BUILD_TYPE="Built with FLTK static library"')
endif(EnableNTK)

if (EnableLADSPASupport)
    pkg_check_modules(LRDF REQUIRED lrdf>=0.4.0)

    check_include_file (ladspa.h HAVE_LADSPA_H)
    if (NOT HAVE_LADSPA_H)
        message(FATAL_ERROR "Ladspa required but not found")
    endif ()

    if (HAVE_LADSPA_H)
        add_definitions(-D'LADSPA_SUPPORT=1')
    endif()
endif (EnableLADSPASupport)

if (EnableLV2Support)
    pkg_check_modules(LV2 REQUIRED lv2>=1.10.0)
    pkg_check_modules(LILV REQUIRED lilv-0>=0.20.0)
    pkg_check_modules(SUIL REQUIRED suil-0>=0.10.0)
    pkg_check_modules(ZIX zix-0 REQUIRED)

    if (LV2_FOUND)
        add_definitions(-D'LV2_SUPPORT=1')
    endif(LV2_FOUND)
endif (EnableLV2Support)

if (EnableCLAPSupport)
    pkg_check_modules(CLAP clap REQUIRED)
    if (CLAP_FOUND)
        add_definitions(-D'CLAP_SUPPORT=1')
    endif (CLAP_FOUND)
endif (EnableCLAPSupport)

if (EnableVST2Support)
    add_definitions(-D'VST2_SUPPORT=1')
endif (EnableVST2Support)

if (EnableVST3Support)
    add_definitions(-D'VST3_SUPPORT=1')
endif (EnableVST3Support)

include(CheckSymbolExists)

set(CMAKE_REQUIRED_LIBRARIES "jack")
check_symbol_exists(jack_get_property "jack/metadata.h" JACK_METADATA)
if(JACK_METADATA)
    add_definitions(-D'HAVE_JACK_METADATA=1')
    message(STATUS "Found jack metadata")
else(JACK_METADATA)
    message(STATUS "Jack metadata not found")
endif(JACK_METADATA)

check_symbol_exists(jack_port_get_latency_range "jack/jack.h" JACK_LATENCY_RANGE)
if(JACK_LATENCY_RANGE)
    add_definitions(-D'HAVE_JACK_PORT_GET_LATENCY_RANGE=1')
    message(STATUS "Found jack_port_get_latency_range")
else(JACK_LATENCY_RANGE)
    message(STATUS "jack_port_get_latency_range not found")
endif(JACK_LATENCY_RANGE)

CHECK_CXX_SOURCE_COMPILES(
    "int main ( char**argv, int argc ) {
    const char *s = (const char*)
    __builtin_assume_aligned( 0, 16 );
    return 0; }" HAVE_BUILTIN_ALIGNED)

if(HAVE_BUILTIN_ALIGNED)
    add_definitions(-D'HAS_BUILTIN_ASSUME_ALIGNED=1')
endif(HAVE_BUILTIN_ALIGNED)

check_symbol_exists(mlock "sys/mman.h" MLOCK)
if(MLOCK)
    add_definitions(-D'HAVE_MLOCK=1')
else (MLOCK)
    message(STATUS "mlock not found")
endif (MLOCK)

find_library (LIBDL dl)
if (LIBDL)
    message(STATUS "Found dl in ${LIBDL}")
else (LIBDL)
    message(FATAL_ERROR "dl required but not found")
endif (LIBDL)

find_library (LIBM m)
if (LIBM)
    message(STATUS "Found m in ${LIBM}")
else (LIBM)
    message(FATAL_ERROR "m required but not found")
endif (LIBM)

# Check for VST3 SDK.
if (EnableVST3Support AND NOT CONFIG_VST3SDK)
    set (CONFIG_VST3SDK ${CMAKE_CURRENT_SOURCE_DIR}/mixer/src/vst3/vst3sdk)
    message(STATUS "CONFIG_VST3SDK = ${CONFIG_VST3SDK}")
endif ()

# uninstall target
if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        IMMEDIATE @ONLY)

    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()

mark_as_advanced (CMAKE_BUILD_TYPE)
mark_as_advanced (CMAKE_INSTALL_PREFIX)
mark_as_advanced (LIBDL)
mark_as_advanced (LIBM)

if (BuildForDebug)
    set (CMAKE_BUILD_TYPE "Debug")
    set (CMAKE_CXX_FLAGS_DEBUG ${BuildOptionsDebug})
else (BuildForDebug)
    set (CMAKE_BUILD_TYPE "Release")

    set (CMAKE_CXX_FLAGS_RELEASE ${BuildOptionsBasic})
    add_definitions(-D'NDEBUG=1')
    if(EnableOptimizations)
        if (SUPPORT_SSE)
            if(EnableSSE)
                if(EnableSSE2)
                    set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${BuildOptions_SSE2}")
                    set (USE_SSE2 "${SUPPORT_SSE}")
                    set (USE_SSE "${SUPPORT_SSE}") #uses both
                else(EnableSSE2)
                    set (USE_SSE "${SUPPORT_SSE}")
                    set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${BuildOptions_SSE}")
                endif(EnableSSE2)
            endif(EnableSSE)
        endif (SUPPORT_SSE)

        if(BuildOptions_Native)
            set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${BuildOptions_Native}")
            set (USE_NATIVE "")
        endif(BuildOptions_Native)

    else(EnableOptimizations)
        set (USE_SSE2 "")
        set (USE_SSE "")
    endif(EnableOptimizations)
endif (BuildForDebug)

add_definitions(-D'BUILD_TYPE_CMAKE="${CMAKE_BUILD_TYPE}"')

add_subdirectory(mixer)
add_subdirectory(mixer/icons)
add_subdirectory(mixer/doc)
add_subdirectory(mixer/pixmaps)


##Summarize The Full Configuration
message(STATUS)
message(STATUS "=========================================================")
message(STATUS "Build Configuration for non-mixer-xt-${PACKAGE_VERSION} for ${CMAKE_SYSTEM_NAME}")
message(STATUS "=========================================================")
message(STATUS)

macro(package_status foundvar pkg)
    if(${foundvar})
        message(STATUS "${pkg} Yes")
    else()
        message(STATUS "${pkg} No")
    endif()
endmacro()

message (STATUS                    "___________________ Optimizations________________________"  )
package_status(USE_SSE             "Use sse. . . . . . . . . . . . . . . . . . . . . . . . .:"  )
package_status(USE_SSE2            "Use sse2 . . . . . . . . . . . . . . . . . . . . . . . .:"  )

message (STATUS)
message (STATUS                    "______________________Library____________________________"  )
package_status(PKG_CONFIG_FOUND    "PkgConfig. . . . . . . . . . . . . . . . . . . . . . . .:"  )
package_status(FLTK_FOUND          "FLTK toolkit . . . . . . . . . . . . . . . . . . . . . .:"  )
package_status(NTK_FOUND           "NTK toolkit. . . . . . . . . . . . . . . . . . . . . . .:"  )
package_status(X11_FOUND           "X11 support. . . . . . . . . . . . . . . . . . . . . . .:"  )
package_status(Threads_FOUND       "pthread support (libpthread) . . . . . . . . . . . . . .:"  )
package_status(MLOCK               "mlock support. . . . . . . . . . . . . . . . . . . . . .:"  )
package_status(LIBM                "libm support . . . . . . . . . . . . . . . . . . . . . .:"  )
package_status(LIBDL               "libdl support. . . . . . . . . . . . . . . . . . . . . .:"  )
package_status(ZIX_FOUND           "zix-0 support. . . . . . . . . . . . . . . . . . . . . .:"  )
package_status(JACK_FOUND          "JACK Audio Connection Kit library. . . . . . . . . . . .:"  )
package_status(FONTCONFIG_FOUND    "Fontconfig support . . . . . . . . . . . . . . . . . . .:"  )

if (EnableLADSPASupport)
    package_status(HAVE_LADSPA_H   "LADSPA support . . . . . . . . . . . . . . . . . . . . .:"  )
endif (EnableLADSPASupport)

if (EnableLV2Support)
    package_status(LV2_FOUND       "LV2 support. . . . . . . . . . . . . . . . . . . . . . .:"  )
endif (EnableLV2Support)

if (EnableCLAPSupport)
    package_status(CLAP_FOUND      "CLAP support . . . . . . . . . . . . . . . . . . . . . .:"  )
endif (EnableCLAPSupport)

if (EnableVST2Support)
    package_status(CONFIG_VST3SDK  "VST2 support . . . . . . . . . . . . . . . . . . . . . .:"  )
endif (EnableVST2Support)

if (EnableVST3Support)
    package_status(CONFIG_VST3SDK  "VST3 support . . . . . . . . . . . . . . . . . . . . . .:"  )
endif (EnableVST3Support)

package_status(LILV_FOUND          "LILV support . . . . . . . . . . . . . . . . . . . . . .:"  )
package_status(SUIL_FOUND          "SUIL support . . . . . . . . . . . . . . . . . . . . . .:"  )
package_status(LIBLO_FOUND         "Liblo support. . . . . . . . . . . . . . . . . . . . . .:"  )
package_status(JACK_METADATA       "Jack metadata support. . . . . . . . . . . . . . . . . .:"  )
package_status(JACK_LATENCY_RANGE  "Jack port latency range support. . . . . . . . . . . . .:"  )
package_status(HAVE_BUILTIN_ALIGNED "Has builtin assume aligned . . . . . . . . . . . . . . .:"  )

if (EnablePangoCairo)
    package_status(PangoCairo_FOUND    "PangoCairo support . . . . . . . . . . . . . . . . . . .:" )
endif(EnablePangoCairo)

message (STATUS)
message (STATUS                    "____________________ User Options________________________"  )
package_status(EnableLADSPASupport "Build LADSPA support . . . . . . . . . . . . . . . . . .:"  )
package_status(EnableLV2Support    "Build LV2 support. . . . . . . . . . . . . . . . . . . .:"  )
package_status(EnableCLAPSupport   "Build CLAP support . . . . . . . . . . . . . . . . . . .:"  )
package_status(EnableVST2Support   "Build VST2 support . . . . . . . . . . . . . . . . . . .:"  )
package_status(EnableVST3Support   "Build VST3 support . . . . . . . . . . . . . . . . . . .:"  )
package_status(EnableNTK           "Use NTK for build. . . . . . . . . . . . . . . . . . . .:"  )
package_status(EnablePangoCairo    "Enable PangoCairo support. . . . . . . . . . . . . . . .:"  )
package_status(EnableOptimizations "Use optimizations. . . . . . . . . . . . . . . . . . . .:"  )
package_status(EnableSSE           "Use sse. . . . . . . . . . . . . . . . . . . . . . . . .:"  )
package_status(EnableSSE2          "Use sse2 . . . . . . . . . . . . . . . . . . . . . . . .:"  )
package_status(NativeOptimizations "Native optimizations . . . . . . . . . . . . . . . . . .:"  )
package_status(BuildForDebug       "Build for debug. . . . . . . . . . . . . . . . . . . . .:"  )


message (STATUS)
message (STATUS)
message (STATUS "Building for ${CMAKE_BUILD_TYPE}")
if (BuildForDebug)
    message (STATUS "Flags: ${CMAKE_CXX_FLAGS_DEBUG}")
else (BuildForDebug)
    message (STATUS "Flags: ${CMAKE_CXX_FLAGS_RELEASE}")
endif (BuildForDebug)
