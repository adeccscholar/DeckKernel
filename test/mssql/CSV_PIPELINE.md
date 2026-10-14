# CSV → Transformation → Microsoft SQL Server

**adecc Scholar teaching example:** typed data sources, reusable mapping policies,
`PersistentSystemData` metadata and a SQL Server sink.

## Processing

1. The built-in `adecc::FromCsvFileTyped<false, true, csv_file_types>`
   reads the semicolon-delimited CSV as a typed generator of domain values (`EuroAmount`, `InvoiceDate`); no custom parser or file-existence precheck is required.
2. Salutations (**Herr**, **Frau**, **Firma**, **Divers**) and booking kinds
   (**Rechnung**, **Gutschrift**, **Storno**, **Lastschrift**) are stored as lookup tables using
   `MakeOutputRange`; generated `IDENTITY` keys are collected in maps.
3. The account master data (bank, receivables, payables, revenues and expenses)
   uses `PersistentSystemData` for insert SQL and parameters.
4. `ImportCsvBookings` contains the compact CSV source → transformation → SQL sink flow. A `std::views::transform` pipeline converts CSV tuples using
   `adecc::Transform` and `BindMapping` for four foreign keys only; the domain amount and date values use the existing `ConvertTo` machinery without additional `BindMapping` entries:
   salutation, booking kind, debit account, credit account.
5. The bookings use `PersistentSystemData` and `MakeOutputSink`.
6. The amount is parsed by `cpp_domain` policies (`TrimStringConversionPolicy`, `GermanDecimalToFloatConversionPolicy`, `PositiveNumberPolicy`) and passed through `ConvertTo` into `adecc::money_ty`. The invoice date uses `CaseConversionPolicy` to choose ISO, European or US `ConvertToConversionPolicy`, followed by `ValidYearMonthDayPolicy`, producing `adecc::date_ty`. The build retains `CHRONO_WORK_AROUND` and `CHRONO_MISSED_ZONEDB`; no timezone database is used.
7. SQL Server evaluates the total turnover, verifies debit/credit equality
   and calculates debit, credit, and debit-minus-credit balances for each account.
8. A second `WriteGridModel<OStreamGridBackend<>>` renders the historically filtered
   person rows (`kind_id = 4`, `salutation_id = 3`). The SQL rows are transformed through
   the existing domain policies, retaining the formatted date and amount for text output.
9. An independent, **unfiltered** SQL query reads all persons for the Excel export.
   The XLSX workbook is written beside the installed executable as
   `CsvBookingReport.xlsx`. It contains exactly two worksheets:
   `Summenübersicht` (account balances plus total/count/difference metrics) and
   `Personen` (all imported rows with native Excel date and numeric amount).
10. Both text and Excel share one constrained `FillGrid(Grid&, Rows)` algorithm.
    Only the row projection differs: formatted strings for text, numeric and
    calendar values for Excel. Ranges, iterators and sinks remain in `cpp_core`.

## Example

`bookings.csv` contains **850 deterministic data rows** with this schema:

```csv
reference;name;salutation;kind;debit_account;credit_account;amount_eur;invoice_date;iban;vat_id_de
BU-000001;Anna Weber;Frau;Rechnung;1200;4000;13,73;01.01.2026;DE34370400440000001000;DE123456000
```

Amounts are positive euro values with comma decimals. Dates demonstrate European, US and ISO formats. Debit/credit direction is defined by
the two separate account columns. This avoids introducing locale-dependent
decimal parsing or double-entry ambiguity into the lesson.

## Build and run

```cmd
cmake -S test/mssql -B test/mssql/build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build test/mssql/build/Release --target adecc_mssql_csv_pipeline
set ADECC_MSSQL_SERVER=localhost
set ADECC_MSSQL_DATABASE=Person_Test
cmake --install test/mssql/build/Release
apps\Release\adecc_mssql_csv_pipeline.exe
```

