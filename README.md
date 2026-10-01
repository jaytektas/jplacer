# jplacer

Pick-and-place machine control, built on JFramework.

## Building

Needs the JFramework SDK installed at `$HOME/jframework-sdk`, plus CMake, Ninja and OpenSSL.

    cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$HOME/jframework-sdk
    cmake --build build
    ./build/jplacer

## Releases

Linux releases are AppImages that update themselves: jplacer checks for a newer release when it opens
and from Help > Check for Updates. Edit > Preferences turns the startup check off and on, and opts in
to beta versions.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
