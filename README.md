# jplacer

Pick-and-place machine control, built on JFramework.

## Building

Needs the JFramework SDK installed at `$HOME/jframework-sdk`, plus CMake, Ninja and OpenSSL.

    cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$HOME/jframework-sdk
    cmake --build build
    ./build/jplacer

## Releases

Linux releases are AppImages that update themselves: jplacer checks for a newer release when it opens
and from Help > Check For Updates…. Edit > Preferences turns the startup check off and on, and opts in
to beta versions.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).

## Credits

jplacer follows [OpenPnP](https://openpnp.org) and carries some of it (its icons, translations, default
configuration, illustrations and BlindsFeeder models), under OpenPnP's GPL-3.0-or-later: Copyright (C) Jason von
Nieda and the OpenPnP contributors. jplacer is a separate project, not made or endorsed by OpenPnP.
[NOTICE.md](NOTICE.md) says what comes from where, and the licences of what is built in.
