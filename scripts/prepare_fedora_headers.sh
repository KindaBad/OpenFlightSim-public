#!/usr/bin/env bash
# Optional rootless verification setup. Normal developers can install devel RPMs.
# Writes only ignored .cache; does not install packages or change /usr.
set -euo pipefail
project_root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$project_root"
mkdir -p .cache/rpms .cache/sysroot
# Restrict to Fedora repositories and the host architecture (not third-party repos).
dnf download --repo=fedora --repo=updates --arch=x86_64,noarch --destdir .cache/rpms \
  libX11-devel libXext-devel xorg-x11-proto-devel libglvnd-devel \
  libglvnd-glx libglvnd-opengl libasan libubsan
for rpm_path in "$project_root"/.cache/rpms/*.rpm; do
  case "$rpm_path" in
    *.x86_64.rpm|*.noarch.rpm)
      (cd .cache/sysroot && rpm2cpio "$rpm_path" | cpio -idmu --quiet) ;;
  esac
done
# Devel symlinks need the already-installed runtime library beside them.
python3 - <<'PY'
import glob, os
local = '.cache/sysroot/usr/lib64'
for runtime in glob.glob('/usr/lib64/lib*.so.*'):
    target = os.path.join(local, os.path.basename(runtime))
    if not os.path.lexists(target):
        os.symlink(runtime, target)
for name in ('libasan', 'libubsan'):
    target = os.path.join(local, name + '.so')
    if not os.path.lexists(target):
        os.symlink(os.path.basename(glob.glob(os.path.join(local, name + '.so.*.*.*'))[0]), target)
PY
