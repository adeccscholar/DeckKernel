# First connection test

This directory contains the first end-to-end DeckKernel infrastructure test.
It validates the complete path from Scryfall bulk data through the adecc database
abstraction into PostgreSQL and back into the text-grid abstraction.

The PostgreSQL server must be prepared first. See:

```text
test\POSTGRESQL_SETUP.md
```

## From git pull to program start

Run all commands from the DeckKernel repository root in a C++Builder Developer
Command Prompt.

### 1. Update the repository

```bat
git pull
```

### 2. Bootstrap the locked ThirdParty stack

```bat
cmake -P bootstrap\Bootstrap.cmake
```

The bootstrap provides the pinned bcc64x ThirdParty packages and project-local
Ninja installation used by this test.

### 3. Configure Debug

```bat
cmake -S test\firstconnect -B test\firstconnect\build\Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

### 4. Build Debug

```bat
cmake --build test\firstconnect\build\Debug
```

### 5. Install Debug

Do not start the executable directly from the build directory. The install step
creates one flat runnable directory containing the executable and its runtime DLLs.

For Ninja this project is single-configuration. The CMake build type is now bound
directly to the BuildEngine ThirdParty configuration, so a Debug build can only
resolve and install DLLs from ThirdParty/.../bin/win64/Debug, while Release uses
the corresponding Release directories. If an older build tree was configured with
the wrong build type, delete that build directory and configure it again.

```bat
cmake --install test\firstconnect\build\Debug --prefix test\firstconnect\install\Debug
```

### 6. Start Debug

```bat
test\firstconnect\install\Debug\deckkernel_scryfall_postgres_test.exe
```

## Release build

Configure, build and install Release separately:

```bat
cmake -S test\firstconnect -B test\firstconnect\build\Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build test\firstconnect\build\Release
cmake --install test\firstconnect\build\Release --prefix test\firstconnect\install\Release
```

Start it with:

```bat
test\firstconnect\install\Release\deckkernel_scryfall_postgres_test.exe
```

## What the test does

The executable is structured into four explicit, independently timed processes:

1. **Load** resolves Scryfall `default_cards` metadata and downloads or reuses the same-day bulk file.
2. **Parse** reads the bulk file and creates the typed Scryfall card and set data.
3. **Store** ensures the test tables exist, truncates them and writes the parsed data through adecc output sinks.
4. **Evaluate** reads the newest 20 cards through the typed database range and sends them through both text-grid paths.

The PostgreSQL connection uses integrated Windows SSPI authentication.

The query deliberately aliases the two selected `name` columns as
`card_name` and `set_name`. The PostgreSQL adapter resolves result fields by
column name; selecting both columns simply as `name` made both tuple elements
resolve to the first result column.

## Expected PostgreSQL defaults

```text
host            localhost
port            5432
database        DeckKernel
database role   deckkernel_user
integrated      true
sslmode         prefer
gssencmode      disable
krbsrvname      postgres
```

Environment overrides remain available:

```text
DECKKERNEL_PGHOST
DECKKERNEL_PGPORT
DECKKERNEL_PGDATABASE
DECKKERNEL_PGUSER
DECKKERNEL_PGPASSWORD
DECKKERNEL_PG_INTEGRATED
DECKKERNEL_PGSSLMODE
DECKKERNEL_PG_GSSENCMODE
DECKKERNEL_PG_GSSLIB
DECKKERNEL_PG_KRBSRVNAME
```

## Files

- `scryfall_postgres_test.cpp`: four timed processes: load, parse, store and evaluate.
- `scryfall_model.h`: persistent Scryfall data definitions, metadata, selectors and manipulators.
- `schema.sql`: equivalent test schema/table DDL for inspection or manual setup.
- `CMakeLists.txt`: standalone Debug/Release build and install entry point.
- `adecc\\postgre\\pqxx_database.h`: reusable PostgreSQL/libpqxx adapter.


## Why the example is written this way

This program is intentionally more explicit than a minimal smoke test. It is the first
functional example for other developers and therefore shows the technical boundaries
rather than hiding them.

The four processes make the data flow visible:

```text
Scryfall HTTPS
   |
   v
Load
   |
   | local gzip/JSONL file
   v
Parse
   |
   | typed set/card tuples
   v
Store
   |
   | PostgreSQL tables
   v
Evaluate
   |
   v
typed database range -> text grid
```

The JSON bulk format repeats set data in every card record. During parsing the example
normalizes that external representation: sets are deduplicated by `set_id`, while every
card printing keeps `set_id` as its foreign key. The database therefore receives data
that already has a clear relational shape.

## RAII and raw pointers

Owned resources use RAII wherever the third-party API allows it:

- libcurl easy handles are owned by `std::unique_ptr` with a custom deleter;
- libcurl header lists are owned by a dedicated RAII class;
- gzip streams are owned by `std::unique_ptr` with a zlib `gzclose` deleter;
- files are owned by `std::ofstream` / `std::ifstream`;
- PostgreSQL connections and transactions are owned by the adapter and transaction guards.

Raw pointers remain only at C API boundaries. libcurl callback signatures require
`char*` and `void*`, `curl_easy_setopt` consumes C handles, `std::getenv` returns a
borrowed C string, and `curl_version_info` returns a library-owned borrowed structure.
The source comments mark these cases and state whether ownership is transferred. No
owning raw pointer is intentionally exposed by the example.

## Third-party responsibilities

- **curl**: HTTPS requests, redirects, response callbacks and Scryfall headers.
- **OpenSSL**: TLS backend used by curl. Peer and hostname verification stay enabled.
- **nlohmann/json**: JSON metadata and one-card-per-line JSONL parsing.
- **zlib**: direct streaming read of Scryfall's single gzip-compressed JSONL payload.
- **libpq/libpqxx**: PostgreSQL transport behind the adecc adapter.
- **libarchive**: not used by this test. The payload is a gzip stream, not an archive with
  multiple members, so using zlib directly keeps the first example smaller and clearer.

The source documents the relevant third-party calls at the call site, including callback
parameters, borrowed pointers and lifetime assumptions.


## Lesson source structure

The example is split so each technical step can be discussed independently:

    scryfall_load.cpp       HTTPS, curl/OpenSSL, local cache
            |
            v
    scryfall_parse.cpp      zlib, JSONL, normalization
            |
            v
    scryfall_database.cpp   PostgreSQL, transaction, output sinks
            |
            v
    scryfall_postgres_test.cpp
                            composition, timing, evaluation/grid output

The data model remains in scryfall_model.h. The Scryfall field and domain reference is
documented in SCRYFALL_DATA_MODEL.md.
