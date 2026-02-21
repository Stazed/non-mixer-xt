#CMake file for Non-mixer-xt

project (non-mixer-xt)

set(NONLIB_SOURCES
    ${CMAKE_SOURCE_DIR}/nonlib/JACK/Client.C
    ${CMAKE_SOURCE_DIR}/nonlib/JACK/Port.C
    ${CMAKE_SOURCE_DIR}/nonlib/Log_Entry.C
    ${CMAKE_SOURCE_DIR}/nonlib/Loggable.C
    ${CMAKE_SOURCE_DIR}/nonlib/NSM/Client.C
    ${CMAKE_SOURCE_DIR}/nonlib/OSC/Endpoint.C
    ${CMAKE_SOURCE_DIR}/nonlib/Thread.C
    ${CMAKE_SOURCE_DIR}/nonlib/debug.C
    ${CMAKE_SOURCE_DIR}/nonlib/dsp.C
    ${CMAKE_SOURCE_DIR}/nonlib/file.C
    ${CMAKE_SOURCE_DIR}/nonlib/string_util.C

    ${CMAKE_SOURCE_DIR}/nonlib/MIDI/event.C
    ${CMAKE_SOURCE_DIR}/nonlib/MIDI/event_list.C
    ${CMAKE_SOURCE_DIR}/nonlib/MIDI/midievent.C
)

set(FL_SOURCES
    ${CMAKE_SOURCE_DIR}/FL/About_Dialog.C
    ${CMAKE_SOURCE_DIR}/FL/Fl_Menu_Settings.C
    ${CMAKE_SOURCE_DIR}/FL/Fl_Scalepack.C
    ${CMAKE_SOURCE_DIR}/FL/Fl_Text_Edit_Window.C
    ${CMAKE_SOURCE_DIR}/FL/Fl_Value_SliderX.C
    ${CMAKE_SOURCE_DIR}/FL/Fl_SliderX.C
    ${CMAKE_SOURCE_DIR}/FL/New_Project_Dialog.C
    ${CMAKE_SOURCE_DIR}/FL/event_name.C
    ${CMAKE_SOURCE_DIR}/FL/menu_popup.C
    ${CMAKE_SOURCE_DIR}/FL/test_press.C
    ${CMAKE_SOURCE_DIR}/FL/focus_frame.C
)

set(PLUGIN_SOURCES
    # LADSPA
    ${CMAKE_SOURCE_DIR}/mixer/src/ladspa/LADSPAInfo.C
    ${CMAKE_SOURCE_DIR}/mixer/src/ladspa/LADSPA_Plugin.C

    # LV2
    ${CMAKE_SOURCE_DIR}/mixer/src/lv2/LV2_Plugin.C
    ${CMAKE_SOURCE_DIR}/mixer/src/lv2/lv2_evbuf.c

    # CLAP
    ${CMAKE_SOURCE_DIR}/mixer/src/clap/CLAP_Plugin.C
    ${CMAKE_SOURCE_DIR}/mixer/src/clap/Clap_Discovery.C
    ${CMAKE_SOURCE_DIR}/mixer/src/clap/PresetIndexer.cpp
    ${CMAKE_SOURCE_DIR}/mixer/src/clap/PresetMetadataReceiver.cpp
    ${CMAKE_SOURCE_DIR}/mixer/src/clap/Time.cpp

    # VST2
    ${CMAKE_SOURCE_DIR}/mixer/src/vst2/Vst2_Discovery.C
    ${CMAKE_SOURCE_DIR}/mixer/src/vst2/VST2_Plugin.C
    ${CMAKE_SOURCE_DIR}/mixer/src/vst2/VST2_Preset.C

    # VST3
    ${CMAKE_SOURCE_DIR}/mixer/src/vst3/VST3_common.C
    ${CMAKE_SOURCE_DIR}/mixer/src/vst3/Vst3_Discovery.C
    ${CMAKE_SOURCE_DIR}/mixer/src/vst3/VST3_Plugin.C
    ${CMAKE_SOURCE_DIR}/mixer/src/vst3/VST3PluginHost.C
    ${CMAKE_SOURCE_DIR}/mixer/src/vst3/runloop.cpp
    ${CMAKE_SOURCE_DIR}/mixer/src/vst3/EditorFrame.C

    # Platform-specific plugin UI
    ${CMAKE_SOURCE_DIR}/mixer/src/x11/X11PluginUI.C
)

