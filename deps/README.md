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
