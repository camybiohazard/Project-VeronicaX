Third-party licences for code included in this release.

VeronicaX is built with the ReXGlue SDK (https://github.com/rexglue/rexglue-sdk),
which includes code derived from Xenia (https://xenia.jp). Its runtime and GPU
plugin libraries (librexruntime.so and librexgpu-xenos.so on Linux,
rexruntime.dll and rexgpu-xenos.dll on Windows) contain the libraries
listed in the folders here. Each folder's SOURCE file names the exact
upstream commit.

FFmpeg (libavcodec/libavutil) and libmspack are licensed under the GNU Lesser
General Public License v2.1. Their complete source code, and the ReXGlue code
that links them, is available from the repositories named in their SOURCE
files and from https://github.com/rexglue/rexglue-sdk (submodules under
thirdparty/). You may rebuild librexruntime.so with modified versions of these
libraries and use it in place of the one shipped here.
