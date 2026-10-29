# Building 8bitPad on Linux

For the full local workflow, test reports, portable launch and Open Shell checks,
see [Building.md](Building.md).

The native Linux build uses a C++20 compiler, CMake 3.21 or newer, Ninja, and Qt
6.5 or newer. Qt Core, Gui, Widgets, Network, PrintSupport, Core5Compat,
Concurrent, Svg, Test and LinguistTools are required. The docking library also
needs Qt's private GUI development headers on Linux.

For Fedora:

```bash
sudo dnf install gcc-c++ cmake ninja-build git \
  qt6-qtbase-devel qt6-qtbase-private-devel qt6-qt5compat-devel \
  qt6-qtsvg-devel qt6-qttools-devel
```

For Ubuntu/Debian distributions providing Qt 6.5 or newer:

```bash
sudo apt install build-essential cmake ninja-build git \
  qt6-base-dev qt6-base-private-dev qt6-5compat-dev libqt6svg6-dev \
  qt6-tools-dev qt6-tools-dev-tools libxkbcommon-dev libxcb-cursor0
```

From the source root:

```bash
bash scripts/build-linux.sh \
  -DCPM_ads_SOURCE="$PWD/dependency-sources/ads" \
  -DCPM_SingleApplication_SOURCE="$PWD/dependency-sources/singleapplication" \
  -DCPM_editorconfig_core_qt_SOURCE="$PWD/dependency-sources/editorconfig-core-qt" \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON
./build-linux/src/8bitPad
```

The script builds and runs the Qt tests with the offscreen platform. It accepts
additional CMake configuration arguments and honors `BIT8PAD_BUILD_DIR`,
`CMAKE_PREFIX_PATH` and `CMAKE_BUILD_PARALLEL_LEVEL`.

The command above uses the three included pinned dependency sources without
network downloads. You can also provide other existing dependency directories:

```bash
bash scripts/build-linux.sh \
  -DCPM_ads_SOURCE=/path/to/Qt-Advanced-Docking-System \
  -DCPM_SingleApplication_SOURCE=/path/to/SingleApplication \
  -DCPM_editorconfig_core_qt_SOURCE=/path/to/editorconfig-core-qt \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON
```

Create a system-dependency archive after the tests pass:

```bash
cd build-linux
cpack -G TGZ
```

That archive contains the application and installation files, **not a bundled
Qt runtime or universal AppImage**. The executable needs compatible system Qt,
C++ runtime, libc and desktop libraries. The local Fedora 44 build uses Qt
6.11.2 and GCC 16.2.1; rebuild on the target distribution for older systems.
The docking library uses Qt private GUI APIs, so keep the corresponding Qt
build/version consistent.
