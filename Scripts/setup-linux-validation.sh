#!/usr/bin/env bash
set -euo pipefail

# Optional headless JUCE validation on the Debian cloud image. The release
# platform remains Windows. APT verifies signed indexes and package hashes;
# all writes go to the task's tools directory, without installing system files.
ivory_repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ivory_tools_dir="${IVORY_TOOLS_DIR:-/workspace/.tools}"
bash "$ivory_repo_dir/Scripts/setup-cloud.sh"
ivory_apt_dir="$ivory_tools_dir/apt"
ivory_sysroot="$ivory_tools_dir/sysroot"
mkdir -p "$ivory_apt_dir"/{lists/partial,cache/archives/partial,etc/parts,etc/sources.list.d,etc/preferences.d,debs} "$ivory_sysroot"
cat > "$ivory_apt_dir/etc/sources.list" <<'SOURCES'
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main
SOURCES
cat > "$ivory_apt_dir/apt.conf" <<APTCONF
Dir::Etc "$ivory_apt_dir/etc";
Dir::Etc::Parts "parts";
Dir::Etc::sourcelist "sources.list";
Dir::Etc::sourceparts "sources.list.d";
Dir::State "$ivory_apt_dir";
Dir::State::status "/var/lib/dpkg/status";
Dir::State::lists "lists";
Dir::Cache "$ivory_apt_dir/cache";
APT::Sandbox::User "$(id -un)";
Acquire::Languages "none";
APTCONF
export APT_CONFIG="$ivory_apt_dir/apt.conf"
/usr/bin/apt-get update
# --download-only never invokes dpkg. NoLocking avoids a system dpkg lock that
# a non-root package download does not need; signature/hash checks stay enabled.
/usr/bin/apt-get --download-only -y -o Debug::NoLocking=1 \
    -o "Dir::Cache::archives=$ivory_apt_dir/debs" install \
    libx11-dev libxext-dev libxinerama-dev libxrandr-dev libxcursor-dev \
    libxrender-dev libfreetype-dev libfontconfig-dev libasound2-dev libgl-dev xvfb
(
    cd "$ivory_apt_dir/debs"
    /usr/bin/apt-get download libasound2t64 libx11-6 libxext6 libxinerama1 \
        libxrandr2 libxcursor1 libxrender1 libxfixes3 libfreetype6 libfontconfig1 \
        libgl1 libglx0 libglvnd0 libpng16-16t64 libbz2-1.0 libxcb1 libxau6 libxdmcp6
)
for ivory_deb in "$ivory_apt_dir/debs/"*.deb; do
    dpkg-deb -x "$ivory_deb" "$ivory_sysroot"
done
cat >> "$ivory_tools_dir/activate-ivory.sh" <<ACTIVATE
export PKG_CONFIG_PATH="$ivory_sysroot/usr/lib/x86_64-linux-gnu/pkgconfig:$ivory_sysroot/usr/share/pkgconfig\${PKG_CONFIG_PATH:+:\$PKG_CONFIG_PATH}"
export PKG_CONFIG_SYSROOT_DIR="$ivory_sysroot"
export CPATH="$ivory_sysroot/usr/include\${CPATH:+:\$CPATH}"
export LIBRARY_PATH="$ivory_sysroot/usr/lib/x86_64-linux-gnu\${LIBRARY_PATH:+:\$LIBRARY_PATH}"
export LD_LIBRARY_PATH="$ivory_sysroot/usr/lib/x86_64-linux-gnu\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
ACTIVATE
source "$ivory_tools_dir/activate-ivory.sh"
cmake -S "$ivory_repo_dir" -B "$ivory_repo_dir/Builds/Linux" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DIVORY_LINUX_VALIDATION=ON \
    -DIVORY_JUCE_PATH="$ivory_tools_dir/JUCE"
cmake --build "$ivory_repo_dir/Builds/Linux" --parallel 2
