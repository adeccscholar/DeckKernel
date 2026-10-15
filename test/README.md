# DeckKernel tests

The test sources remain below `test/`, while preparation and build instructions are kept
in the central documentation tree.

## PostgreSQL preparation

```text
Docs\preparation\POSTGRESQL_SETUP.md
```

Administrative SQL used by that setup:

```text
test\postgresql_sspi_setup.sql
```

## First connection

Sources:

```text
test\firstconnect
```

Motivation, build, install, run and verification guide:

```text
Docs\build\FIRST_CONNECTION_TEST.md
```

## OpenXLSX Grid

Sources: test/OpenXlsxGrid; adapter: adecc/openXlsxTools.

Motivation, build, install and verification: Docs/build/OPENXLSX_GRID.md.

## PoDoFo PDF booking reader

Sources: test/PoDoFoBooking. Manual PoDoFo stack and offline diagnostics:
Docs/build/PODOFO_BOOKING.md.
