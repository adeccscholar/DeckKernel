# adecc libraries in DeckKernel

The `adecc/` directory is the licensing boundary for reusable first-party adecc C++ libraries used by DeckKernel.

## Public use

Sources below this directory are publicly available under the DeckKernel project's:

**PolyForm Noncommercial License 1.0.0**

They may therefore be used, studied, modified, and redistributed for permitted non-commercial purposes according to that licence.

## Additional proprietary-use licence

Eligible purchasers of:

**Rethinking C++ (C++ neu denken)**

receive an additional licence for the DeckKernel-owned source code below this directory.

The complete supplemental terms are defined in:

```text
ADECC_BOOK_LICENSE.md
```

The additional licence permits eligible developers to use the covered adecc sources in proprietary software, subject to its conditions.

Use under the supplemental licence requires attribution to the **adecc C++ libraries**. The acknowledgement can be placed in an `About` dialog, legal-notice page, documentation, `THIRD_PARTY_NOTICES`, or a comparable location.

Recommended wording:

```text
This product uses software components from the adecc C++ libraries.
Copyright (c) adecc Systemhaus GmbH.
Used under the adecc Book License associated with "Rethinking C++ (C++ neu denken)".
```

The covered adecc sources are written primarily for education, professional development, experimentation, and reusable example-oriented library use. They are provided **"AS IS"**, without warranty of any kind, to the maximum extent permitted by applicable law. Anyone using them in proprietary or production software remains responsible for review, testing, validation, security, and suitability for the intended purpose.

The directory boundary is intentional: no per-file component list is required.

## Not included

The supplemental adecc licence does not apply to:

- DeckKernel code outside `adecc/`,
- third-party source code,
- third-party libraries,
- Scryfall data,
- Magic: The Gathering intellectual property.

Third-party material always remains under its respective upstream terms.

## Recommended source header

```cpp
/*
   Copyright (c) adecc

   Public license:
      PolyForm Noncommercial License 1.0.0

   Additional license:
      Eligible purchasers of "Rethinking C++ (C++ neu denken)" may use
      this file under the supplemental terms in ADECC_BOOK_LICENSE.md.

   Attribution is required for use under the supplemental license.
   The software is provided "AS IS", without warranty of any kind.

   This additional license applies only because this source file is part
   of the adecc/ source tree.

   SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
*/
```

The header is intentionally concise. The complete licence documents remain authoritative.
