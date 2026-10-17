#!/usr/bin/env bash
set -euo pipefail

# Non-root bootstrap. Keep dependencies outside the application checkout.
ivory_repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ivory_tools_dir="${IVORY_TOOLS_DIR:-/workspace/.tools}"
mkdir -p "$ivory_tools_dir"
python3 -m pip install --disable-pip-version-check --no-cache-dir --upgrade --target "$ivory_tools_dir/python" \
    cmake==3.31.6 ninja==1.11.1.4

if [[ ! -d "$ivory_tools_dir/JUCE/.git" ]]; then
    git clone --depth 1 --branch 8.0.10 https://github.com/juce-framework/JUCE.git "$ivory_tools_dir/JUCE"
fi
ivory_juce_commit="$(git -C "$ivory_tools_dir/JUCE" rev-parse HEAD)"
if [[ "$ivory_juce_commit" != 3af3ce009f6a02f6fa651008fffb5b41743a9fab ]]; then
    echo "Unexpected JUCE revision; preserving the existing checkout. Set IVORY_TOOLS_DIR to a fresh dependency directory." >&2
    exit 1
fi

cat > "$ivory_tools_dir/activate-ivory.sh" <<ACTIVATE
export PATH="$ivory_tools_dir/python/cmake/data/bin:$ivory_tools_dir/python/bin:\$PATH"
export IVORY_JUCE_PATH="$ivory_tools_dir/JUCE"
ACTIVATE
source "$ivory_tools_dir/activate-ivory.sh"
cmake -S "$ivory_repo_dir" -B "$ivory_repo_dir/Builds/DSP" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DIVORY_BUILD_PLUGIN=OFF -DIVORY_BUILD_TESTS=ON
cmake --build "$ivory_repo_dir/Builds/DSP" --parallel 2
ctest --test-dir "$ivory_repo_dir/Builds/DSP" --output-on-failure
