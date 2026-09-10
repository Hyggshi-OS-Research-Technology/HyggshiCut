#!/usr/bin/env bash
#
# Install everything needed to build HyggshiCut, derived from debian/control.
#
# Why this exists rather than a vcpkg.json / conanfile.txt:
#
#   HyggshiCut links Qt 6, FFmpeg, libmpv and ALSA. On Linux those are large,
#   system-integrated libraries that every distro already packages, and that
#   the .deb has to depend on at the distro level anyway. Building them from
#   source through vcpkg or Conan would add a multi-hour first build and a
#   second, divergent set of version constraints, while `debian/control` still
#   had to list the runtime deps for packaging. The manifest already exists —
#   it is debian/control.
#
#   The real problem was that the same dependency list was written out by hand
#   in three places (debian/control, the Dockerfile and the CI workflow), free
#   to drift apart. This script parses Build-Depends so there is one list.
#
# Usage:
#   ./scripts/install-build-deps.sh          # install via apt (needs sudo)
#   ./scripts/install-build-deps.sh --print  # just print the package list

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
control="${repo_root}/debian/control"

if [[ ! -f "${control}" ]]; then
    echo "error: ${control} not found" >&2
    exit 1
fi

# Extract Build-Depends: take everything from that field up to the next
# top-level field, strip version constraints "(>= 3.20)", architecture
# qualifiers and comments, drop debhelper-compat (a build-system
# pseudo-package, not something apt installs under that name), and for an
# alternatives group like "libgl-dev | libgl1-mesa-dev" take the first
# option, which is what apt would pick anyway.
mapfile -t packages < <(
    awk '
        /^Build-Depends:/ { inblock = 1; sub(/^Build-Depends:/, ""); }
        inblock && /^[A-Za-z-]+:/ && !/^Build-Depends:/ { inblock = 0 }
        inblock { print }
    ' "${control}" \
    | tr ',' '\n' \
    | sed -e 's/([^)]*)//g' -e 's/\[[^]]*\]//g' -e 's/#.*//' \
    | tr -d ' \t' \
    | grep -v '^$' \
    | grep -v '^debhelper-compat$' \
    | cut -d'|' -f1 \
    | sort -u
)

if [[ ${#packages[@]} -eq 0 ]]; then
    echo "error: parsed no packages out of ${control}" >&2
    exit 1
fi

# debhelper-compat is expressed as a versioned build-system dependency; the
# actual tool to install is debhelper. dpkg-dev provides dpkg-buildpackage.
packages+=(debhelper dpkg-dev)

if [[ "${1:-}" == "--print" ]]; then
    printf '%s\n' "${packages[@]}"
    exit 0
fi

echo "Installing ${#packages[@]} build dependencies from debian/control:"
printf '  %s\n' "${packages[@]}"
echo

sudo apt-get update
sudo apt-get install -y --no-install-recommends "${packages[@]}"

echo
echo "Done. Configure and build with:"
echo "  cmake --preset tests && cmake --build --preset tests && ctest --preset tests"
