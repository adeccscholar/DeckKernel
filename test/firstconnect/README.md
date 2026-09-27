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
creates the runnable directory and copies the required configuration-specific
runtime DLLs.

```bat
cmake --install test\firstconnect\build\Debug --prefix test\firstconnect\install\Debug
```

### 6. Start Debug

```bat
test\firstconnect\install\Debug\bin\deckkernel_scryfall_postgres_test.exe
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
test\firstconnect\install\Release\bin\deckkernel_scryfall_postgres_test.exe
```

## What the test does

The test:

1. resolves Scryfall `default_cards` bulk metadata over HTTPS;
2. uses a same-day cached bulk file when requested;
3. parses the cards and set information;
4. connects to PostgreSQL using integrated Windows SSPI authentication;
5. creates the test tables and indexes inside the pre-provisioned
   `deckkernel_test` schema when necessary;
6. truncates and reloads the test data;
7. reads the newest 20 cards through the typed adecc database range;
8. sends the result through both text-grid paths.

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

- `scryfall_postgres_test.cpp`: end-to-end test.
- `postgres_pqxx_database.h`: current PostgreSQL/libpqxx adapter candidate.
- `schema.sql`: equivalent test schema/table DDL for inspection or manual setup.
- `CMakeLists.txt`: standalone Debug/Release build and install entry point.
