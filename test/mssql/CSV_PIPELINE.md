# CSV → Transformation → Microsoft SQL Server

**adecc Scholar teaching example:** typed data sources, reusable mapping policies,
`PersistentSystemData` metadata and a SQL Server sink.

## Processing

1. The built-in `adecc::FromCsvFileTyped<false, true, csv_file_types>`
   reads the semicolon-delimited CSV as a typed generator; no custom parser is required.
2. Salutations (**Herr**, **Frau**, **Firma**) and booking kinds
   (**Rechnung**, **Gutschrift**, **Storno**) are stored as lookup tables using
   `MakeOutputRange`; generated `IDENTITY` keys are collected in maps.
3. The account master data (bank, receivables, payables, revenues and expenses)
   uses `PersistentSystemData` for insert SQL and parameters.
4. A `std::views::transform` pipeline converts CSV tuples using
   `adecc::Transform` and `BindMapping` for four foreign keys:
   salutation, booking kind, debit account, credit account.
5. The bookings use `PersistentSystemData` and `MakeOutputSink`.
6. SQL Server evaluates the total turnover, verifies debit/credit equality
   and calculates debit, credit, and debit-minus-credit balances for each account.

## Example

`bookings.csv` contains **600 deterministic data rows** with this schema:

```csv
reference;name;salutation;kind;debit_account;credit_account;amount_cents
BU-000001;Anna Weber;Frau;Rechnung;1200;4000;1137
```

Amounts are positive integer cents; debit/credit direction is defined by
the two separate account columns. This avoids introducing locale-dependent
decimal parsing or double-entry ambiguity into the lesson.

## Build and run

```cmd
cmake -S test/mssql -B test/mssql/build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build test/mssql/build/Release --target adecc_mssql_csv_pipeline
set ADECC_MSSQL_SERVER=localhost
set ADECC_MSSQL_DATABASE=Person_Test
test\mssql\build\Release\adecc_mssql_csv_pipeline.exe test\mssql\bookings.csv
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