set(CORE_SOURCES
    # Generic plugin infrastructure
    ${CMAKE_SOURCE_DIR}/mixer/src/Plugin_Chooser.C
    ${CMAKE_SOURCE_DIR}/mixer/src/Plugin_Chooser_UI.C
    ${CMAKE_SOURCE_DIR}/mixer/src/Plugin_Module.C
    ${CMAKE_SOURCE_DIR}/mixer/src/Scanner_Window.C

    # Engine / processing
    ${CMAKE_SOURCE_DIR}/mixer/src/Chain.C
    ${CMAKE_SOURCE_DIR}/mixer/src/Controller_Module.C
    ${CMAKE_SOURCE_DIR}/mixer/src/DPM.C
    ${CMAKE_SOURCE_DIR}/mixer/src/Gain_Module.C
    ${CMAKE_SOURCE_DIR}/mixer/src/Spatializer_Module.C
    ${CMAKE_SOURCE_DIR}/mixer/src/JACK_Module.C
    ${CMAKE_SOURCE_DIR}/mixer/src/AUX_Module.C

    # Mixing / metering
    ${CMAKE_SOURCE_DIR}/mixer/src/Meter_Indicator_Module.C
    ${CMAKE_SOURCE_DIR}/mixer/src/Meter_Module.C
    ${CMAKE_SOURCE_DIR}/mixer/src/Mixer.C
    ${CMAKE_SOURCE_DIR}/mixer/src/Mixer_Strip.C
    ${CMAKE_SOURCE_DIR}/mixer/src/Module.C
    ${CMAKE_SOURCE_DIR}/mixer/src/Module_Parameter_Editor.C
    ${CMAKE_SOURCE_DIR}/mixer/src/Mono_Pan_Module.C
    ${CMAKE_SOURCE_DIR}/mixer/src/Panner.C

    # Project / UI / misc
    ${CMAKE_SOURCE_DIR}/mixer/src/Project.C
    ${CMAKE_SOURCE_DIR}/mixer/src/Group.C
    ${CMAKE_SOURCE_DIR}/mixer/src/SpectrumView.C
    ${CMAKE_SOURCE_DIR}/mixer/src/Spatialization_Console.C
    ${CMAKE_SOURCE_DIR}/mixer/src/NSM.C
)

set(ProgSources
    ${NONLIB_SOURCES}
    ${FL_SOURCES}
    ${CORE_SOURCES}
    ${PLUGIN_SOURCES}
)

if(EnableNTK)
    message(STATUS "USING NTK BUILD")
else(EnableNTK)
    message(STATUS "USING FLTK BUILD")
    set(FLTK_specific
        ${CMAKE_SOURCE_DIR}/FL/Fl_Tooltip.cxx
        ${CMAKE_SOURCE_DIR}/FL/Fl_Panzoomer.C
        ${CMAKE_SOURCE_DIR}/FL/Fl_Theme_Chooser.cxx
        ${CMAKE_SOURCE_DIR}/FL/Fl_Theme.cxx
        ${CMAKE_SOURCE_DIR}/FL/GTK_Theme.cxx
        ${CMAKE_SOURCE_DIR}/FL/Clean_Theme.cxx
        ${CMAKE_SOURCE_DIR}/FL/Crystal_Theme.cxx
        ${CMAKE_SOURCE_DIR}/FL/Gleam_Theme.cxx
        ${CMAKE_SOURCE_DIR}/FL/Vector_Theme.cxx
        ${CMAKE_SOURCE_DIR}/FL/themes.cxx
    )
endif(EnableNTK)

