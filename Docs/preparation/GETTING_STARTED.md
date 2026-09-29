# Preparing DeckKernel documentation and the first PostgreSQL test

[TOC|Getting started]

This document is the entry point for preparing a fresh DeckKernel checkout so that the
project-owned Markdown documentation can be viewed through the DeckKernel documentation
server and the first PostgreSQL/Scryfall test can be built and run.

The intended workflow is:

```text
git clone / git pull
      |
      v
bootstrap locked native ThirdParty packages
      |
      v
build and install documentation server
      |
      v
open project documentation in browser
      |
      +--> PostgreSQL server preparation
      |
      +--> first-connect build and test instructions
```

The browser-side documentation assets are prepared by the project bootstrap and written
to the generated `Docs/js` directory. That directory is intentionally not tracked by Git.

The detailed bootstrap contract, including the native ThirdParty packages, tools and
browser assets, is documented centrally in:

[DeckKernel bootstrap](BOOTSTRAP.md)

The native Markdown parser `cmark-gfm` remains part of the locked native ThirdParty
stack because it is a compiled C library used by the documentation server.

---

## 1. Requirements

Use a **C++Builder 13 Developer Command Prompt**.

The repository bootstrap expects the C++Builder environment, especially `BDS`, to be
available. It also resolves the project-local Ninja installation and the locked
BuildEngine ThirdParty packages.

Git is required to obtain and update the repository. No additional package manager is
required for the documentation server.

---

## 2. Update the repository

From the DeckKernel repository root:

```cmd
git pull
```

For a new checkout, clone the repository first and then change into its root directory.

---

## 3. Run the project bootstrap

Run:

```cmd
cmake -P bootstrap\Bootstrap.cmake
```

This is the single preparation entry point. The implementation and all generated
directories are described in:

[DeckKernel bootstrap](BOOTSTRAP.md)

After the bootstrap, the documentation server has one shared browser-asset directory:

```text
Docs/
   js/
   images/
```

There is no Debug/Release copy of the browser assets.

---

## 4. Build the documentation server

### Debug

Configure:

```cmd
cmake -S src\docu_server -B src\docu_server\build\Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

Build:

```cmd
cmake --build src\docu_server\build\Debug
```

Install:

```cmd
cmake --install src\docu_server\build\Debug
```

The runnable application is installed below:

```text
apps\Debug\
```

Start it with:

```cmd
apps\Debug\deckkernel_docu_server.exe
```

Open:

[DeckKernel documentation](http://127.0.0.1:8770/docs/)

### Release

```cmd
cmake -S src\docu_server -B src\docu_server\build\Release -G Ninja -DCMAKE_BUILD_TYPE=Release
```

Build:

```cmd
cmake --build src\docu_server\build\Release
```

Install:

```cmd
cmake --install src\docu_server\build\Release
```

Start:

```cmd
apps\Release\deckkernel_docu_server.exe
```

---

## 5. What the documentation server provides

The server renders the Markdown files below `Docs/` and provides:

- GitHub-Flavored Markdown through cmark-gfm;
- source-code syntax highlighting through highlight.js;
- Mermaid diagrams;
- MathJax formulas;
- local document assets;
- browser-side printing after Mermaid and MathJax rendering has completed.

All browser assets are served locally from the generated `Docs/js` tree. After the
project bootstrap has prepared that directory, rendering does not depend on a CDN.

For implementation details see:

[Documentation server](../build/DOCU_SERVER.md)

For licensing details of cmark-gfm and the browser-side assets see:

[Documentation ThirdParty](../licenses/DOCUMENTATION_THIRDPARTY.md)

---

## 6. Prepare PostgreSQL

The first functional DeckKernel test stores Scryfall data in PostgreSQL.

The complete Windows/SSPI setup is documented here:

[PostgreSQL setup](POSTGRESQL_SETUP.md)

That guide covers:

- creation of the `DeckKernel` database;
- creation of `deckkernel_user`;
- the `deckkernel_test` schema;
- SSPI mapping through `pg_ident.conf`;
- `pg_hba.conf`;
- independent verification with `psql`.

Complete this preparation before running the first-connect application.

---

## 7. Build and run the first functional test

The complete lesson and test description is here:

[First connection test](../build/FIRST_CONNECTION_TEST.md)

The short Debug sequence is:

```cmd
cmake -S test\firstconnect -B test\firstconnect\build\Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

```cmd
cmake --build test\firstconnect\build\Debug
```

```cmd
cmake --install test\firstconnect\build\Debug
```

```cmd
apps\Debug\deckkernel_scryfall_postgres_test.exe
```

The test covers the complete path:

```text
Scryfall bulk metadata
      |
      v
download/cache
      |
      v
JSON/JSONL parsing
      |
      v
typed DeckKernel model
      |
      v
adecc database abstraction
      |
      v
libpqxx / libpq
      |
      v
PostgreSQL
      |
      v
query and text-grid evaluation
```

The conceptual Scryfall/card-game model is documented centrally in:

[Scryfall data model and design analysis](../model/SCRYFALL_DATA_MODEL.md)

---

## 8. Recommended order for a fresh checkout

Use this sequence:

Update:

```cmd
git pull
```

Bootstrap:

```cmd
cmake -P bootstrap\Bootstrap.cmake
```

Configure:

```cmd
cmake -S src\docu_server -B src\docu_server\build\Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

Build:

```cmd
cmake --build src\docu_server\build\Debug
```

Install:

```cmd
cmake --install src\docu_server\build\Debug
```

Start:

```cmd
apps\Debug\deckkernel_docu_server.exe
```

Then use the running documentation server to follow:

1. the PostgreSQL setup;
2. the first-connect build;
3. the Scryfall data-model documentation;
4. the adecc licensing rules;
5. later DeckKernel design documentation.

This makes the project documentation itself the navigation layer for the remaining setup
and development work.
