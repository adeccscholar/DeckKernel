# adecc libraries — licensing and reuse

The reusable header-only adecc C++ library is being separated from DeckKernel
into **adecc-core-lib**. The license follows the **covered adecc-owned source**,
not the directory name or the checkout location.

## Public, noncommercial route

The publicly available adecc-owned sources are provided under
**PolyForm Noncommercial License 1.0.0**. This route does **not** require
buying a book and does not grant general commercial usage rights.

## Optional adecc Book License

A legitimate purchaser of **Rethinking C++ (C++ neu denken)** can designate
one developer to use the covered revision under the supplemental
[adecc Book License](ADECC_BOOK_LICENSE.md).

This additional license is intentionally MIT-like: the eligible developer
may use, modify, combine and redistribute the code in proprietary and
commercial software, subject to preserving the required notices.
The book-purchase qualification makes this a **custom permissive license,
not the standard MIT License**.

The source is educational material, not a certified production component.
There is no production warranty or fitness assurance. Users must evaluate
security, correctness and suitability themselves.

## Repository boundaries

- `adecc-core-lib`: the independent, covered source distribution.
- `DeckKernel/adecc`: expected Git submodule checkout of that repository.
- Other DeckKernel source: governed by DeckKernel's own license.
- Third-party headers/libraries: governed by their respective upstream terms.

Changing the Git checkout or CMake include path does not change coverage.

## Source notices

Preserve the license documents and original copyright notices. For custom
licenses do not claim the official MIT SPDX identifier. A file may refer to
the public license using:

```text
SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
```

Where a supplemental book-license reference is necessary, refer readers to
`ADECC_BOOK_LICENSE.md` without inventing an SPDX-approved identifier.
Do not automatically rewrite all existing file headers merely to change
their checkout path.

## Attribution example

```text
Uses components of the adecc C++ libraries.
Copyright (c) adecc Systemhaus GmbH.
See the accompanying adecc license notices.
```

The book license remains a **draft pending legal review**, especially around
book purchaser eligibility and downstream redistribution.
