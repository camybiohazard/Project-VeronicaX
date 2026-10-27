#!/usr/bin/env bash
# Download the licence texts of the code shipped in a release into
# third_party_licenses/. That is ReXGlue itself plus the libraries compiled
# into librexruntime.so and librexgpu-xenos.so. Each file comes from the
# commit ReXGlue pins for that library.
#
# The table matches ReXGlue bd833a2 (SDK 0.10.0.24-dev). After an SDK update,
# regenerate it from that commit's submodules:
#   git ls-tree <commit> thirdparty/<name>
# Needs the GitHub CLI (gh).
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/third_party_licenses"

rexglue_commit=bd833a2ab69d305f99ddf57418fc38ffec8af93d
# name  github-repo  commit  file[,file...]
libraries=(
    "SDL3 libsdl-org/SDL 147a8ee32dbf9ac02f3794964490687b6bbda1bc LICENSE.txt"
    "FFmpeg wmarti/FFmpeg 0604b464c7cb4ebc94940cf1f324a3b26b87717c LICENSE.md,COPYING.LGPLv2.1"
    "libmspack kyz/libmspack 305907723a4e7ab2018e58040059ffb5e77db837 libmspack/COPYING.LIB"
    "spdlog gabime/spdlog 79524ddd08a4ec981b7fea76afd08ee05f83755d LICENSE"
    "fmt fmtlib/fmt 407c905e45ad75fc29bf0f9bb7c5c2fd3475976f LICENSE"
    "glslang KhronosGroup/glslang f4f1d8a352ca1908943aea2ad8c54b39b4879080 LICENSE.txt"
    "SPIRV-Tools KhronosGroup/SPIRV-Tools 9a49b0883b9b635689a85b5647dbfcb223268151 LICENSE"
    "SPIRV-Headers KhronosGroup/SPIRV-Headers 29981f65241605e08b0ede4cfeb999fe3b723c6a LICENSE"
    "Vulkan-Headers KhronosGroup/Vulkan-Headers e3b1eec08173d6b825cd3ac88c885a63b621504a LICENSE.md"
    "VulkanMemoryAllocator GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator 1d8f600fd424278486eade7ed3e877c99f0846b1 LICENSE.txt"
    "imgui ocornut/imgui 6d910d5487d11ca567b61c7824b0c78c569d62f0 LICENSE.txt"
    "tomlplusplus marzer/tomlplusplus 30172438cee64926dc41fdd9c11fb3ba5b2ba9de LICENSE"
    "simde simd-everywhere/simde 71fd833d9666141edcd1d3c109a80e228303d8d7 COPYING"
    "xxHash Cyan4973/xxHash e626a72bc2321cd320e953a0ccf1584cad60f363 LICENSE"
    "CLI11 CLIUtils/CLI11 bfffd37e1f804ca4fae1caae106935791696b6a9 LICENSE"
    "o1heap pavel-kirienko/o1heap 388a73fd9007300e5130c5fe352d9ce3288b6dde LICENSE"
    "utfcpp nemtrif/utfcpp 63d64de49fd6b829f7c8694df5ab2ee625cb7134 LICENSE"
)

fetch() {  # repo commit path dest
    gh api -H "Accept: application/vnd.github.raw" "repos/$1/contents/$3?ref=$2" > "$4"
}

rm -rf "$out"
mkdir -p "$out/ReXGlue"
fetch rexglue/rexglue-sdk "$rexglue_commit" LICENSE "$out/ReXGlue/LICENSE"
for aes in LICENSE README.md; do
    fetch rexglue/rexglue-sdk "$rexglue_commit" "thirdparty/aes_128/$aes" "$out/ReXGlue/aes_128-$aes"
done

for entry in "${libraries[@]}"; do
    read -r name repo commit files <<< "$entry"
    mkdir -p "$out/$name"
    IFS=, read -ra list <<< "$files"
    for file in "${list[@]}"; do
        fetch "$repo" "$commit" "$file" "$out/$name/$(basename "$file")"
    done
    echo "$repo @ $commit" > "$out/$name/SOURCE"
done

cat > "$out/README.txt" <<'EOF'
Third-party licences for code included in this release.

VeronicaX is built with the ReXGlue SDK (https://github.com/rexglue/rexglue-sdk),
which includes code derived from Xenia (https://xenia.jp). Its runtime and GPU
plugin libraries (librexruntime.so, librexgpu-xenos.so) contain the libraries
listed in the folders here. Each folder's SOURCE file names the exact
upstream commit.

FFmpeg (libavcodec/libavutil) and libmspack are licensed under the GNU Lesser
General Public License v2.1. Their complete source code, and the ReXGlue code
that links them, is available from the repositories named in their SOURCE
files and from https://github.com/rexglue/rexglue-sdk (submodules under
thirdparty/). You may rebuild librexruntime.so with modified versions of these
libraries and use it in place of the one shipped here.
EOF
echo "Wrote licences for $(( ${#libraries[@]} + 1 )) components to third_party_licenses/"
