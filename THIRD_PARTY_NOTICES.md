# Third-Party Notices

DjehutiCAD links against the existing JUCE and FrustLang checkouts supplied by the Djehuti development environment. Their source and license notices remain in those dependencies and are not modified by this repository.

The application shell in this repository does not copy Electronics Lab's SPICE, Xyce, circuit-solver, schematic, PCB, machine, or instrument dependencies.

## Open CASCADE Technology 7.8.0

Solid modeling, OCAF documents, STEP export (STEPCAFControl/XCAF) and STL export (StlAPI) use Open CASCADE Technology, linked statically. It is licensed under the GNU Lesser General Public License version 2.1 with the Open CASCADE exception; see `deps/LICENSE_LGPL_21.txt`, `deps/OCCT_LGPL_EXCEPTION.txt` and https://github.com/Open-Cascade-SAS/OCCT.

## lib3mf 2.5.0

3MF export uses lib3mf by the 3MF Consortium (https://github.com/3MFConsortium/lib3mf), shipped as `lib3mf.dll` next to the executable. lib3mf is licensed under the BSD 2-Clause license; the full notice is in `deps/LIB3MF_LICENSE.txt`. The prebuilt `lib3mf.dll` includes zlib 1.3.1 (Copyright 1995-2024 Jean-loup Gailly and Mark Adler, zlib license) and the other libraries listed in the lib3mf source distribution, under their own permissive licenses.
