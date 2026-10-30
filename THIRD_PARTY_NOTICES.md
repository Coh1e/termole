# Third-party code and dependencies

## Product implementation

The product targets (`src/`, `apps/`, `include/`) contain Termole-original
source and link only Windows system components: Kernel32, User32, GDI32,
Direct2D, DirectWrite. No third-party source code or runtime is bundled in the
product DLL or executables. The static CRT is provided by the selected MSVC
toolchain.

The ConPTY setup in `src/session/session.cpp` follows Microsoft's documented
pseudoconsole contract and the same public pattern as the MIT-licensed EchoCon
sample below. The VT parser is original code written against public
specifications; behavior references and scope are recorded in
[docs/vt-core.md](docs/vt-core.md) and
[docs/adr/0005-minimal-vt-core.md](docs/adr/0005-minimal-vt-core.md).

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
It does not vendor their source or select them as product dependencies. Fixed
commits and license review boundaries are recorded in
[docs/core-validation.md](docs/core-validation.md). Any future redistribution
of their code or binaries must retain their actual licenses and notices,
including Unicode data notices where applicable.
