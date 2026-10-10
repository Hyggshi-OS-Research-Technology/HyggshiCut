#!/usr/bin/env bash
# Build a self-contained x86_64 AppImage. Build dependencies must already be
# installed (see scripts/install-build-deps.sh); ffmpeg is bundled as well.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_root="${APPIMAGE_BUILD_DIR:-${repo_root}/build-appimage}"
output_dir="${APPIMAGE_OUTPUT_DIR:-${repo_root}/dist}"
mkdir -p "${build_root}" "${output_dir}"
build_root="$(cd "${build_root}" && pwd)"
output_dir="$(cd "${output_dir}" && pwd)"
cmake_build_dir="${build_root}/build"
appdir="${build_root}/AppDir"
tool_output_dir="${build_root}/image-output"

fail() {
    echo "build-appimage: $*" >&2
    exit 1
}

report_failed_command() {
    local stage="$1" log_file="$2" status="$3" message
    echo "build-appimage: ${stage} failed (exit ${status}); last output:" >&2
    tail -n 35 "${log_file}" >&2 || true
    message="$(tail -n 20 "${log_file}" 2>/dev/null | tr '\n' ' ' | \
        sed -e 's/%/%25/g' -e 's/\r/%0D/g' -e 's/:/%3A/g')"
    if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
        printf '### AppImage build failure: %s\n\n```text\n%s\n```\n' "${stage}" \
            "$(tail -n 35 "${log_file}" 2>/dev/null)" >> "${GITHUB_STEP_SUMMARY}"
    fi
    printf '::error title=%s::%s\n' "${stage}" "${message}"
}
run_logged() {
    local stage="$1" log_file="$2" status
    shift 2
    echo "Running ${stage}"
    if "$@" >"${log_file}" 2>&1; then
        cat "${log_file}"
    else
        status=$?
        report_failed_command "${stage}" "${log_file}" "${status}"
        return "${status}"
    fi
}

run_logged_in_dir() {
    local directory="$1" stage="$2" log_file="$3" status
    shift 3
    echo "Running ${stage}"
    if (cd "${directory}" && "$@") >"${log_file}" 2>&1; then
        cat "${log_file}"
    else
        status=$?
        report_failed_command "${stage}" "${log_file}" "${status}"
        return "${status}"
    fi
}

if [[ "$(uname -m)" != "x86_64" ]]; then
    fail "the linuxdeploy AppImage is x86_64-only (host: $(uname -m))"
fi

for tool in cmake curl install; do
    command -v "${tool}" >/dev/null 2>&1 || fail "required build tool not found: ${tool}"
done

ffmpeg_bin="$(command -v ffmpeg || true)"
[[ -n "${ffmpeg_bin}" ]] || fail "ffmpeg is required to build and bundle; install it first"
qmake_bin="${QMAKE:-$(command -v qmake6 || command -v qmake || true)}"
[[ -n "${qmake_bin}" ]] || fail "qmake6/qmake not found; install Qt 6 development tools"