if (EnableVST3Support)
    if (CONFIG_VST3SDK)
      add_definitions(-DRELEASE)
      set (VST3SDK_SOURCES
        ${CONFIG_VST3SDK}/base/source/baseiids.cpp
        ${CONFIG_VST3SDK}/base/source/fobject.cpp
        ${CONFIG_VST3SDK}/base/thread/source/flock.cpp
        ${CONFIG_VST3SDK}/public.sdk/source/common/commoniids.cpp
        ${CONFIG_VST3SDK}/public.sdk/source/vst/vstinitiids.cpp
        ${CONFIG_VST3SDK}/pluginterfaces/base/coreiids.cpp
        ${CONFIG_VST3SDK}/pluginterfaces/base/funknown.cpp
      )
    endif (CONFIG_VST3SDK)
endif (EnableVST3Support)

add_executable (non-mixer-xt
    ${ProgSources}
    ${FLTK_specific}
    ${VST3SDK_SOURCES}
    ${CMAKE_SOURCE_DIR}/mixer/src/main.C)

set(NTK_AND_LEGACY_LIBS
    ${FONTCONFIG_LIBRARIES}
    ${JACK_LINK_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    ${X11_LIBRARIES}
    ${LIBLO_LIBRARIES}
    ${LILV_LIBRARIES}
    ${SUIL_LIBRARIES}
    ${ZIX_LIBRARIES}
    ${PangoCairo_LIBRARIES}
    ${XFT_LIBRARIES}
    ${XRENDER_LIBRARIES}
    ${XINERAMA_LIBRARIES}
    ${JPEG_LIBRARY}
    ${LIBPNG_LIBRARIES}
    ${XCURSOR_LIBRARIES}
    ${XFIXES_LIBRARIES}
    ${DL_LIBRARY}
    ${M_LIBRARY}
    ${LRDF_LIBRARIES}
)

set(NTK_AND_LEGACY_DIRS
    ${FONTCONFIG_INCLUDE_DIRS}
    ${JACK_INCLUDE_DIRS}
    ${LRDF_INCLUDE_DIRS}
    ${LILV_INCLUDE_DIRS}
    ${SUIL_INCLUDE_DIRS}
    ${LIBLO_INCLUDE_DIRS}
    ${ZIX_INCLUDE_DIRS}
    ${CLAP_INCLUDEDIR}
    ${PangoCairo_INCLUDE_DIRS}
    ${XFT_INCLUDE_DIRS}
    ${XRENDER_INCLUDE_DIRS}
    ${XINERAMA_INCLUDE_DIRS}
)

if(EnableNTK)
    set(ExternLibraries
        ${NTK_AND_LEGACY_LIBS}
        ${CAIRO_LIBRARIES}
    )

    target_include_directories (
        non-mixer-xt PRIVATE
        ${NTK_INCLUDE_DIRS}
        ${NTK_AND_LEGACY_DIRS}
    )

else(EnableNTK) #FLTK Build
    if(TARGET fltk::fltk)    # Modern cmake
        set(ExternLibraries
            ${JACK_LINK_LIBRARIES}
            ${CMAKE_THREAD_LIBS_INIT}
            ${LIBLO_LIBRARIES}
            ${LILV_LIBRARIES}
            ${SUIL_LIBRARIES}
            ${ZIX_LIBRARIES}
            ${PangoCairo_LIBRARIES}
            ${DL_LIBRARY}
            ${M_LIBRARY}
            ${LRDF_LIBRARIES}
        )
        target_include_directories (
            non-mixer-xt PRIVATE
            ${JACK_INCLUDE_DIRS}
            ${LRDF_INCLUDE_DIRS}
            ${LILV_INCLUDE_DIRS}
            ${SUIL_INCLUDE_DIRS}
            ${LIBLO_INCLUDE_DIRS}
            ${ZIX_INCLUDE_DIRS}
            ${CLAP_INCLUDEDIR}
            ${PangoCairo_INCLUDE_DIRS}
        )
    else()                          # Legacy cmake
        set(ExternLibraries
            ${NTK_AND_LEGACY_LIBS}
        )
        target_include_directories (
            non-mixer-xt PRIVATE
            ${NTK_AND_LEGACY_DIRS}
        )
    endif()
endif(EnableNTK)

if (EnableVST3Support AND CONFIG_VST3SDK)
    target_include_directories (non-mixer-xt PRIVATE ${CONFIG_VST3SDK})
endif()

if(EnableNTK)
    target_link_libraries (non-mixer-xt PRIVATE ${NTK_STATIC} ${NTK_STATIC_IMAGES} ${ExternLibraries})
else(EnableNTK) #FLTK
    if(TARGET fltk::fltk)   # Modern cmake
        target_link_libraries (non-mixer-xt PRIVATE fltk::fltk fltk::images ${ExternLibraries})
    else()                  # Legacy cmake
        target_link_libraries (non-mixer-xt PRIVATE ${FLTK_STATIC} ${FLTK_STATIC_IMAGES} ${ExternLibraries})
    endif()
endif(EnableNTK)

install (TARGETS non-mixer-xt RUNTIME DESTINATION bin)

# midi-mapper-xt
set (MapSources
    ${CMAKE_SOURCE_DIR}/nonlib/JACK/Client.C
    ${CMAKE_SOURCE_DIR}/nonlib/JACK/Port.C
    ${CMAKE_SOURCE_DIR}/nonlib/NSM/Client.C
    ${CMAKE_SOURCE_DIR}/nonlib/OSC/Endpoint.C
    ${CMAKE_SOURCE_DIR}/nonlib/Thread.C
    ${CMAKE_SOURCE_DIR}/nonlib/debug.C
    ${CMAKE_SOURCE_DIR}/nonlib/MIDI/midievent.C
)

add_executable (midi-mapper-xt ${MapSources} ${CMAKE_SOURCE_DIR}/mixer/src/midi-mapper.C)

target_include_directories (midi-mapper-xt PRIVATE
    ${JACK_INCLUDE_DIRS}
    ${LIBLO_INCLUDE_DIRS}
)
target_link_libraries(midi-mapper-xt PRIVATE
    ${LIBLO_LIBRARIES}
    ${JACK_LINK_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    ${DL_LIBRARY}
    ${M_LIBRARY}
)

install (TARGETS midi-mapper-xt RUNTIME DESTINATION bin)

# nmxt-plugin-scan
set (ScanSources
    ${CMAKE_SOURCE_DIR}/mixer/src/Plugin_Scan.C
    ${CMAKE_SOURCE_DIR}/mixer/src/ladspa/LADSPAInfo.C
    ${CMAKE_SOURCE_DIR}/mixer/src/clap/Clap_Discovery.C
    ${CMAKE_SOURCE_DIR}/mixer/src/vst2/Vst2_Discovery.C
    ${CMAKE_SOURCE_DIR}/mixer/src/vst3/Vst3_Discovery.C
    ${CMAKE_SOURCE_DIR}/mixer/src/vst3/VST3_common.C
    ${CMAKE_SOURCE_DIR}/nonlib/debug.C
)

add_executable (nmxt-plugin-scan ${ScanSources} ${VST3SDK_SOURCES} ${CMAKE_SOURCE_DIR}/mixer/src/nmxt-plugin-scan.C)

target_include_directories (nmxt-plugin-scan PRIVATE
        ${LRDF_INCLUDE_DIRS}
        ${LILV_INCLUDE_DIRS}
        ${SUIL_INCLUDE_DIRS}
)

target_link_libraries(nmxt-plugin-scan PRIVATE
        ${LILV_LIBRARIES}
        ${SUIL_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        ${DL_LIBRARY}
        ${LRDF_LIBRARIES}
)

if (EnableVST3Support AND CONFIG_VST3SDK)
    target_include_directories (nmxt-plugin-scan PRIVATE ${CONFIG_VST3SDK})
endif()

install (TARGETS nmxt-plugin-scan RUNTIME DESTINATION bin)


install (FILES non-mixer-xt.desktop.in
    DESTINATION share/applications RENAME non-mixer-xt.desktop)

install (FILES midi-mapper-xt.desktop.in
    DESTINATION share/applications RENAME midi-mapper-xt.desktop)