Windows integrated authentication is used. C++Builder, Ninja and the existing
adequately bootstrapped DeckKernel dependencies are required.

**Warning:** This sample deletes and recreates its four dedicated demo tables
(`dbo.AdeccCsvBooking`, `dbo.AdeccCsvAccount`,
`dbo.AdeccCsvSalutation`, `dbo.AdeccCsvKind`). Run it only in a disposable
test database, not in production.

The parser uses the existing `file_as_tuple.h` adapter. Its delimited-file
format is deliberately simple and does not implement all RFC 4180 quoting
rules.

The four demo tables are prepared through a `std::vector<std::string>` of SQL commands executed via `std::for_each`. All code-to-ID mappings share the `mapping_ids_ty` alias.

The demo installs `bookings.csv` next to `adecc_mssql_csv_pipeline.exe` and resolves it relative to the executable directory when no CSV argument is provided. The IBAN field is normalized and validated by `adecc::Iban` (modulo 97); `vat_id_de` is normalized and checksum-validated by `adecc::VatId_DE` from `sepa_policies_hawkers_tray.h`. These are illustrative test identifiers, not real payee identifiers. **The field `vat_id_de` is a German VAT identification number (USt-IdNr.), not a domestic tax number (Steuernummer).**

IBAN storage deliberately uses `Iban::Value()` through a dedicated `BindMapping` for
column 9 (canonical, ungrouped string). The read path constructs `adecc::Iban` again
and then uses its stringify policy via `ConvertTo<std::string>` to display grouped
IBANs. This keeps persisted values independent of presentation formatting.

## XLSX result and verification

After installation, running `apps\\Release\\adecc_mssql_csv_pipeline.exe`
creates `apps/Release/CsvBookingReport.xlsx` and prints the absolute path.
Open it in Excel to inspect the two worksheets. `Summenübersicht` contains
all account debit, credit, and balance figures in columns A:E and the control
metrics in G:H; `Personen` contains the complete booking population, with no
WHERE restriction. The command-line text grid intentionally keeps the original
filtered sample. The Excel Amount column is numeric and the invoice date uses
an Excel date cell, not a formatted string. The program verifies the unfiltered
person count against the imported count before exporting, then reopens the
saved workbook to validate its worksheet headers and last person.

When repeating the example, older reports remain intact. The next available
filename is used (CsvBookingReport.xlsx, CsvBookingReport_1.xlsx, etc.), so
existing workbooks may remain open in Excel. The export is performed
**after the database transaction has committed**; an XLSX write failure cannot
roll back the already completed SQL import. This sample still drops and
recreates its dedicated demo SQL tables, so use only a disposable test database.

The CMake project resolves OpenXLSX 0.5.1 exclusively through the pinned
DeckKernel bootstrap package, links `OpenXLSX::OpenXLSX`, and installs the
actually imported Debug/Release DLLs from the recursive runtime dependency
analysis, rather than copying entire Boost and other package folders.
The active C++Builder bin64/bin directories are also searched and only required
compiler runtime DLLs such as libc++-370.dll and libunwind-370.dll are copied;
Windows system DLLs are not copied. The install component is `csv_pipeline`. To install just this executable and its runtime dependencies:

```cmd
cmake --install test/mssql/build/Release --component csv_pipeline
```

## Declarative Excel cell formats

Excel captions add an optional fourth `ExcelColumnFormat` value: invoice date
`dd.mm.yyyy` and amount / account totals `#,##0.00 "EUR"`. Both columns keep
their native typed values. `set_captions()` prepares styles, and the shared
workbook session caches number formats and styles across both worksheets.
The exported report is re-opened and validates that date and amount styles
are distinct and that the requested format codes survived serialization.

## Dynamic Excel row styling

The account and person Excel grids use `ExcelGridStyle` for a fixed bold,
blue heading and dynamic conditional-format row patterns. Formats apply only
to written data rectangles. The SQL generator, transformations and grid sink
remain unchanged. The saved person-range rules are verified after reopening.
