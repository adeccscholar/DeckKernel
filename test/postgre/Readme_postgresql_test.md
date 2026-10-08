# PostgreSQL/libpqxx backend test

## Purpose

This integration test exercises `adecc/postgre/pqxx_database.h` through the same
public logical database API used by the SQL Server test.

It verifies that PostgreSQL-specific behavior stays inside the physical backend while
application code continues to use named adecc parameters, typed result rows, output
ranges, transactions, and the common value types.

The test creates a uniquely named temporary table in the current PostgreSQL session.
No permanent schema objects are required.

## Configure and build

Run the commands from the DeckKernel repository root in a C++Builder Developer Command
Prompt.

Prepare the repository tools once:

```cmd
cmake -P bootstrap\Bootstrap.cmake
```

Configure the test:

```cmd
cmake -S test\postgre -B test\postgre\build -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

Build the test:

```cmd
cmake --build test\postgre\build
```

Install the test and its runtime DLLs to `apps\Debug`:

```cmd
cmake --install test\postgre\build
```

For a Release configuration the destination is `apps\Release`. The install step copies
the executable together with the required libpq, libpqxx, OpenSSL, zlib, Brotli, and zstd
runtime DLLs.

Run with integrated authentication:

```cmd
apps\Debug\adecc_postgresql_pqxx_test.exe --host localhost --database DeckKernel --user deckkernel_user --integrated
```

Run with password authentication:

```cmd
apps\Debug\adecc_postgresql_pqxx_test.exe --host localhost --database DeckKernel --user deckkernel_user --password-login --password <password>
```

The defaults and environment variables match the existing DeckKernel PostgreSQL test
configuration: `DECKKERNEL_PGHOST`, `DECKKERNEL_PGPORT`,
`DECKKERNEL_PGDATABASE`, `DECKKERNEL_PGUSER`, `DECKKERNEL_PGPASSWORD`, and
`DECKKERNEL_PG_INTEGRATED`.

## Tests performed

The test checks:

- connection establishment through libpqxx/libpq;
- PostgreSQL identity handling through `RETURNING`;
- `std::string` as the normal narrow C++ string path;
- `std::wstring`, `std::wstring_view`, and `wchar_t const*` as explicit wide
  values;
- the PostgreSQL adapter's internal UTF-8 transport for wide values;
- nullable string and integer values;
- `numeric` to `money_ty` and `double`;
- `timestamp(0)`, `date`, and `time(0)`;
- boolean values;
- repeated logical named parameters being supplied once and reused as one PostgreSQL
  positional parameter;
- automatic transaction rollback;
- committed transactions;
- preservation of a server-side UNIQUE constraint diagnostic.

Normal application code does not need to encode `std::string` as UTF-8 for the wide
string contract. UTF-8 is used only inside the PostgreSQL backend because libpq/libpqxx
transports text as bytes and the adapter explicitly configures the client connection for
UTF-8.
