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
    src/ladspa/LADSPAInfo.C
    src/ladspa/LADSPA_Plugin.C
    src/lv2/LV2_Plugin.C
    src/clap/CLAP_Plugin.C
    src/clap/Clap_Discovery.C
    src/clap/Time.cpp
    src/vst3/Vst3_Discovery.C
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
    dl
    m
    lrdf
)

# FIXME using travesty lib makes this unnecessary
if (CONFIG_VST3SDK)
  add_definitions(-DRELEASE) # required by vstsdk
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
    ${VST3SDK_SOURCES}  # FIXME using travesty lib makes this unnecessary
    src/main.C)

# FIXME using travesty lib makes this unnecessary
if (EnableVST3Support AND CONFIG_VST3SDK)
    target_include_directories (non-mixer-xt PRIVATE ${CONFIG_VST3SDK})
endif()

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
)

target_link_libraries (non-mixer-xt ${ExternLibraries})
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

install (TARGETS midi-mapper-xt RUNTIME DESTINATION bin)

install (FILES non-mixer-xt.desktop.in
    DESTINATION share/applications RENAME non-mixer-xt.desktop)