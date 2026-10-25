# DeckKernel

The complete project documentation lives below `Docs` and is rendered by the DeckKernel
documentation server.

- [Documentation start page](Docs/README.md)
- [Getting started](Docs/preparation/GETTING_STARTED.md)
- [Project vision](Docs/ideas/PROJECT_VISION.md)
- [Scryfall data model](Docs/model/SCRYFALL_DATA_MODEL.md)
- [Documentation server build](Docs/build/DOCU_SERVER.md)
- [First connection build and test](Docs/build/FIRST_CONNECTION_TEST.md)

Local executable deployments are written below `apps/Debug` or `apps/Release`.
The `apps/` directory is intentionally not tracked by Git.


The reusable header-only adecc C++ library is maintained independently in
[adecc-core-lib](https://github.com/adeccscholar/adecc-core-lib), pinned here as
the `adecc-core-lib/` Git submodule. Clone with `--recurse-submodules` or run
`git submodule update --init --recursive` before configuring the examples.
