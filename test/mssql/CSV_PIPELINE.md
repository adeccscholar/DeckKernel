# CSV → Transformation → MS SQL Server

This independent teaching example demonstrates the **adecc Scholar** building-block idea:
values have a type, policies select the transformations, and ranges connect a source to a sink.

## Flow

1. Read a deliberately simple semicolon-separated CSV as
   `std::vector<csv_types::type_list>`.
2. Store two independent value domains in SQL Server from `std::vector<std::string>`:
   salutations **Herr / Frau / Firma**, booking kinds **Rechnung / Gutschrift / Storno**.
3. `MakeOutputRange` returns the generated SQL Server `IDENTITY` values.
   Store them in `std::map<std::string, long long>`.
4. `std::views::transform` applies `adecc::Transform` with two
   `adecc::BindMapping` policies to resolve the foreign keys.
5. `MakeOutputSink` consumes the transformed range.

The executable uses the existing `adecc::db::mssql` ODBC backend.
It does not issue ODBC calls directly.

## Build and run

From the repository root, with BuildEngine prerequisites already bootstrapped
and an MS SQL Server database available:

```cmd
cmake -S test/mssql -B test/mssql/build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build test/mssql/build/Release --target adecc_mssql_csv_pipeline
set ADECC_MSSQL_SERVER=localhost
set ADECC_MSSQL_DATABASE=Person_Test
test\mssql\build\Release\adecc_mssql_csv_pipeline.exe test\mssql\bookings.csv
```

The actual executable path may be affected by the selected generator's
output directory. Windows integrated authentication is used.

**Warning:** This example *drops and recreates* three fixed-name demo tables:
`dbo.AdeccCsvBooking`, `dbo.AdeccCsvSalutation`, and `dbo.AdeccCsvKind`.
Only run it against a disposable test database, never against production data.
All rows are inserted within one transaction.

## Sample data

See [bookings.csv](bookings.csv). It contains an integer `amount_cents`,
rather than locale-dependent decimal numbers, so the example can focus
on transforming foreign-key domains, not number parsing.

The CSV reader intentionally supports only a small subset of CSV:
UTF-8-compatible text, semicolon separators, no escaped separators or quotes.
A production CSV parser would add quoting and encoding policies without
changing the typed transformation and database sink.
