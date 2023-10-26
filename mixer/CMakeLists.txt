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
    src/LADSPAInfo.C
    src/LADSPA_Plugin.C
    src/LV2_Plugin.C
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
    src/zix/ring.C
    src/lv2/lv2_evbuf.c
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
    dl
    m
    lrdf
)

add_executable (non-mixer-xt ${ProgSources} src/main.C)

target_include_directories (
    non-mixer-xt PRIVATE
    ${NTK_INCLUDE_DIRS}
    ${JACK_INCLUDE_DIRS}
    ${LRDF_INCLUDE_DIRS}
    ${LILV_INCLUDE_DIRS}
    ${SUIL_INCLUDE_DIRS}
    ${X11_INCLUDE_DIRS}
    ${LIBLO_INCLUDE_DIRS}
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