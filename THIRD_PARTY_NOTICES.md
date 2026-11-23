# Third-party code and dependencies

## Product implementation

Termole uses the libghostty-vt static library for terminal emulation. The
Windows build uses MinGW-w64 and links Windows system libraries. The pinned
Windows artifact, checksum and license are listed in the
[dependency source record](https://github.com/Coh1e/termole/blob/main/third_party/libghostty-vt/SOURCE.txt). The optional
QuickJS engine and the SDL3/FreeType window dependencies are listed below.

## Microsoft Terminal EchoCon (MIT)

The ConPTY initialization flow in `spikes/picker/picker_spike.cpp` and
`src/session/session.cpp` is adapted from
[EchoCon.cpp](https://github.com/microsoft/terminal/blob/95142e6ebfcbce21da27c3f7eb5b87e8abdb2555/samples/ConPTY/EchoCon/EchoCon/EchoCon.cpp).

Copyright (c) Microsoft Corporation. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Windows Implementation Libraries (WIL)

The picker spike uses Microsoft's header-only resource management helpers,
fetched at commit `a1c0cbe1d7065aef6333399d3e25608eacdffc0f`.
[Source and license](https://github.com/microsoft/wil/tree/a1c0cbe1d7065aef6333399d3e25608eacdffc0f).
Copyright (c) Microsoft Corporation. All rights reserved. WIL uses the MIT
permission, conditions and warranty disclaimer reproduced above. Retain the
upstream license with any redistributed WIL code.

## fzf

Validation uses the user's installed, unmodified fzf 0.74.5. No fzf binary or
source is included in this repository. Its MIT-licensed source is inspected at
commit `c0a193e031f01fb50bf6f7be4381990a3753f7cf`.

## Optional Contour comparison probe

The disabled-by-default `spikes/core` probe consumes original upstream Contour,
libunicode, boxed-cpp and reflection-cpp (Apache-2.0), plus Microsoft GSL (MIT).
It does not vendor their source or select them as product dependencies. The [probe CMake project](https://github.com/Coh1e/termole/blob/main/spikes/core/minimal/CMakeLists.txt) pins the source commits. Any future redistribution
of their code or binaries must retain their actual licenses and notices,
including Unicode data notices where applicable.

## libghostty-vt

- Version: 1.0.1
- Artifact: `ghostty-vt-1.0.1-x86_64-windows-gnu.zip` (static library)
- SHA-256: `8e2eb9e1822e85d786b456c8beb86f49d09c318c91f0c2913f91e7ffa12cda29`
- License: MPL-2.0; the unmodified license text is vendored at
  `third_party/libghostty-vt/LICENSE`.
- Source: <https://release.files.ghostty.org/libghostty-vt/1.0.1/>
- Usage: linked statically into the product; no upstream files are modified.

## QuickJS

- Version: 2026-06-04, MIT.
- Source archive: [QuickJS upstream](https://bellard.org/quickjs/quickjs-2026-06-04.tar.xz).
- SHA-256: `b376e839b322978313d929fd20663b11ba58b75df5a46c126dd19ea2fa70ad2a`.
- Linked into the static SDK only when `TERMOLE_WITH_QUICKJS=ON`. The original
  archive and license are in `third_party/quickjs/2026-06-04/`.

## Not bundled

- MinGW-w64 runtime: linked statically; no runtime DLL is shipped.

## SDL3 and FreeType

The Linux and macOS terminal window statically links the following pinned
upstream releases. Their source archives include the applicable license texts.

- [SDL 3.4.20](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.20),
  zlib license. Source SHA-256 `f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f`.
- [FreeType 2.14.3](https://freetype.org/download.html), FreeType License.
  Source SHA-256 `36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f`.
