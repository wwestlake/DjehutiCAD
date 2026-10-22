# DjehutiCAD native dependencies

This directory contains the pinned native geometry dependency used by the
solid-modeling foundation. OCCT `V7_8_0` is configured and built as a private
Windows x64 static dependency under `occt-build`. The source checkout is kept
outside the application repository history; recreate it from the pinned tag
when provisioning a new machine.

Open CASCADE Technology is distributed under the GNU Lesser General Public
License version 2.1 with the OCCT exception. The complete applicable notices
are preserved beside the local source checkout as `LICENSE_LGPL_21.txt` and
`OCCT_LGPL_EXCEPTION.txt`, and the upstream project is at:

https://github.com/Open-Cascade-SAS/OCCT

## OCCT toolkit for Sweep, Loft and Router/Mill

These features also need the static `TKOffset` toolkit from the existing pinned
OCCT configuration. Its modeling prerequisites are already used by the app:

```powershell
cmake --build occt-build --config Debug --target TKOffset --parallel 2
```

Build the same target with `--config Release` before a Release application build.
No OCCT source changes or new geometry runtime are required.

## OCCT toolkits for manufacturing export

Export needs the STEP and STL data-exchange toolkits, which are not part of the
modules configured for modeling. They are added to the same `occt-build`
configuration without enabling whole modules:

```powershell
cmake -S occt-src -B occt-build "-DBUILD_ADDITIONAL_TOOLKITS=TKDESTEP;TKDESTL;TKBinXCAF"
cmake --build occt-build --config Debug --target TKXSBase TKDE TKHLR TKService TKV3d TKVCAF TKXCAF TKBinXCAF TKDESTL TKDESTEP --parallel 1
```

The configuration adds their dependencies (`TKDE`, `TKXSBase`, `TKXCAF`,
`TKVCAF`, `TKV3d`, `TKService`, `TKHLR`), but building a static toolkit does
not build the toolkits it depends on, so all of them are listed. A Release
build needs the same targets built with `--config Release`.

## lib3mf (3MF export)

`lib3mf-sdk/` holds the unpacked official lib3mf 2.5.0 SDK from the 3MF
Consortium (`lib3mf_sdk_v2.5.0.zip`, pinned in `LIB3MF_VERSION.txt` with its
SHA-256). It is not committed; download the release asset from
https://github.com/3MFConsortium/lib3mf/releases/tag/v2.5.0, verify the hash
and unpack its `lib3mf_sdk` folder here as `lib3mf-sdk`. CMake uses the C++
binding, the import library and copies `lib3mf.dll` next to the executable.
lib3mf is BSD-2-Clause licensed; the notice is kept as `LIB3MF_LICENSE.txt`.
