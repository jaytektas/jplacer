# Notices

jplacer is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any
later version (see [LICENSE](LICENSE)). Copyright (C) 2026 Jason Roughley.

## OpenPnP

jplacer follows [OpenPnP](https://openpnp.org) (https://github.com/openpnp/openpnp): its machines, feeders,
vision and jobs; it reads and writes OpenPnP's configuration files; and much of its interface's wording (labels,
tooltips, notes) is OpenPnP's. It also carries these parts of OpenPnP, as OpenPnP ships them:

| In jplacer | From OpenPnP |
|---|---|
| `icons/` | `src/main/resources/icons` (jplacer's own few are named in its README) |
| `translations/` | `src/main/resources/org/openpnp/translations*.properties` |
| `openpnp-defaults/` | `src/main/resources/config` and `src/main/resources/samples` |
| `illustrations/` | `src/main/resources/illustrations` |
| `openscad/` | the BlindsFeeder 3D-printing files, Copyright (C) 2019-2020 makr@makr.zone |

OpenPnP is Copyright (C) 2011 Jason von Nieda <jason@vonnieda.org> and the OpenPnP contributors, and is free
software under the GNU General Public License, version 3 or (at your option) any later version. What jplacer
carries of it stays under that licence. jplacer is a separate project: it is not part of OpenPnP, and the OpenPnP
project neither makes nor endorses it. Thank you to everyone who made OpenPnP.

## Built in

| Part | Licence |
|---|---|
| [JFramework](https://github.com/jaytektas/JFramework), Copyright (C) 2026 Jason Roughley | GPL-3.0-or-later |
| [OpenCV](https://opencv.org) 4.10 (core, imgproc, calib3d, features2d, flann, objdetect), and the libraries it builds in (zlib, quirc, SoftFloat, flatbuffers, the MSCR chi table) | Apache-2.0 ([licenses/OpenCV-LICENSE.txt](licenses/OpenCV-LICENSE.txt)); the others' in `licenses/OpenCV-3rdparty-*` |
| [nanosvg](https://github.com/memononen/nanosvg), Copyright (c) 2013-14 Mikko Mononen | zlib ([licenses/nanosvg-LICENSE.txt](licenses/nanosvg-LICENSE.txt)) |

### Built into the Windows build too

On Linux these come from the system; `jplacer.exe` has them built in (`packaging/build-windows-deps.sh` builds
them), and carries the Vulkan loader beside it.

| Part | Licence |
|---|---|
| [zlib](https://zlib.net) 1.3.1 | zlib ([licenses/zlib-LICENSE](licenses/zlib-LICENSE)) |
| [libpng](http://www.libpng.org) 1.6.43 | PNG Reference Library License v2 ([licenses/libpng-LICENSE](licenses/libpng-LICENSE)) |
| [libjpeg-turbo](https://libjpeg-turbo.org) 3.0.3: this software is based in part on the work of the Independent JPEG Group | IJG and BSD-3-Clause ([licenses/libjpeg-turbo-LICENSE.md](licenses/libjpeg-turbo-LICENSE.md), [licenses/libjpeg-turbo-README.ijg](licenses/libjpeg-turbo-README.ijg)) |
| [Expat](https://libexpat.github.io) 2.6.2 | MIT ([licenses/expat-COPYING](licenses/expat-COPYING)) |
| [OpenSSL](https://www.openssl.org) 3.0.15 | Apache-2.0 ([licenses/OpenSSL-LICENSE.txt](licenses/OpenSSL-LICENSE.txt)) |
| [SQLite](https://sqlite.org) 3.46.1 | public domain |
| [Vulkan Loader](https://github.com/KhronosGroup/Vulkan-Loader) 1.3.290 (`vulkan-1.dll`) | Apache-2.0 ([licenses/Vulkan-Loader-LICENSE.txt](licenses/Vulkan-Loader-LICENSE.txt)) |
| [MinGW-w64](https://www.mingw-w64.org)'s runtime (winpthreads and the C runtime's start-up), and GCC's (libgcc, libstdc++: the GCC Runtime Library Exception) | as their copyright files say ([licenses/mingw-w64-runtime-copyright.txt](licenses/mingw-w64-runtime-copyright.txt), [licenses/gcc-mingw-w64-runtime-copyright.txt](licenses/gcc-mingw-w64-runtime-copyright.txt)) |

The Jaytek logo in the About box (`icons/jaytek-logo.png`) is Jaytek's.

The AppImage carries this file, jplacer's licence and the `licenses/` folder in `usr/share/doc/jplacer`; the
Windows build, beside `jplacer.exe`.
