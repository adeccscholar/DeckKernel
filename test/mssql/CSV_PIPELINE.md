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
8. A second `WriteGridModel<OStreamGridBackend<>>` renders twelve sample booking/person rows.
   `adecc::Transform` projects the SQL tuples first into domain types (`Iban`, `VatId_DE`, `InvoiceDate`),
   then into display strings through existing `ConvertTo`/stringify specializations. The only explicit
   `BindMapping` resolves salutation IDs via a reverse `std::map<int, std::string>`. The grid consumes
   the typed display range directly; invoice dates use European formatting.

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
