# DeckKernel documentation

This directory is the single root of the DeckKernel documentation.

DeckKernel is also intended for beginners. Build guides therefore do not only list
commands: they explain **why** a project exists, what it demonstrates, what must be
prepared first, how it is built and how a successful run can be checked.

## Start here

- [Prepare the project and documentation server](preparation/GETTING_STARTED.md)
- [Project motivation and central ideas](ideas/PROJECT_VISION.md)

## Ideas

Concepts, motivation and future directions:

- [Project vision](ideas/PROJECT_VISION.md)

## Model

Domain and data-model documentation:

- [Scryfall and card-game data model](model/SCRYFALL_DATA_MODEL.md)

## Preparation

One-time or environment preparation:

- [Getting started](preparation/GETTING_STARTED.md)
- [DeckKernel bootstrap](preparation/BOOTSTRAP.md)
- [PostgreSQL and SSPI setup](preparation/POSTGRESQL_SETUP.md)

## Build guides

Executable learning projects and how to build, install, run and verify them:

- [Documentation server](build/DOCU_SERVER.md)
- [First connection test](build/FIRST_CONNECTION_TEST.md)

Future applications should receive their own build guide here. Each guide should begin
with the motivation for the application and the concepts it is intended to teach.

## Licenses

Project and documentation dependency rules:

- [adecc license rules](licenses/ADECC_LICENSE_RULES.md)
- [adecc Book License](licenses/ADECC_BOOK_LICENSE.md)
- [Documentation ThirdParty dependencies](licenses/DOCUMENTATION_THIRDPARTY.md)

## Shared document assets

All documentation uses one shared asset layout:

```text
Docs/
   js/
   images/
```

The JavaScript runtimes, syntax-highlighting stylesheet and documentation client script
exist only once below `Docs/js`; there is no Debug/Release duplication.

Images used by Markdown documents belong below `Docs/images`.

The documentation server recursively discovers Markdown files below `Docs`. New
documents placed in the category directories therefore become selectable without adding
a new server route.
