# adecc OpenXLSX tools

This physical OpenXLSX 0.5.1 adapter implements the backend contract of
adecc::grid::GridModel; cpp_core remains backend-neutral. WorkbookSession
owns XLDocument using RAII; OpenXLSXWorkbook opens or creates a workbook,
creates missing worksheets and offers explicitly selected GridRegion views.
Conversions and optional semantics live at the Excel boundary, while
ranges, iterators, row proxies and sinks remain in cpp_core.

Saving is explicit. See [the build guide](../../Docs/build/OPENXLSX_GRID.md).

SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
