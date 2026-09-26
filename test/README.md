# Scryfall / PostgreSQL functional test

This directory contains the first end-to-end DeckKernel infrastructure test.

The goal is deliberately narrow: prove that the selected networking, crypto, JSON,
compression, PostgreSQL, and adecc core components work together before building the
actual DeckKernel domain model.

## Tested path

```text
Scryfall bulk-data metadata
        |
        | HTTPS: curl + OpenSSL
        v
default_cards bulk file
        |
        | nlohmann/json + zlib
        v
PersistentSystemData tuples
        |
        | adecc PostgreSQL OutputSink
        v
PostgreSQL
        |
        | typed adecc database range
        v
tuple<string, string>
        |
        +--> direct assignment --> text grid
        |
        +--> ranges::copy --> text-grid sink
```

The small persistent model stores Scryfall set ID, code and name plus printing ID,
Oracle ID, card name, set relation, and printing release date.

## Files

- `scryfall_postgres_test.cpp`: complete end-to-end test.
- `postgres_pqxx_database.h`: current PostgreSQL/libpqxx adapter candidate.
- `schema.sql`: minimal PostgreSQL schema, directly executable in pgAdmin.
- `CMakeLists.txt`: standalone CMake entry point.

The PostgreSQL adapter intentionally remains below `test/` in this first step.
After the test proves the backend contract, it can be moved to its final adapter layer.

## Build

Run the DeckKernel bootstrap from the repository root first:

```bat
cmake -P bootstrap\\Bootstrap.cmake
```

Use separate single-configuration build trees for Debug and Release.

Debug:

```bat
cmake -S test -B test\\build\\Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build test\\build\\Debug
```

Release:

```bat
cmake -S test -B test\\build\\Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build test\\build\\Release
```

The executables are written to:

```text
test\\build\\Debug\\bin\\deckkernel_scryfall_postgres_test.exe
test\\build\\Release\\bin\\deckkernel_scryfall_postgres_test.exe
```

The locked dependency set already contains curl, OpenSSL, nlohmann/json, libpq,
libpqxx, and zlib.

## Install

The install step creates a runnable directory containing the executable and the
configuration-specific BuildEngine runtime DLLs from curl, OpenSSL, libpq,
libpqxx, and zlib.

Debug:

```bat
cmake --install test\\build\\Debug --prefix test\\install\\Debug
```

Release:

```bat
cmake --install test\\build\\Release --prefix test\\install\\Release
```

The runnable installations are:

```text
test\\install\\Debug\\bin\\
test\\install\\Release\\bin\\
```

Run, for example:

```bat
test\\install\\Debug\\bin\\deckkernel_scryfall_postgres_test.exe
```

The DLLs are copied from the configuration-specific
`ThirdParty/<package>/bin/win64/<Debug|Release>` directories. This keeps Debug
and Release runtime closures separate.

## PostgreSQL configuration

Create an empty PostgreSQL database, normally named `deckkernel_test`.
The executable creates its dedicated schema and tables automatically.
The equivalent DDL is available in `schema.sql` for pgAdmin and inspection.

| Variable | Default |
| --- | --- |
| `DECKKERNEL_PGHOST` | `127.0.0.1` |
| `DECKKERNEL_PGPORT` | `5432` |
| `DECKKERNEL_PGDATABASE` | `DeckKernel` |
| `DECKKERNEL_PGUSER` | `deckkernel_user` |
| `DECKKERNEL_PGPASSWORD` | empty |
| `DECKKERNEL_PGSSLMODE` | `prefer` |
| `DECKKERNEL_PG_INTEGRATED` | true |
| `DECKKERNEL_PG_GSSENCMODE` | `disable` |
| `DECKKERNEL_PG_GSSLIB` | empty, use libpq Windows default |
| `DECKKERNEL_PG_KRBSRVNAME` | `postgres` |

The test prints the OpenSSL version and curl TLS backend before network or database work.

For the concrete local Windows setup, including the PostgreSQL role, pg_hba.conf,
pg_ident.conf, and SSPI verification, see `POSTGRESQL_SSPI.md`.

## Scryfall behavior

The program performs one API request to resolve `default_cards`, followed by one bulk
download. It sends explicit User-Agent and Accept headers and requires TLS 1.2 or newer.

The loader prefers `jsonl_download_uri` when the field is present and supports the
gzip-compressed JSONL representation. It also retains a fallback for an uncompressed
JSON array or uncompressed JSONL file.

## adecc database write path

Both persistent model classes derive from `PersistentSystemData`. Their metadata
produces the INSERT SQL and output-parameter description. The rows are written with
complete range assignment:

```cpp
aSetSink = vecSets;
aCardSink = vecCards;
```

No native libpqxx INSERT is used.

## adecc database read and text-grid path

The newest 20 printings are read through a typed query range:

```cpp
auto rngLatest =
   aDatabase.Execute<std::string, std::string>(...);
```

The first display path transfers the database range directly into the text grid:

```cpp
aGrid = rngLatest;
```

The second path validates the grid output iterator:

```cpp
std::ranges::copy(
   rngLatest,
   aGrid.sink<std::string, std::string>()
);
```

This is the architectural point of the test: the PostgreSQL source and text-grid sink
communicate through typed standard-C++ ranges.

## Deliberate limitations

This is not yet the DeckKernel production schema. Card faces, legality, images, prices,
collector numbers, languages, rulings, decks, and incremental synchronization are
deliberately omitted. The dedicated test tables are truncated and reloaded on every run.
