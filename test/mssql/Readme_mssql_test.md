# MS SQL Server ODBC backend test

## Purpose

This test exercises the Microsoft SQL Server implementation in
`adecc/mssql/mssql_odbc_database.h` through the public logical database API from
`adecc/cpp_core/database.h`.

The test is intentionally an integration test. It requires a reachable SQL Server,
Microsoft ODBC Driver 18 for SQL Server, and a database to which the selected login can
connect. The test creates uniquely named test tables, uses them for the test run, and
removes them afterwards.

The test verifies the physical ODBC implementation without exposing ODBC details to
application code.

## Configure and build

Run the commands from the DeckKernel repository root in a C++Builder Developer Command
Prompt.

Prepare the repository tools once:

```cmd
cmake -P bootstrap\Bootstrap.cmake
```

Configure the test:

```cmd
cmake -S test\mssql -B test\mssql\build -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

Build the test:

```cmd
cmake --build test\mssql\build
```

Install the test to `apps\Debug`:

```cmd
cmake --install test\mssql\build
```

For a Release configuration the destination is `apps\Release`. No DeckKernel
third-party runtime DLLs are required by this test because the ODBC driver is provided
by the operating system installation.

Run with Windows integrated authentication:

```cmd
apps\Debug\adecc_mssql_odbc_test.exe --server localhost --database Person_Test --integrated
```

The `--integrated` option is the default and may be omitted:

```cmd
apps\Debug\adecc_mssql_odbc_test.exe --server localhost --database Person_Test
```

Run with SQL Server authentication:

```cmd
apps\Debug\adecc_mssql_odbc_test.exe --server localhost --database Person_Test --sql-login --user <user> --password <password>
```

The same values can be supplied through `ADECC_MSSQL_SERVER`,
`ADECC_MSSQL_DATABASE`, `ADECC_MSSQL_INTEGRATED`, `ADECC_MSSQL_USER`, and
`ADECC_MSSQL_PASSWORD`.

## Tests performed

The test checks:

- connection establishment through Microsoft ODBC Driver 18;
- creation and automatic cleanup of the test schema objects;
- SQL Server `IDENTITY(1,1)` handling;
- `SCOPE_IDENTITY()` remaining correct in the presence of a trigger with its own
  identity column;
- `std::string` through the narrow ODBC path;
- `std::wstring`, `std::wstring_view`, and `wchar_t const*` through the wide
  ODBC path, including non-ASCII Unicode data;
- nullable values;
- `decimal` to `money_ty` and `double`;
- `datetime`, `datetime2`, `date`, and `time`;
- `bit`;
- repeated logical named parameters such as `:Id` being supplied once and bound to
  multiple physical ODBC parameters;
- automatic transaction rollback;
- committed transactions;
- server-side constraint errors and preservation of complete ODBC diagnostics.

The final intentional UNIQUE constraint violation is expected. It verifies that the
exception contains the ODBC operation, SQLSTATE, native SQL Server error number, driver
message, and the mapping from physical ODBC parameters back to the logical adecc
parameter names.

## Diagnostic text

Database exception text is stored as UTF-8 in `std::string`. This is a diagnostic
contract only and does not make ordinary application `std::string` values UTF-8.

The SQL Server backend retrieves ODBC diagnostic records with the wide ODBC API and
converts them to UTF-8. The test writes diagnostic samples through
`adecc::diagnostic::WriteUtf8`. On an attached Windows console this converts UTF-8 to
UTF-16 and uses the wide console API; redirected output remains UTF-8.

This allows localized SQL Server messages, object names, and wide parameter values to be
shown without depending on the active OEM or ANSI console code page.