app_version="${APP_VERSION:-$(awk '$1 == "project(HyggshiCut" && $2 == "VERSION" { print $3; exit }' "${repo_root}/CMakeLists.txt")}"
[[ -n "${app_version}" ]] || fail "could not determine the project version from CMakeLists.txt"

parallel="${CMAKE_BUILD_PARALLEL_LEVEL:-$(nproc 2>/dev/null || echo 2)}"
echo "Configuring HyggshiCut ${app_version} for AppImage (x86_64)"
run_logged "CMake configure" "${build_root}/cmake-configure.log" \
    cmake -S "${repo_root}" -B "${cmake_build_dir}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DHYGGSHICUT_BUILD_TESTS=OFF
run_logged "CMake build" "${build_root}/cmake-build.log" \
    cmake --build "${cmake_build_dir}" --parallel "${parallel}"

rm -rf -- "${appdir}" "${tool_output_dir}"
mkdir -p "${appdir}" "${tool_output_dir}"
run_logged "CMake install" "${build_root}/cmake-install.log" \
    env DESTDIR="${appdir}" cmake --install "${cmake_build_dir}"

# Desktop integration and project licences are not part of the CMake install
# targets used by the Debian package, so stage them explicitly in the AppDir.
install -D -m 0644 "${repo_root}/debian/hyggshicut.desktop" \
    "${appdir}/usr/share/applications/hyggshicut.desktop"
# linuxdeploy only accepts spec-compliant icon dimensions; the source PNG is
# 1080x1080, so derive a 512px AppImage icon without altering the Debian art.
mkdir -p "${appdir}/usr/share/pixmaps"
"${ffmpeg_bin}" -hide_banner -loglevel error -y \
    -i "${repo_root}/debian/hyggshicut.png" \
    -vf "scale=512:512:flags=lanczos" -frames:v 1 -update 1 \
    "${appdir}/usr/share/pixmaps/hyggshicut.png"
install -D -m 0644 "${repo_root}/LICENSE" \
    "${appdir}/usr/share/doc/hyggshicut/LICENSE"
install -D -m 0644 "${repo_root}/LICENSE-HOSL-1.3.md" \
    "${appdir}/usr/share/doc/hyggshicut/LICENSE-HOSL-1.3.md"

# The app uses QProcess for export, proxy generation, and screen recording.
# Place ffmpeg beside HyggshiCut; scripts/AppRun prepends that directory to
# PATH so all three features use this bundled binary rather than an unrelated
# host ffmpeg.
install -D -m 0755 "${ffmpeg_bin}" "${appdir}/usr/bin/ffmpeg"

# linuxdeploy and its Qt plugin are distributed as AppImages. Extract-and-run
# avoids requiring FUSE in CI and on developer machines.
tools_dir="$(mktemp -d "${TMPDIR:-/tmp}/hyggshicut-appimage-tools.XXXXXX")"
trap 'rm -rf -- "${tools_dir}"' EXIT
linuxdeploy="${tools_dir}/linuxdeploy-x86_64.AppImage"
qt_plugin="${tools_dir}/linuxdeploy-plugin-qt-x86_64.AppImage"
release="${LINUXDEPLOY_RELEASE:-continuous}"
curl --fail --location --retry 3 --silent --show-error \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/${release}/linuxdeploy-x86_64.AppImage" \
    --output "${linuxdeploy}"
curl --fail --location --retry 3 --silent --show-error \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/${release}/linuxdeploy-plugin-qt-x86_64.AppImage" \
    --output "${qt_plugin}"
chmod +x "${linuxdeploy}" "${qt_plugin}"

export QMAKE="${qmake_bin}"
# Bundle offscreen in addition to the default XCB platform plugin so the image
# can be smoke-tested on headless runners. Desktop Wayland sessions can use
# XWayland, which is present on supported desktop environments.
export EXTRA_PLATFORM_PLUGINS="${EXTRA_PLATFORM_PLUGINS:-libqoffscreen.so}"
export ARCH=x86_64
export APPIMAGE_EXTRACT_AND_RUN=1

run_logged_in_dir "${tool_output_dir}" "linuxdeploy" "${tool_output_dir}/linuxdeploy.log" \
    "${linuxdeploy}" \
    --appdir "${appdir}" \
    --executable "${appdir}/usr/bin/HyggshiCut" \
    --executable "${appdir}/usr/bin/ffmpeg" \
    --desktop-file "${appdir}/usr/share/applications/hyggshicut.desktop" \
    --icon-file "${appdir}/usr/share/pixmaps/hyggshicut.png" \
    --custom-apprun "${repo_root}/scripts/AppRun" \
    --plugin qt \
    --output appimage

shopt -s nullglob
images=("${tool_output_dir}"/*.AppImage)
[[ ${#images[@]} -eq 1 ]] || fail "expected one AppImage from linuxdeploy, found ${#images[@]}"
image_path="${output_dir}/HyggshiCut-${app_version}-x86_64.AppImage"
install -m 0755 "${images[0]}" "${image_path}"

echo "Built: ${image_path}"
if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "${image_path}"
fi
