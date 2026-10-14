# OpenXLSX Grid - motivation and build guide

## What this project teaches

The grid belongs to adecc::cpp_core, while XLSX is only a physical representation.
This project adapts OpenXLSX 0.5.1 to the existing C++23 grid_concept.
All typed row ranges, iterator proxies and output sinks continue to come
from cpp_core. The adapter does not introduce another ranges library.

The teaching example opens an XLSX workbook or creates it when missing,
creates a missing worksheet, selects a complete rectangular region, writes
captions including width and horizontal alignment, converts values at the
Excel boundary, saves and reopens the file, and checks the real persisted data.

## Sources

- adecc/openXlsxTools/openxlsx_session.h: shared RAII lifetime of XLDocument.
- adecc/openXlsxTools/openxlsx_workbook.h: open/create, worksheet, explicit save.
- adecc/openXlsxTools/openxlsx_grid_backend.h: selected grid region, captions,
  cell access, position, reset, freeze/unfreeze and row insertion.
- adecc/openXlsxTools/openxlsx_cell_conversion.h: value and optional conversion.
- test/OpenXlsxGrid/main.cpp: independently executable XLSX roundtrip.
- test/OpenXlsxGrid/CMakeLists.txt: BCC64X/Ninja, CTest and install.

## Coordinates, dimensions and lifetime

GridRegion takes a one-based Excel row/column origin, an explicit DATA row
count, an explicit column count, and a flag for an optional caption row.
GridModel uses zero-based relative indices; captions do not count as data.
A new region starts with zero rows. set_captions() establishes the width
without adding data rows. Reading a region requires supplying its exact
dimensions rather than relying on worksheet lastCell().

All workbook-backed grids hold a shared RAII session so that an XLDocument
cannot be destroyed beneath a live view. save() is explicit and close()
rejects live grid handles. The freeze guard controls the grid's logical
batch-update contract, not persisted Excel freeze panes. focus_row() and
focus_cell() set the logical grid position, not Excel's active UI cell.
The second, non-range Excel facade is planned separately and will reuse
the same session and conversions.

The supported types include strings, integers, double, money, calendar date,
timestamp and time, as well as optional values. Large integer identifiers
are stored as text to avoid Excel's 15-digit precision loss. The numeric
1900-date system has the historical Excel leap-year exception.

## Requirements and pinned package provenance

Use Windows, RAD Studio 13 / C++Builder 13 BCC64X, CMake and project-managed
Ninja. OpenXLSX 0.5.1 is pinned in the DeckKernel thirdparty.lock and its
companion XML lock. The package belongs to GitHub release 2026.09.26-1.
The original 16 package archives are unchanged.

Cache/StackPackages.xml is the versioned StackBuilder selection (17 packages).
Bootstrap verifies its SHA-256 and verifies package entries against both lock
representations before downloading. The bootstrap also checks archive byte
size and SHA-256 and installs OpenXLSX below ThirdParty/openxlsx/0.5.1.

Run the following command in a C++Builder Developer Command Prompt from the
DeckKernel repository root:

```cmd
cmake -P bootstrap\Bootstrap.cmake
```

No manual BuildEngine install path, global package fallback or environment
variable is required. The generated ADECC_TP_OPENXLSX_ROOT is the only
OpenXLSX root used by this test.

## Debug - configure, build, test, install, run

```cmd
cmake -S test\OpenXlsxGrid -B test\OpenXlsxGrid\build\Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

```cmd
cmake --build test\OpenXlsxGrid\build\Debug
```

```cmd
ctest --test-dir test\OpenXlsxGrid\build\Debug --output-on-failure
```

```cmd
cmake --install test\OpenXlsxGrid\build\Debug
```

```cmd
apps\Debug\adecc_openxlsx_grid_test.exe
```

## Release - configure, build, test, install, run

```cmd
cmake -S test\OpenXlsxGrid -B test\OpenXlsxGrid\build\Release -G Ninja -DCMAKE_BUILD_TYPE=Release
```

```cmd
cmake --build test\OpenXlsxGrid\build\Release
```

```cmd
ctest --test-dir test\OpenXlsxGrid\build\Release --output-on-failure
```

```cmd
cmake --install test\OpenXlsxGrid\build\Release
```

```cmd
apps\Release\adecc_openxlsx_grid_test.exe
```

The test uses ADECC_TP_OPENXLSX_ROOT from the bootstrap-generated package map.
The former DECKKERNEL_OPENXLSX_ROOT override is ignored.

## What install copies and how to check

Install uses apps/Debug or apps/Release according to the current single-config
Ninja build. It deploys the test executable, libOpenXLSXd.dll (Debug) or
libOpenXLSX.dll (Release), plus matching runtime DLLs found in the pinned
libzip, pugixml, Boost, zlib, compression and crypto package directories.
Configuration-specific DLL folders are never mixed. No PowerShell is used.

CTest runs the executable with the intended DLL search path. The executable
creates an XLSX file in its build/bin directory, verifies typed data and
optional cells, explicitly saves, reopens, verifies actual persisted caption
width and alignment and date formatting, then removes the test file.
One passing OpenXlsxGridRoundtrip test indicates success.

If OpenXLSXConfig.cmake or an imported .lib reference is missing, the
BuildEngine package must first be installed correctly. Do not disguise
a broken package with a global include or DLL path.
