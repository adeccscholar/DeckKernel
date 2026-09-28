# DeckKernel

> **Documentation navigation:** [PostgreSQL setup](POSTGRESQL_SETUP.md) ·
> [Documentation server](DOCU_SERVER.md)


> **Where cards meet code — and generations meet around both.**

DeckKernel is a non-commercial learning and community project built around a simple idea: use a real card game, real data, and real software-engineering problems to explore modern C++ together.

The project started from conversations with my sons, who play **Magic: The Gathering** and brought the idea to me. That creates an interesting meeting point between generations: they bring the game, the cards, the deck ideas, and the questions that matter to players; I can use the same project to show what I actually do professionally as a software architect and developer.

That is the spirit of DeckKernel.

It is not intended to become a commercial product. There is no financial interest behind the project. Instead, it should be something that card players and programmers can build, discuss, question, test, and improve together.

For card players, the project should remain connected to the real game and to practical questions around cards, decks, collections, probabilities, and analysis.

For programmers, it should be a substantial learning project using modern C++, networking, APIs, databases, data modelling, compression, security, testing, simulation, and later possibly the interaction between a native C++ backend and completely different frontends.

And ideally, both groups meet somewhere in the middle.

---

## What is DeckKernel?

DeckKernel is an experimental, source-available C++ project for working with card and deck data from **Magic: The Gathering**, initially with a strong focus on Commander.

The project uses the **Scryfall API and Scryfall Bulk Data** as its primary external source for card metadata.

It is intentionally not designed as one large finished application from the beginning.

We will build it step by step.

Each stage should introduce a real technical or domain problem:

- retrieving and updating card data,
- understanding the Scryfall data model,
- storing structured data locally,
- importing decks,
- analysing mana and card roles,
- working with probabilities,
- simulating game situations,
- comparing deck revisions,
- dealing with collections,
- sharing data between systems,
- exposing backend functionality to other applications,
- and eventually connecting different frontends to the same C++ core.

The journey is at least as important as the final program.

---

## A project for players and programmers

DeckKernel deliberately connects two worlds.

```mermaid
flowchart LR
    A[Card Players] --> C[DeckKernel]
    B[Programmers] --> C

    A --> A1[Cards]
    A --> A2[Decks]
    A --> A3[Commander]
    A --> A4[Collections]
    A --> A5[Game Questions]

    B --> B1[Modern C++]
    B --> B2[Databases]
    B --> B3[Networking]
    B --> B4[Architecture]
    B --> B5[Algorithms]

    C --> D[Shared Experiments]
    C --> E[Discussion]
    C --> F[Learning]
    C --> G[Useful Tools]
```

A player does not need to be a C++ expert to contribute a useful idea.

A programmer does not need to know every Magic rule before contributing to the architecture.

Questions from both sides are valuable.

Examples include:

- Why does this deck frequently miss its fourth land?
- How many coloured mana sources are actually required?
- How should different printings of one card be represented?
- What should be stored locally and what should be requested from Scryfall?
- When is SQLite sufficient and when does PostgreSQL become useful?
- How can simulation results be reproduced?
- How can a desktop application later expose the same functionality to a browser or phone?
- How do we keep the architecture understandable while the project grows?

These are exactly the kinds of questions DeckKernel is intended to explore.

---

## Development in public

DeckKernel will be developed openly.

A significant part of the development will happen during live streams. The streams should show real software development rather than only polished final results.

That includes successful ideas, failed ideas, design discussions, compiler problems, performance questions, refactoring, database decisions, and changing assumptions.

We hope that people will participate with:

- ideas,
- questions,
- criticism,
- card-game knowledge,
- test scenarios,
- deck examples,
- architecture discussions,
- C++ suggestions,
- database experience,
- and completely different perspectives.

The project should grow from concrete use cases and discussion rather than from a fixed feature catalogue written before we have learned enough.

---

## Non-commercial by design

DeckKernel is intentionally a **non-commercial project**.

The goal is learning, experimentation, discussion, and community participation.

The source code should be visible so that people can inspect it, learn from it, modify it for permitted non-commercial purposes, and discuss how it works.

The public project is therefore licensed under the:

**PolyForm Noncommercial License 1.0.0**

https://polyformproject.org/licenses/noncommercial/1.0.0/

The public license does not grant permission for commercial use.

In particular, publishing the source code is not intended to allow unrelated third parties to turn DeckKernel, modified DeckKernel versions, or DeckKernel-derived software into a commercial product or commercial service.

DeckKernel application code remains subject to this non-commercial project model.

There is one deliberately separated first-party library area:

```text
adecc/
```

Reusable adecc library sources placed below this directory remain available publicly under the project's non-commercial license, but may additionally be used under the separate **adecc Book License** by eligible purchasers of **Rethinking C++ (C++ neu denken)**.

This supplemental license applies only to DeckKernel-owned source code located below `adecc/`. It does **not** extend to the DeckKernel application as a whole, DeckKernel-specific domain logic outside that directory, third-party libraries, Scryfall data, or Magic: The Gathering intellectual property.

The supplemental terms are documented separately in:

```text
ADECC_BOOK_LICENSE.md
```

Because of the commercial-use restriction on the public project, DeckKernel should be described as **source-available**, not as OSI-approved open-source software.

Third-party libraries remain under their own licenses.

---

## Magic: The Gathering and Scryfall

**Magic: The Gathering** is created and published by Wizards of the Coast.

DeckKernel is an independent fan and learning project.

It is not approved, endorsed, sponsored, or affiliated with Wizards of the Coast or Scryfall.

The project uses the **Scryfall API** and **Scryfall Bulk Data** to work with Magic card metadata.

Scryfall is an important part of the project because it gives us something extremely valuable for a learning application: a large, real, evolving, structured dataset connected to an actual game and an active community.

DeckKernel does not relicense Magic content, Scryfall content, card artwork, card text, symbols, trademarks, or other third-party intellectual property under the DeckKernel software license.

Where practical, the repository should contain project-owned source code, schemas, tests, and documentation rather than copies of card data or artwork.

Runtime or development data should be obtained through the interfaces and downloads provided by Scryfall.

Relevant references:

- Wizards of the Coast Fan Content Policy  
  https://company.wizards.com/en/legal/fancontentpolicy

- Scryfall  
  https://scryfall.com/

- Scryfall API  
  https://scryfall.com/docs/api

- Scryfall Bulk Data  
  https://scryfall.com/docs/api/bulk-data

---

## Responsible Scryfall usage

DeckKernel should avoid unnecessary traffic against Scryfall.

For larger datasets, the preferred approach is:

```mermaid
flowchart LR
    A[Scryfall Bulk Data] --> B[Download]
    B --> C[Local Import]
    C --> D[Local Database]
    D --> E[DeckKernel Analysis]

    F[Scryfall API] --> G[Targeted Requests]
    G --> E
```

Bulk Data should be used whenever large-scale local processing is appropriate.

Direct API requests should be cached where useful and must follow Scryfall's current access requirements.

The implementation should also identify itself with meaningful HTTP headers and respect Scryfall's published rate limits and service guidance.

---

## Age requirement

**DeckKernel is intended for users aged 13 and older.**

This is a project requirement because the application uses Scryfall-backed online services and Magic-related online content.

Local law, parental rules, or the terms of an external service may require additional restrictions or a higher minimum age.

DeckKernel is not intended for children under 13.

---

## Why C++Builder 13 Community Edition?

One goal of DeckKernel is to make modern C++ visible and approachable.

The primary Windows development environment is therefore:

### Embarcadero C++Builder 13 Community Edition

C++Builder 13 CE is available from Embarcadero for eligible users and provides the modern Win64 Clang-based toolchain with substantial C++23 support.

That makes it interesting for this project for two reasons.

First, DeckKernel can use modern C++ language and library features instead of teaching an intentionally outdated subset of the language.

Second, the Community Edition makes it possible for eligible students, hobby developers, freelancers, and small teams to follow the project without first purchasing a professional C++Builder license.

For DeckKernel, **C++Builder 13 CE is the leading development environment**, but it is not intended to become a technical boundary around the project.

C++Builder Community Edition:

https://www.embarcadero.com/products/cbuilder/starter

Community Edition FAQ:

https://www.embarcadero.com/products/delphi/starter/faq

C++Builder itself is subject to Embarcadero's own license and eligibility requirements. The DeckKernel license grants no rights to C++Builder.

### Preparing the development environment

Before DeckKernel is configured or built for the first time, the repository bootstrap must be run from a **C++Builder Developer Command Prompt**.

The bootstrap resolves the active RAD Studio / BCC64X and CMake installations, records their paths for later CMake use, provisions the required third-party packages, and prepares the project-local CMake package integration.

DeckKernel's CMake/BCC64X workflow also requires **Ninja**. A separate manual Ninja installation is not required: the bootstrap uses the pinned project version and, when necessary, downloads and verifies the official Windows x64 Ninja release automatically. Internet access is therefore required for the initial bootstrap unless all required archives are already cached.

The prerequisites and the complete one-command setup are documented in:

**[bootstrap_readme.md](../bootstrap_readme.md)**

The intended first setup is:

```bat
cmake -P bootstrap\Bootstrap.cmake
```

This bootstrap step is part of the required development setup; contributors should run it before configuring or building DeckKernel.

---

## C++ as a common foundation

C++ is a standardized programming language with implementations on most relevant desktop, server, embedded, and infrastructure platforms.

That matters for DeckKernel.

The project is not intended to be tied conceptually to one operating system, one vendor, or one compiler. C++Builder 13 CE leads the project because it is the environment we want to use, demonstrate, and promote, but the larger goal is to keep as much of the code base as possible in **standard C++**.

Large parts of modern software are built directly in C++ or rely on C and C++ libraries underneath higher-level languages, frameworks, runtimes, databases, browsers, game engines, networking stacks, compression libraries, cryptographic libraries, and operating-system components.

Many libraries that appear to belong to other language ecosystems ultimately depend on native C or C++ implementations, wrap them, bind to them, or port their algorithms and architecture.

That makes C++ especially interesting for a learning project like DeckKernel:

> It sits close enough to the platform to expose real systems engineering, while still being standardized and portable enough to build substantial cross-platform software.

DeckKernel should make that visible in practice rather than only state it as a theoretical advantage.

---

## From an ecosystem project into the real world

DeckKernel also continues work from an earlier project.

In that project, a broad set of important open-source C and C++ libraries was made available for the modern C++Builder toolchain.

The intention was larger than simply getting libraries to compile.

It was about demonstrating that the current C++Builder toolchain can once again participate seriously in the wider C and C++ ecosystem and that important upstream projects can be built, tested, integrated, and used with it.

Those libraries now become part of the foundation of DeckKernel.

This gives the new project an additional purpose:

> DeckKernel should provide evidence in the real world that these libraries are not only buildable, but actually usable together in a substantial application.

Networking, TLS, JSON, XML, compression, archive handling, databases, HTTP, asynchronous I/O, and later server functionality are not artificial showcase features here. They are needed by the application itself.

That makes DeckKernel a practical integration test across a broad part of the native C++ ecosystem.

```mermaid
flowchart LR
    A[Open-source C and C++ ecosystem] --> B[Ported and integrated libraries]
    B --> C[C++Builder 13 CE]
    B --> D[Other conforming C++ toolchains]

    C --> E[DeckKernel]
    D --> E

    E --> F[Desktop Application]
    E --> G[Analysis Core]
    E --> H[Database Access]
    E --> I[Networking]
    E --> J[Possible Server]
```

---

## C++Builder leads, standard C++ keeps the door open

The project should make a clear distinction between **the preferred toolchain** and **the portable architecture**.

C++Builder 13 CE is the leading environment for DeckKernel.

It is the environment used in the streams, the environment against which the project is developed first, and the environment we explicitly want to show as a modern and serious C++ development platform.

But DeckKernel should not close the door to other compilers or platforms.

Where functionality can be expressed in standard C++, it should be expressed in standard C++.

Where platform-specific code is necessary, it should be isolated behind narrow interfaces.

For example:

```mermaid
flowchart TB
    CORE[Standard C++ Core]

    CORE --> NET[Networking Abstraction]
    CORE --> DB[Database Abstraction]
    CORE --> SEC[Security / Certificate Abstraction]
    CORE --> FS[Platform Services]

    SEC --> WIN[Windows Certificate Store]
    SEC --> UNIX[Linux / Unix Certificate Handling]
    SEC --> OTHER[Other Platform Backend]

    NET --> ASIO[Boost.Asio / Beast]
    NET --> CURL[libcurl]
```

A Windows-specific implementation for certificate handling may be the first implementation.

That should not mean that the rest of the application depends on Windows.

The same interface can later be implemented for another operating system, another TLS integration, or another compiler environment.

The same principle applies to other platform-dependent services.

This is important because portability is not achieved by avoiding platform capabilities. It is achieved by **containing them**.

---

## Standard C++ first, replaceable platform layers where necessary

The intended architectural direction is therefore:

- domain logic in standard C++,
- data models in standard C++,
- analysis and simulation in standard C++,
- networking logic on portable libraries where practical,
- database access through replaceable backends,
- platform-specific functionality behind explicit interfaces,
- user interfaces allowed to differ by platform,
- server components designed so they can be built independently of the desktop frontend.

A possible future structure is:

```mermaid
flowchart TB
    DOMAIN[Standard C++ Domain Model]
    ANALYSIS[Standard C++ Analysis]
    SIM[Standard C++ Simulation]
    SERVICES[Standard C++ Services]

    DOMAIN --> SERVICES
    ANALYSIS --> SERVICES
    SIM --> SERVICES

    SERVICES --> PLATFORM[Platform Interfaces]

    PLATFORM --> WIN[Windows Implementation]
    PLATFORM --> LINUX[Linux Implementation]
    PLATFORM --> OTHER[Other Platform Implementation]

    SERVICES --> DESKTOP[C++Builder Desktop Frontend]
    SERVICES --> SERVER[Portable C++ Server]
    SERVER --> WEB[Web Frontend]
    SERVER --> MOBILE[Mobile Frontend]
```

This is not a promise that every platform will be supported immediately.

It is a design goal that keeps such support possible.

---

## Why the open-source libraries matter

The third-party libraries used by DeckKernel are not merely conveniences.

They are part of the lesson.

Boost.Asio, Boost.Beast, libcurl, OpenSSL, PostgreSQL, SQLite, zlib, libarchive, nlohmann/json, pugixml, and the other libraries in the stack represent mature pieces of the wider C and C++ ecosystem.

Many of them form infrastructure used directly or indirectly by software written in many other languages.

Using them inside DeckKernel has several educational advantages:

- we can learn where abstraction boundaries really work,
- we can see how portable libraries interact with platform APIs,
- we can study build systems and dependency management,
- we can compare different implementation strategies,
- we can measure performance instead of guessing,
- we can observe transitive dependencies,
- we can discuss licensing using real software,
- and we can demonstrate that a modern C++ application is usually part of an ecosystem rather than an isolated executable.

The point is not to use as many libraries as possible.

The point is to understand why each one exists, where it belongs, and how it behaves in a real application.

---

## Learning across generations

There is another kind of portability in DeckKernel that matters just as much as compiler portability.

Knowledge should move between generations.

My sons bring a game that matters to them, with its own language, strategies, habits, communities, and problems.

I bring decades of experience with programming, architecture, databases, systems, and C++.

Neither side owns the whole project.

That creates the interesting part.

A deck can become a database problem.

A question about mana can become a probability problem.

A card search can become an API and indexing problem.

A collection can become a data-modelling problem.

A game night can create a simulation question.

A website can become a discussion about backends, frontends, protocols, and security.

And a compiler can suddenly become relevant to someone who originally only wanted to understand why a deck behaves the way it does.

That is where DeckKernel should be fun.

The project should have enough technical depth to remain interesting for experienced developers, but enough connection to the real game that players can recognise their own questions in it.

Ideally, people should be able to enter the project from very different directions and still find something familiar.

```mermaid
flowchart LR
    GAME[Fun with the Game] --> PROJECT[DeckKernel]
    CODE[Fun with Programming] --> PROJECT
    FAMILY[Generations Learning Together] --> PROJECT
    COMMUNITY[Community Ideas] --> PROJECT

    PROJECT --> LEARN[Learn]
    PROJECT --> BUILD[Build]
    PROJECT --> DISCUSS[Discuss]
    PROJECT --> EXPERIMENT[Experiment]
```

That combination is intentional.

DeckKernel should be serious software engineering without losing the reason the project exists in the first place:

**because building something together around a game should be interesting and fun.**

---

## Books behind the project

DeckKernel is also connected to two book projects.

They are not required in order to understand or participate in DeckKernel, but they provide a deeper treatment of ideas that appear in the project.

### Rethinking C++ (C++ neu denken)

**Rethinking C++ (C++ neu denken)** is planned for publication in the coming weeks.

The book grows out of work around modern C++, architecture, compiler behaviour, reusable libraries, C++Builder 13, and the integration of established open-source projects.

Parts of the reusable adecc libraries used by DeckKernel originate from, were refined during, or were tested as part of the C++Builder 13 work and the preparation of this book.

DeckKernel gives these components something that isolated compiler tests cannot provide:

> use in a real application, in combination with networking, databases, compression, security, structured data, and later possibly a server.

The book explores many of the C++ concepts behind those components in greater depth. DeckKernel, in turn, can show how those ideas behave when they meet a real-world domain and have to work together.

The licensing boundary is deliberately simple:

```mermaid
flowchart TB
    DK[DeckKernel Repository]

    DK --> APP[DeckKernel application and project-specific code]
    DK --> ADECC["adecc/ reusable library sources"]
    DK --> THIRD[Third-party libraries]

    APP --> PNC[PolyForm Noncommercial 1.0.0]
    ADECC --> PNC2[PolyForm Noncommercial 1.0.0]
    ADECC --> BOOK["Additional adecc Book License for eligible readers of Rethinking C++"]
    THIRD --> UPSTREAM[Respective upstream licenses]
```

For public use, the adecc sources remain available under the DeckKernel non-commercial license.

A legitimate purchaser of **Rethinking C++ (C++ neu denken)** may additionally receive the rights defined in `ADECC_BOOK_LICENSE.md`, including use of the eligible `adecc/` sources in proprietary software.

Use under the supplemental licence requires attribution to the **adecc C++ libraries** and is provided on an explicit **"AS IS"** basis without warranty, to the maximum extent permitted by applicable law. This reflects the educational, professional-development, and experimental origin of these reusable components; production users remain responsible for their own review, testing, validation, and security.

This additional license does not turn DeckKernel itself into a commercially reusable code base. Its scope is defined by the directory boundary.

### The Structure of Information (Die Struktur der Information)

A second planned book, **The Structure of Information (Die Struktur der Information)**, goes deeper into another area that DeckKernel will increasingly encounter: data modelling, identity, relations, persistence, database architecture, and the distinction between information itself and the systems used to store it.

DeckKernel is a particularly useful domain for these questions.

A card application quickly has to distinguish between concepts such as:

- a conceptual card,
- a specific printing,
- language and finish,
- external identifiers,
- a physical card in a collection,
- a card slot in a deck,
- versions of a deck,
- analysis results,
- and historical states.

Likewise, using SQLite locally and PostgreSQL for shared or server-backed data raises questions that are broader than SQL syntax or a particular database API.

**The Structure of Information (Die Struktur der Information)** is intended to discuss these concepts in greater depth.

Together, the two books and DeckKernel form three different perspectives on the same work:

```mermaid
flowchart LR
    CPP["Rethinking C++\n(C++ neu denken)"] --> DK[DeckKernel]
    INFO["The Structure of Information\n(Die Struktur der Information)"] --> DK

    CPP --> C1[Language and Architecture]
    CPP --> C2[Reusable C++ Components]
    CPP --> C3[Toolchains and Ecosystem]

    INFO --> I1[Information Models]
    INFO --> I2[Identity and Relations]
    INFO --> I3[Databases and Persistence]

    DK --> REAL[Real-world Application]
    REAL --> TEST[Ideas Tested in Practice]
```

The books can go deeper into concepts than a README or stream can.

DeckKernel can test whether those concepts remain useful in practice.

And the project community can challenge both with new questions.

---

## What we want to learn

DeckKernel is deliberately broad enough to connect multiple areas of software development.

### Modern C++

The C++ core should use modern language and library facilities where they improve the design.

Possible topics include:

- C++23,
- concepts,
- ranges,
- templates,
- compile-time programming,
- RAII,
- value semantics,
- coroutines,
- asynchronous processing,
- parallel analysis,
- type-safe domain modelling,
- testing,
- benchmarking,
- and performance measurement.

### Networking

Scryfall gives us a practical reason to work with real network communication.

The project can compare and use technologies such as:

- Boost.Asio,
- Boost.Beast,
- curl / libcurl,
- TLS,
- OpenSSL,
- REST-style APIs,
- caching,
- and eventually our own service endpoints.

### Data formats

Real systems rarely use only one format.

DeckKernel will initially rely heavily on JSON, but XML and other formats may appear where useful.

Libraries currently considered or already integrated include:

- nlohmann/json,
- pugixml,
- and project-owned serialization logic where appropriate.

### Databases

Cards, printings, decks, collections, analyses, and experiments give us a meaningful database problem rather than an artificial tutorial schema.

The project is expected to use:

- **SQLite** for local, self-contained storage,
- **PostgreSQL** for shared, server-backed, or larger installations.

This gives us an opportunity to compare embedded and client/server database architectures using the same domain.

---

## From desktop application to backend and frontends

The first steps are intentionally local.

The long-term architecture, however, should not assume that the user interface and the analysis engine must always live in the same process.

One possible development direction is:

```mermaid
flowchart TB
    S[Scryfall API / Bulk Data]

    S --> K[DeckKernel C++ Core]

    K --> SQ[SQLite]
    K --> PG[PostgreSQL]

    K --> D[Native Desktop Frontend]

    K --> API[DeckKernel Server API]

    API --> W[JavaScript Web Frontend]
    API --> M[Mobile Frontend]
    API --> O[Other Clients]

    MD[Own Markdown Content / Rendering Logic] --> API
```

This is a direction, not a promise that every component will be built immediately.

The important architectural idea is that the domain model, analysis, simulation, and data access should remain useful independently of one particular user interface.

A native C++ desktop application may therefore be the first frontend rather than the final boundary of the system.

Later, a DeckKernel server could expose selected functionality to:

- JavaScript applications,
- browser interfaces,
- mobile clients,
- educational demonstrations,
- dashboards,
- or other software.

---

## Our own Markdown-based server content

Later in the project we plan to provide a server that uses our **own Markdown processing and presentation logic**.

The goal is not merely to host static documentation.

Markdown can become part of a lightweight content system for:

- project documentation,
- analysis reports,
- tutorials,
- card and deck explanations,
- experiment results,
- streamed development notes,
- technical articles,
- and potentially interactive content connected to the DeckKernel backend.

This also gives us another real engineering topic:

> How do native backend services, structured data, generated analysis, Markdown content, and arbitrary frontends work together cleanly?

---

## Planned architecture

The exact architecture will evolve as the project grows.

A current high-level direction is:

```mermaid
flowchart TB
    SF[Scryfall]

    subgraph External_Data[External Data]
        API[Scryfall API]
        BULK[Scryfall Bulk Data]
    end

    SF --> API
    SF --> BULK

    API --> NET[Networking Layer]
    BULK --> IMP[Bulk Import]

    NET --> DOMAIN[DeckKernel Domain Model]
    IMP --> DOMAIN

    DOMAIN --> DATA[Persistence Layer]

    DATA --> SQLITE[SQLite]
    DATA --> POSTGRES[PostgreSQL]

    DOMAIN --> ANALYSIS[Analysis Engine]
    DOMAIN --> SIM[Simulation Engine]

    ANALYSIS --> APP[Application Services]
    SIM --> APP

    APP --> DESKTOP[Native Desktop UI]
    APP --> SERVER[DeckKernel Server]

    SERVER --> WEB[Web / JavaScript]
    SERVER --> MOBILE[Mobile]
    SERVER --> OTHER[Other Frontends]
```

The first versions will be much smaller than this diagram.

That is intentional.

---

## Step-by-step development

A possible progression is:

```mermaid
flowchart LR
    A[Card Data] --> B[Local Database]
    B --> C[Search]
    C --> D[Deck Import]
    D --> E[Deck Analysis]
    E --> F[Simulation]
    F --> G[Collections]
    G --> H[Shared Database]
    H --> I[Server API]
    I --> J[Additional Frontends]
```

Each stage should remain usable and understandable on its own.

We explicitly do not want to build a large speculative framework before the earlier steps have demonstrated that the abstractions are useful.

---

## Third-party software

DeckKernel uses or may use a number of third-party libraries.

The library stack is deliberately broad enough to support a real application, while still being accessible to people following the project.

Each third-party component remains under its **own license**.

The DeckKernel license does not replace, restrict, or relicense third-party software.

The following table is an architectural overview and does not replace the license files of the individual projects.

| Component | Intended use | License |
|---|---|---|
| Boost.Asio | asynchronous I/O, networking, timers | Boost Software License 1.0 |
| Boost.Beast | HTTP and WebSocket building blocks | Boost Software License 1.0 |
| curl / libcurl | HTTP(S) transfers and interoperability | curl license |
| OpenSSL 3.x | TLS and cryptographic support | Apache License 2.0 |
| nlohmann/json | JSON parsing and serialization | MIT License |
| pugixml | optional XML parsing and serialization | MIT License |
| SQLite | local embedded database | Public Domain |
| PostgreSQL / libpq | PostgreSQL client and server integration | PostgreSQL License |
| libpqxx | optional C++ PostgreSQL client wrapper if used | BSD 3-Clause |
| zlib | DEFLATE compression | zlib License |
| bzip2 / libbzip2 | bzip2 compression | bzip2 License |
| XZ Utils / liblzma | XZ and LZMA compression | liblzma: 0BSD; additional XZ Utils files may use other licenses |
| Zstandard / zstd | Zstandard compression | BSD-style license or GPLv2 option; DeckKernel distributions should use the permissive BSD licensing path |
| Brotli | Brotli compression | MIT License |
| libzip | ZIP archive access | BSD 3-Clause |
| libarchive | multi-format archive access | predominantly BSD-style licensing; individual files control |

---

## Compression and archive stack

Archive and compression support is useful beyond packaging.

It may be required for:

- downloaded datasets,
- local caches,
- imports,
- exports,
- backups,
- test data,
- server-side content,
- and future distribution formats.

The current or planned stack includes:

```mermaid
flowchart TB
    A[DeckKernel Archive / Compression Layer]

    A --> Z[zlib]
    A --> BZ[bzip2]
    A --> XZ[XZ / liblzma]
    A --> ZS[Zstandard]
    A --> BR[Brotli]
    A --> LZ[libzip]
    A --> LA[libarchive]
```

Not every build has to use every library directly.

Some components may be introduced transitively through other dependencies or enabled only by specific build options.

---

## Transitive dependencies

A README can never be the final authority for the dependency closure of an actual binary.

Dependencies may change with:

- platform,
- compiler,
- configuration,
- enabled features,
- static or dynamic linkage,
- database backend,
- TLS backend,
- and archive-format support.

Therefore:

1. the actual dependency closure of a DeckKernel build is authoritative,
2. every distributable build must retain all notices required by its resolved dependencies,
3. `THIRD_PARTY_NOTICES.md` or an equivalent generated notice document should be created from the actual build configuration,
4. optional functionality must not silently introduce an incompatible license,
5. direct and transitive dependencies should remain reproducible through the build system.

---

## License separation

The project contains several legally separate layers.

```mermaid
flowchart TB
    A[DeckKernel Source Code]
    B[Third-party Libraries]
    C[Scryfall Data and Services]
    D[Magic IP]

    A --> A1[PolyForm Noncommercial 1.0.0]
    B --> B1[Respective upstream licenses]
    C --> C1[Scryfall terms and policies]
    D --> D1[Wizards of the Coast and other rights holders]
```

No statement in the DeckKernel repository should be interpreted as granting rights to third-party intellectual property.

---

## Current project philosophy

DeckKernel should prefer:

- transparent algorithms over unexplained scores,
- reproducible calculations over opaque recommendations,
- measurable behaviour over assumptions,
- local processing where practical,
- explicit data provenance,
- clear separation between external data and project-owned code,
- incremental development over speculative architecture,
- modern C++ over artificially simplified teaching code,
- understandable architecture over unnecessary abstraction,
- and discussion over pretending that every design decision is obvious.

Where DeckKernel produces recommendations or analytical results, the long-term goal is to make both the input data and the reasoning inspectable.

---

## Possible future areas

The project may eventually explore:

- Commander deck analysis,
- probability calculations,
- mana-base analysis,
- mulligan models,
- deck revision comparison,
- simulation,
- collection management,
- new-set impact analysis,
- card substitution experiments,
- PostgreSQL-backed shared data,
- server APIs,
- browser frontends,
- mobile clients,
- Markdown-based project and analysis content,
- and other ideas contributed by the community.

These are possibilities rather than a fixed roadmap.

The project should remain free to change direction when we learn something better.

---

## Contributing ideas

At the beginning, the most valuable contributions may not be code.

Useful contributions include:

- real deck examples,
- questions existing tools do not answer well,
- interesting Commander situations,
- test cases,
- expected calculations,
- database-model suggestions,
- architecture criticism,
- performance ideas,
- UI ideas,
- documentation,
- and discussion.

A separate contribution policy may be added before substantial third-party source contributions are accepted.

---

## Disclaimer

DeckKernel is experimental software and is provided without warranty.

Card data, legality information, rulings, prices, external APIs, and other third-party information may change.

DeckKernel must not be treated as an authoritative replacement for current information from Wizards of the Coast, Scryfall, tournament organizers, retailers, or other primary sources.

Price information, if added later, is informational only.

---

## References

### Magic and Scryfall

- Wizards of the Coast Fan Content Policy  
  https://company.wizards.com/en/legal/fancontentpolicy

- Scryfall  
  https://scryfall.com/

- Scryfall API  
  https://scryfall.com/docs/api

- Scryfall Bulk Data  
  https://scryfall.com/docs/api/bulk-data

### Development environment

- Embarcadero C++Builder Community Edition  
  https://www.embarcadero.com/products/cbuilder/starter

- Community Edition FAQ  
  https://www.embarcadero.com/products/delphi/starter/faq

### License

- PolyForm Noncommercial License 1.0.0  
  https://polyformproject.org/licenses/noncommercial/1.0.0/

- adecc Book License  
  See `ADECC_BOOK_LICENSE.md`

### Related books

- **Rethinking C++ (C++ neu denken)** — planned publication
- **The Structure of Information (Die Struktur der Information)** — planned publication

### Selected third parties

- Boost  
  https://www.boost.org/

- curl  
  https://curl.se/

- OpenSSL  
  https://openssl-library.org/

- nlohmann/json  
  https://github.com/nlohmann/json

- SQLite  
  https://sqlite.org/

- PostgreSQL  
  https://www.postgresql.org/

- libpqxx  
  https://pqxx.org/libpqxx/

- pugixml  
  https://pugixml.org/

- libarchive  
  https://www.libarchive.org/

---

## Final note

DeckKernel begins with a card game, but it is really about something larger.

It is about taking a real-world domain seriously enough to model it properly.

It is about showing that modern C++ can be used for something understandable and visible outside the usual systems-programming examples.

It is about databases, APIs, probability, architecture, frontends, and all the connections between them.

It is about players explaining the game to programmers and programmers explaining the software to players.

And, on a personal level, it began because my sons brought their game to me and asked whether we could build something around it.

That makes DeckKernel a good place for generations to meet as well.

If that sounds interesting, join the discussion.
