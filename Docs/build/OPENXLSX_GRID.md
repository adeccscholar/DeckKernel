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
Ninja build. It installs the executable plus only its **actual recursive DLL
imports**, determined with CMake file(GET_RUNTIME_DEPENDENCIES) from the built
program. DLLs are resolved within the pinned Debug/Release ThirdParty stack;
unused Boost components are not copied. The resolver also searches the
actual C++Builder compiler directory and the BDS bin64/bin directories;
required compiler runtime DLLs (for example libc++-370.dll and
libunwind-370.dll) are copied selectively. Windows system DLLs are not
copied. Previously installed DLLs are not automatically deleted because
other tests may depend on them. CTest uses the same search directories.
No PowerShell is used.

CTest runs the executable with the intended DLL search path. The executable
creates **OpenXlsxGrid_Example.xlsx beside the executable**, verifies typed data
and optional cells, explicitly saves, reopens, and verifies the actual persisted
caption width, alignment and date formatting. The finished XLSX file is
**retained**, not deleted. CTest writes to its own build/<configuration>/bin
directory; invoking the installed program writes to apps/Debug or apps/Release.

Open the workbook in Excel and inspect worksheet `Ledger`, cells C5:G6:
captions in row 5 and the typed data in row 6. The date is stored using
the Excel date number format, the Amount column is right aligned, and
the final numeric Amount is 47.5 after the freeze-guard update.

The executable prints a [PASS] line for each completed verification and
a final [FILE] line with the absolute path. A nonzero exit indicates failure.
Repeated runs create OpenXlsxGrid_Example.xlsx, then
OpenXlsxGrid_Example_1.xlsx, and so on. Older files remain untouched and
may stay open in Excel.

One passing OpenXlsxGridRoundtrip test indicates success.

If OpenXLSXConfig.cmake or an imported .lib reference is missing, the
BuildEngine package must first be installed correctly. Do not disguise
a broken package with a global include or DLL path.

## Variadic captions and reusable Excel styles

The first three caption values remain title, width and horizontal alignment.
OpenXLSX accepts an optional fourth value `ExcelColumnFormat` with an Excel
number-format code (for example `dd.mm.yyyy` or `#,##0.00 "EUR"`).
The text backend ignores this additional parameter. `set_captions()` registers
number formats and caches column styles in the shared `WorkbookSession`, reused
across worksheets. Existing XML cell styles are cloned before modification.
Native numeric values therefore remain numbers: no date format may leak into
a money column. The roundtrip test checks distinct persisted date and amount
formats after re-opening the workbook.

## Dynamic Excel grid styling

`ExcelGridStyle` is optional. Captions, typed values and `FillGrid()` stay unchanged.
The header uses permanent cell formatting. A row pattern becomes Excel
conditional formatting restricted to the actual written data rectangle.
The style is evaluated by Excel after sorting, with no per-row colors stored.

```cpp
using namespace adecc::openxlsx_grid;
ExcelGridStyle style;
style.header({.background = RGB{34,79,119}, .foreground = RGB{255,255,255}, .bold = true})
     .row_pattern({{.background = RGB{255,255,255}},
                   {.background = RGB{235,242,249}}});
grid.set_captions(captions);
grid.set_style(style);
FillGrid(grid, rows);
book.save();
```

One pattern element colors all rows. Two alternate; four can represent A,A,B,B.
An empty pattern adds no data styling. Rules are finalized at save(), when the
actual row count is known. Excel rows inserted outside the saved rectangle are
not automatically included; the result is not an auto-expanding Excel Table.

## Reading worksheet dimensions, typed selected columns and deleting sheets

`OpenXLSXWorkbook::worksheet_rows(name)` returns the *last occupied worksheet
row number*, not the count of business records. `grid_existing(name)`
creates an A1-based read view, taking the first row as captions by default.
For arbitrary offsets, continue to use `grid(name, GridRegion{...})`.
The general `GridModel` already provides `rows()`, `columns()`,
`get<T>(row,column)`, `focus_row`, `focus_cell`, and
`makeRowView<Ts...>({selected_column_indices})`. Grid indices are zero-based.

```cpp
auto grid = book.grid_existing("Personen");
auto amounts = grid.makeRowView<double>({5})
   | std::views::transform([](auto const& row) { return std::get<0>(row); });
double sum = std::ranges::fold_left(amounts, 0.0, std::plus<>{});
```

The MSSQL pipeline now reads back the actual generated file and compares the
person amount fold with the SQL aggregate. It never materializes intermediary
row DTOs. Worksheet management is provided by `sheet_exists`,
`sheet_names`, `add_sheet` and `remove_sheet`. Deleting the final sheet
is prohibited by OpenXLSX; deleting while a grid is alive is prohibited by
the adapter. Opening a path reads a saved XLSX file, not the in-memory Excel
process or its unsaved changes. File sharing and write locks are controlled by
Windows/Excel.

## Existing grid with an offset

`grid_existing(sheet, first_row, first_column, rows = 0, columns = 0,
has_captions = true)` now accepts the same 1-based coordinates as `grid()`.
A `GridRegion` overload is also available. When dimensions are zero, the
adapter discovers consecutive nonempty cells of the starting row and reads
until the first fully empty data row. Sparse tables should specify dimensions.
The existing `grid_existing(sheet)` remains compatible and uses A1.

```cpp
auto ledger = workbook.grid_existing("Ledger", 5, 3); // C5:G6
auto amount = ledger.get<double>(0, 1);             // D6
```
