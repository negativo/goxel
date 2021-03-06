
Goxel
=====

Version 0.1

By Guillaume Chereau <guillaume@noctua-software.com>

                    _______  _______  __   __  _______  ___
                   |       ||       ||  |_|  ||       ||   |
                   |    ___||   _   ||       ||    ___||   |
                   |   | __ |  | |  ||       ||   |___ |   |
                   |   ||  ||  |_|  | |     | |    ___||   |___
                   |   |_| ||       ||   _   ||   |___ |       |
                   |_______||_______||__| |__||_______||_______|

About
-----

You can use goxel to create voxel graphics (3D images formed of cubes).  It
works on Linux and Windows.

![goxel screenshot 0](/screenshots/screenshot-0.png?raw=true)
![goxel screenshot 1](/screenshots/screenshot-1.png?raw=true)


Licence
-------

Goxel is released under the GPL3 licence.


Features
--------

- 24 bits RGB colors.
- Unlimited scene size.
- Unlimited undo buffer.
- Layers.
- Smooth rendering mode.
- Export to obj and pyl.


Usage
-----

- Left click: apply selected tool operation.
- Middle click: rotate the view.
- right click: pan the view.
- Left/Right arrow: rotate the view.
- Mouse wheel: zoom in and out.


Building
--------

The building system uses scons.  You can compile in debug with 'scons', and in
release with 'scons debug=0'.  On Windows, I only tried to build with msys2.
The code is in C99, using some gnu extensions, so it does not compile with
msvc.
