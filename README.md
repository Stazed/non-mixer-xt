Non-Mixer-XT
============

Screenshot
----------

![screenshot](https://raw.github.com/Stazed/non-mixer-xt/main/mixer/doc/non-mixer-xt-1.0.0.png "Non-Mixer-XT Release 1.0.0")

Non-Mixer-XT is a reboot of original Non-Mixer with eXTended LV2 support, CLAP support and VST3* support. LV2 support includes X11, ShowInterface and External custom UI support. In addition, MIDI support with JACK timebase support and much more. The generic parameter editor has been redesigned to accommodate larger LV2 plugins, preset support and state save and restore. With version 1.1.0, CLAP support was added. With version 1.2.0, VST3 support was added. Special thanks to Filipe Coelho from the Carla project, David Robillard from Jalv project, and Rui Nuno Capela from the Qtractor project.


Non-Mixer-XT build instructions:
--------------------------------

Dependencies :

* ntk
* lilv      (Optional LV2 support)
* suil      (Optional LV2 support)
* liblo
* liblo-dev
* lv2       (Optional LV2 support)
* lv2-dev   (Optional LV2 support)
* ladspa    (Optional LADSPA support)
* liblrdf   (Optional LADSPA support)
* jack2
* zix-0     (Optional LV2 support)
* clap      (Optional CLAP support)

Getting submodules (nonlib and FL):
---------------

```bash
    git submodule update --init
```

Getting ZIX
-----------

If your distribution does not have ZIX available, you can get it at:

```bash
    git clone https://github.com/drobilla/zix.git
```

Getting NTK:
------------

Your distribution will likely have NTK available. If not then you can get NTK at:

```bash
    git clone https://github.com/linuxaudio/ntk.git
```

Getting CLAP:
-------------

If your distribution does not have CLAP available, you can get it at:

```bash
    git clone https://github.com/free-audio/clap.git
```

Build Non-Mixer-XT:
-------------------

For cmake build:

```bash
    mkdir build
    cd build
    cmake ..
    make
    sudo make install
```

To uninstall:

```bash
    sudo make uninstall
```

For package maintainers, if you are building generic binary packages to be used on different architectures,
then NativeOptimizations must be disabled:

```bash
    cmake -DNativeOptimizations=OFF ..
```
To disable VST3 support:

```bash
    cmake -DEnableVST3Support=OFF ..
```

To disable CLAP support:

```bash
    cmake -DEnableCLAPSupport=OFF ..
```

To disable LV2 support:

```bash
    cmake -DEnableLV2Support=OFF ..
```

To disable LADSPA support:

```bash
    cmake -DEnableLADSPASupport=OFF ..
```

Controlling Non-Mixer-XT with OSC:
-------------

See [OSC.md](OSC.md)

*VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries.