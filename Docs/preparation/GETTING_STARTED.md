# Preparing DeckKernel documentation and the first PostgreSQL test

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

The browser-side documentation assets are part of the DeckKernel repository itself.
They are not downloaded by the bootstrap. This keeps the documentation server usable from
a complete repository checkout without depending on a CDN or on an additional packaging
step.

The native Markdown parser `cmark-gfm` remains part of the normal locked ThirdParty
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

## 3. Bootstrap the native ThirdParty stack

Run:

```cmd
cmake -P bootstrap\Bootstrap.cmake
```

This prepares the locked native dependencies below `ThirdParty/` and the project-local
build tools below `Cache/`.

The documentation server uses these native packages:

- Boost 1.92.0 for Asio/Beast;
- cmark-gfm 0.29.0.gfm.13 for Markdown parsing.

The browser-side assets are already versioned once in the central documentation tree:

```text
Docs/
   js/
      docu_client.js
      highlight.min.js
      github.min.css
      mermaid.min.js
      mathjax-tex-svg.js
   images/
```

There is no Debug/Release copy of these web assets. Both server configurations serve the
same files directly from `Docs/js`.

Their upstream licence texts are stored below `licenses/`.

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

```text
http://127.0.0.1:8770/docs/
```

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

All browser assets are served locally from the installed application. The rendered
documentation therefore does not depend on a CDN after the repository has been cloned.

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
