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

Getting NTK:
------------

Your distribution may have the NTK library. If not, then do the following to build and install the NTK submodule.

If you just cloned the non-mixer-xt repository or just executed git pull, then you should also run :

```bash
    git submodule update --init
```

to pull down the latest NTK code required by Non. Git does *not* do this automatically.

Building NTK:
-------------

If you don't have NTK installed system-wide (which isn't very likely yet) you *MUST* begin the build process by typing:

```bash
    cd lib/ntk
    ./waf configure
    ./waf
```

Once NTK has been built you must install it system-wide before attempting to build non-mixer-xt.

To install NTK type:

```bash
    sudo ./waf install
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


