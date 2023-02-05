Non-Mixer-XT README
===================
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

If you just cloned the non repository or just executed git pull, then you should also run :

```bash
    git submodule update --init --remote
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


