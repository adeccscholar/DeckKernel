# OpenXLSX Grid test

This directory contains the standalone BCC64X/C++23 OpenXLSX Grid test,
including its CMake build and install targets.

The [complete build guide](../../Docs/build/OPENXLSX_GRID.md) explains the
motivation, workbook/region semantics, setup, Debug/Release commands,
installation into apps/{configuration}, and validation of saved XLSX data.

## Inspect the generated workbook

Run `apps/Release/adecc_openxlsx_grid_test.exe` or its Debug equivalent
after installation. The test prints the full path to `OpenXlsxGrid_Example.xlsx`
and **retains** the file beside the executable. Open it in Excel to inspect
sheet `Ledger`, C5:G6. CTest independently retains its workbook below
`test/OpenXlsxGrid/build/<configuration>/bin`.
