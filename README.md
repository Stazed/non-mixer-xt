Non-Mixer-XT
============

Screenshot
----------

![screenshot](https://raw.github.com/Stazed/non-mixer-xt/main/mixer/doc/non-mixer-xt-1.0.0.png "Non-Mixer-XT Release 1.0.0")

Non-Mixer-XT is a reboot of original Non-Mixer with eXTended LV2 support. LV2 support includes X11, ShowInterface and External custom UI support. In addition, MIDI support with JACK timebase support and much more. The generic parameter editor has been redesigned to accommodate larger LV2 plugins, preset support and state save and restore.


Non-Mixer-XT build instructions:
--------------------------------

Dependencies :

* ntk
* lilv
* suil
* liblo
* liblo-dev
* lv2
* lv2-dev
* ladspa
* liblrdf
* jack2
* zix-0

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
    git clone https://github.com/linuxaudio/ntk
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

Controlling Non-Mixer-XT with OSC:
-------------

See [OSC.md](OSC.md)
