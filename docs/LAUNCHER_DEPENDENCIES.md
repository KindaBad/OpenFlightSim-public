# Launcher distribution dependencies

The launcher uses PySide6 Essentials 6.10.2 (Qt Core/Gui/Widgets) and shiboken6,
under the upstream LGPLv3/GPLv3/commercial licensing options. It uses dynamic Qt
libraries in an inspectable one-directory bundle, without Qt WebEngine or a
browser runtime. The public source for the launcher is in `launcher/` and the
reproducible packaging procedure is in `scripts/package_launcher.py`.
The separate standalone setup is a PyInstaller one-file application: it extracts
its Python/Qt runtime and embedded stable bootstrap to a temporary directory.
The downloaded game still carries the full launcher in the one-directory bundle.
Include setup's dependencies in the same notices, matching source publication
and selected license review as the main launcher before distributing it.

The GitHub publication job uploads the unmodified Qt 6.10.2 and
PySide/shiboken 6.10.2 corresponding source archives to the persistent
`qt-sources-6.10.2` release before publishing game binaries. Official source
URLs, archive sizes and SHA-256 digests are pinned in
`scripts/dependency-sources.json`; changing the PySide/Qt version requires
updating those pins and the release-note source link together. LGPLv3, GPLv3
and Qt's GPL exception texts are carried in `licenses/qt/` and installed notices.

Redistributors must satisfy their selected upstream license, including applicable
license texts, notices, source availability and relinking/replacement obligations.
Obtain the **matching** Qt/PySide/shiboken source archives and license texts from
<https://download.qt.io/official_releases/QtForPython/> and
<https://download.qt.io/official_releases/qt/> before public publication. The
binary wheels do not consistently include all license texts. Publish those
matching sources or the applicable offer alongside the binaries; link them from
publisher release documentation. A commercial license may have different terms.

`collect_release_notices.py` collects notices present in actual CMake dependency
sources and installed Python package metadata/license files. It also carries the
Karla font OFL notice and stb_image notice/source. The collection is evidence,
not a replacement for reviewing all distributed dependencies. PyInstaller uses
its GPL exception for generated applications; Python uses PSF licensing. Qt may
bundle third-party ICU, image codecs, and platform support libraries, which also
have notices/source requirements. Include these when publishing the chosen
platform's bundle. Preserve vcpkg `installed/*/share/*/copyright` when shipping
its Windows OpenSSL/protobuf DLLs.

SDL, bgfx/bx/bimg, ImGui, GLM, GameNetworkingSockets, OpenSSL and protobuf retain
their respective upstream terms; pinned revisions are in the CMake sources and
`docs/BUILDING.md`. The simulator contains embedded shaders and the Karla font.
Assets need separate hash-bound redistribution approval; see
`docs/ASSET_RELEASE_PROVENANCE.md`. No asset license is granted by this launcher.

Linux binaries are built against the release runner's glibc baseline (Ubuntu
24.04 in the prepared CI workflow). An accelerated OpenGL 4.3/EGL driver,
X11/XWayland and OpenSSL 3 are runtime requirements; current Fedora and Ubuntu
desktops provide them. protobuf (and its abseil/utf8 companions where the build
host has them) is installed into the release's `lib/` folder and found through
an `$ORIGIN/lib` run path, because its soname differs between distributions;
preserve its license notice with the package. Other system libraries are not
copied from the build host. For other distributions build locally or package matching
native runtime dependencies. Windows targets x64 Windows 10/11; CMake collects
non-system DLL dependencies and installs the MSVC runtime. Qt and Python runtime
libraries travel in the launcher bundle.
