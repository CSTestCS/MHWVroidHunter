ooz - open source Kraken / Mermaid / Selkie / Leviathan decompressor
Copyright (C) 2016, Powzix - https://github.com/powzix/ooz - GPL-3.0-or-later.

Changes for MHW VRoid Hunter:
* `stdafx.h` replaced with a portable shim so the code builds with GCC/Clang and MinGW-w64.
* The command line tool and Oodle DLL loader at the end of `kraken.cpp` were removed.
* `Kraken_DecompressBounded` was added (decodes until the input is consumed).
