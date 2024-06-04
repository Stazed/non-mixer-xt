#CMake file for Non-mixer-xt

project (non-mixer-xt)

set (ProgSources
    ../nonlib/JACK/Client.C
    ../nonlib/JACK/Port.C
    ../nonlib/Log_Entry.C
    ../nonlib/Loggable.C
    ../nonlib/NSM/Client.C
    ../nonlib/OSC/Endpoint.C
    ../nonlib/Thread.C
    ../nonlib/debug.C
    ../nonlib/dsp.C
    ../nonlib/file.C
    ../nonlib/MIDI/midievent.C
    ../nonlib/string_util.C
    ../nonlib/MIDI/event_list.C
    ../nonlib/MIDI/event.C
    ../nonlib/MIDI/midievent.C

    ../FL/About_Dialog.C
    ../FL/Fl_Menu_Settings.C
    ../FL/Fl_Scalepack.C
    ../FL/Fl_Text_Edit_Window.C
    ../FL/Fl_Value_SliderX.C
    ../FL/Fl_SliderX.C
    ../FL/New_Project_Dialog.C
    ../FL/event_name.C
    ../FL/menu_popup.C
    ../FL/test_press.C
    ../FL/focus_frame.C

    src/Plugin_Chooser_UI.C
    src/Chain.C
    src/Controller_Module.C
    src/DPM.C
    src/Gain_Module.C
    src/Spatializer_Module.C
    src/JACK_Module.C
    src/AUX_Module.C
    src/Plugin_Scan.C
    src/ladspa/LADSPAInfo.C
    src/ladspa/LADSPA_Plugin.C
    src/lv2/LV2_Plugin.C
    src/clap/CLAP_Plugin.C
    src/clap/Clap_Discovery.C
    src/clap/Time.cpp
    src/vst2/Vst2_Discovery.C
    src/vst2/VST2_Plugin.C
    src/vst2/VST2_Preset.C
    src/vst3/Vst3_Discovery.C
    src/vst3/VST3_Plugin.C
    src/vst3/VST3PluginHost.C
    src/vst3/Timer.C
    src/vst3/runloop.cpp
    src/vst3/EditorFrame.cpp
    src/Meter_Indicator_Module.C
    src/Meter_Module.C
    src/Mixer.C
    src/Mixer_Strip.C
    src/Module.C
    src/Module_Parameter_Editor.C
    src/Mono_Pan_Module.C
    src/Plugin_Chooser.C
    src/NSM.C
    src/Panner.C
    src/Plugin_Module.C
    src/Project.C
    src/Group.C
    src/SpectrumView.C
    src/Spatialization_Console.C
    src/lv2/lv2_evbuf.c
    src/x11/X11PluginUI.C
)

if(EnableNTK)
    message(STATUS "USING NTK BUILD")
else(EnableNTK)
    message(STATUS "USING FLTK BUILD")
    set(FLTK_specific
        ../FL/Fl_Tooltip.cxx
        ../FL/Fl_Panzoomer.C
        ../FL/Fl_Theme_Chooser.cxx
        ../FL/Fl_Theme.cxx
        ../FL/Clean_Theme.cxx
        ../FL/Crystal_Theme.cxx
        ../FL/Gleam_Theme.cxx
        ../FL/themes.cxx
    )
endif(EnableNTK)

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
endif ()

add_executable (non-mixer-xt
    ${ProgSources}
    ${FLTK_specific}
    ${VST3SDK_SOURCES}
    src/main.C)

if(EnableNTK)
    set(ExternLibraries
        ${NTK_IMAGES_LIBRARIES}
        ${NTK_LIBRARIES}
        ${JACK_LINK_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        ${X11_LIBRARIES}
        ${LIBLO_LIBRARIES}
        ${LILV_LIBRARIES}
        ${SUIL_LIBRARIES}
        ${ZIX_LIBRARIES}
        ${PangoCairo_LIBRARIES}
        dl
        m
        lrdf
    )

    target_include_directories (
        non-mixer-xt PRIVATE
        ${NTK_INCLUDE_DIRS}
        ${JACK_INCLUDE_DIRS}
        ${LRDF_INCLUDE_DIRS}
        ${LILV_INCLUDE_DIRS}
        ${SUIL_INCLUDE_DIRS}
        ${X11_INCLUDE_DIRS}
        ${LIBLO_INCLUDE_DIRS}
        ${ZIX_INCLUDE_DIRS}
        ${CLAP_INCLUDEDIR}
        ${PangoCairo_INCLUDE_DIRS}
    )

else(EnableNTK) #FLTK Build
    set(ExternLibraries
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
        fontconfig
        Xcursor
        Xfixes
        jpeg
        png
        dl
        m
        lrdf
    )

    target_include_directories (
        non-mixer-xt PRIVATE
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
endif(EnableNTK)

if (EnableVST3Support AND CONFIG_VST3SDK)
    target_include_directories (non-mixer-xt PRIVATE ${CONFIG_VST3SDK})
endif()

if(EnableNTK)
    target_link_libraries (non-mixer-xt ${ExternLibraries})
else(EnableNTK) #FLTK
    target_link_libraries (non-mixer-xt ${ExternLibraries} libfltk.a libfltk_images.a)
endif(EnableNTK)

install (TARGETS non-mixer-xt RUNTIME DESTINATION bin)

set (MapSources
    ../nonlib/JACK/Client.C
    ../nonlib/JACK/Port.C
    ../nonlib/NSM/Client.C
    ../nonlib/OSC/Endpoint.C
    ../nonlib/Thread.C
    ../nonlib/debug.C
    ../nonlib/MIDI/midievent.C
)

add_executable (midi-mapper-xt ${MapSources} src/midi-mapper.C)

if(EnableNTK)
    target_include_directories (midi-mapper-xt PRIVATE
        ${NTK_INCLUDE_DIRS}
        ${JACK_INCLUDE_DIRS}
        ${LRDF_INCLUDE_DIRS}
        ${JACK_INCLUDE_DIRS}
        ${LIBLO_INCLUDE_DIRS}
    )
    target_link_libraries(midi-mapper-xt PRIVATE
        ${LIBLO_LIBRARIES}
        ${NTK_IMAGES_LIBRARIES}
        ${NTK_LIBRARIES}
        ${JACK_LINK_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        dl
        m
        lrdf
    )

else(EnableNTK) #FLTK Build
    target_include_directories (midi-mapper-xt PRIVATE
        ${FLTK_INCLUDE_DIRS}
        ${JACK_INCLUDE_DIRS}
        ${LRDF_INCLUDE_DIRS}
        ${JACK_INCLUDE_DIRS}
        ${LIBLO_INCLUDE_DIRS}
    )
    target_link_libraries(midi-mapper-xt PRIVATE
        ${LIBLO_LIBRARIES}
        ${FLTK_LIBRARIES}
        ${JACK_LINK_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        dl
        m
        lrdf
    )
endif(EnableNTK)

install (TARGETS midi-mapper-xt RUNTIME DESTINATION bin)

install (FILES non-mixer-xt.desktop.in
    DESTINATION share/applications RENAME non-mixer-xt.desktop)
