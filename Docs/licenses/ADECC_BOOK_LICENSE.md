# adecc Book License

[TOC|adecc Book License]

## Supplemental license for purchasers of *Rethinking C++ (C++ neu denken)*

**Status: Draft**

This document defines an additional license for source code located below the `adecc/` directory in the DeckKernel repository.

It is intended to coexist with, not replace, the public DeckKernel license.

This draft should be reviewed before the first published book edition or commercial use under these terms.

---

## 1. Public license remains available

All source code below:

```text
adecc/
```

that is published as part of DeckKernel remains available under the public DeckKernel license:

**PolyForm Noncommercial License 1.0.0**

The public license remains valid independently of this document.

Nothing in this supplemental license removes rights already granted by the public license.

---

## 2. What this supplemental license covers

This supplemental license applies only to DeckKernel-owned source code physically located below:

```text
adecc/
```

in an official DeckKernel source distribution or repository revision.

The directory boundary is the licensing boundary.

No individual component list is required.

Files moved out of `adecc/` are not covered by this supplemental license merely because an earlier revision placed them there.

Files belonging to third parties do not become covered by this license merely because they are stored below `adecc/`. Their original upstream licenses always take precedence.

---

## 3. Related book

The supplemental license is associated with the book:

**Rethinking C++ (C++ neu denken)**

The book discusses and develops concepts that are also used by the reusable adecc C++ libraries.

DeckKernel provides a real-world application in which some of those libraries and concepts are used together.

---

## 4. Eligibility

A person becomes an eligible licensee after legitimately acquiring a new licensed copy of **Rethinking C++ (C++ neu denken)** through the author, publisher, or an authorised sales channel.

Unless a later edition states otherwise, one legitimately acquired copy grants one developer seat.

The purchaser may designate one natural person as the licensed developer.

A company or other legal entity may use the resulting proprietary software when the covered adecc source code is used, modified, or integrated by a developer holding a valid developer seat.

The purchase record, invoice, receipt, publisher record, licence code, or another reasonable proof of legitimate acquisition should be retained as evidence of eligibility.

No permanent online activation is required by this draft.

---

## 5. Additional rights granted

An eligible licensee receives, in addition to the public non-commercial rights, permission to use the covered `adecc/` source code for proprietary and commercial software development.

Subject to the conditions in this document, an eligible licensee may:

- use covered adecc source code in proprietary software,
- modify the covered source code,
- compile it statically or dynamically into proprietary applications or libraries,
- combine it with independently developed proprietary source code,
- distribute compiled applications or libraries containing covered code,
- deploy software containing covered code internally in a commercial organisation,
- use covered code when providing commercial software or services.

These permissions apply only to the covered adecc code and only while all applicable third-party licences are also observed.

Use under this supplemental licence is also subject to the attribution requirement below.

---

## 6. Source redistribution

This supplemental license is intended primarily to permit proprietary **use** of the adecc libraries, not unrestricted relicensing of their source code.

An eligible licensee may distribute modified or unmodified covered source code:

- within the licensee's organisation,
- to contractors working for the licensee under confidentiality obligations,
- as source required to build or maintain the licensee's own product,

provided that recipients may use that source only for the licensee's product or work performed for the licensee.

This supplemental license does not grant a general right to publish the covered adecc source code under a different public source license.

---

## 7. Attribution requirement

Use of the supplemental adecc Book License requires attribution.

A proprietary product that contains, incorporates, or is materially based on covered `adecc/` source code must include a reasonable acknowledgement of the adecc C++ libraries.

The acknowledgement should appear in at least one place normally used for legal, licensing, or third-party notices, for example:

- an `About` dialog,
- a `Legal Notices` page,
- accompanying product documentation,
- a `THIRD_PARTY_NOTICES` or `ACKNOWLEDGEMENTS` file,
- installer or package documentation,
- or another comparable location accessible to recipients of the software.

Recommended wording:

```text
This product uses software components from the adecc C++ libraries.
Copyright (c) adecc Systemhaus GmbH.
Used under the adecc Book License associated with "Rethinking C++ (C++ neu denken)".
```

Reasonable changes to formatting or surrounding text are permitted as long as the acknowledgement remains recognisable and the origin of the covered adecc components is not obscured.

This attribution requirement does **not** require disclosure of proprietary source code, proprietary modifications, trade secrets, or confidential implementation details.

For software used only internally and not distributed outside the licensee's organisation, the attribution may instead be retained in internal legal, dependency, SBOM, or component documentation.

---

## 8. No standalone commercial resale of the adecc libraries

This supplemental license permits the covered source code to be used as part of a proprietary product.

It does not permit an eligible licensee to take the adecc libraries themselves, or a substantially equivalent repackaging of them, and sell or license them as a competing standalone library product.

A separate written agreement is required for:

- standalone commercial redistribution of the adecc libraries,
- sublicensing the covered source as a general-purpose library,
- offering the covered source as part of a competing library or SDK product,
- selling access to substantially unchanged adecc source code.

This restriction does not prevent normal distribution of compiled adecc functionality as an integral part of a larger application or system.

---

## 9. DeckKernel itself is not covered

This supplemental license does **not** grant proprietary or commercial-use rights to DeckKernel as a whole.

In particular, it does not apply to:

- DeckKernel application code outside `adecc/`,
- DeckKernel-specific game or domain logic outside `adecc/`,
- project documentation unless explicitly stated,
- Scryfall data or services,
- Magic: The Gathering intellectual property,
- third-party libraries,
- third-party assets.

Those components remain governed by their respective licences, terms, and rights holders.

---

## 10. Third-party code

The adecc libraries may depend on third-party libraries.

Examples may include Boost, OpenSSL, curl, PostgreSQL, SQLite, libarchive, compression libraries, and other open-source components.

This supplemental license grants no rights to third-party software beyond the rights provided by those third parties.

The licensee remains responsible for complying with all applicable upstream licences.

`THIRD_PARTY_NOTICES.md` provides an overview for DeckKernel builds.

---

## 11. Copyright notices and source headers

Covered adecc source files should identify their licensing status in a concise and machine-readable way.

A recommended source header is:

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

If an SPDX identifier or expression is later registered or standardised for the supplemental license, the header may be updated accordingly.

The source header is informational.

The complete terms in this document and in the public project license govern.

---

## 12. Modifications

Modified versions of covered adecc source code created by an eligible licensee remain permitted for proprietary use under this supplemental license.

The licensee is not required to publish proprietary modifications.

This permission does not change the licensing status of the original public source repository.

---

## 13. Employees, contractors, and teams

One book copy grants one developer seat unless a separate agreement states otherwise.

A licensed developer may use the covered adecc source code in software owned by the developer's employer or client.

Other developers who independently work with or modify the covered source code require their own developer seat or another applicable adecc licence.

Use of compiled binaries by end users does not require each end user to own the book.

---

## 14. Transfer of the book

The supplemental developer right is associated with legitimate ownership of the qualifying book licence or copy.

A physical book may be transferred according to applicable law, but the supplemental developer seat may not be duplicated by transferring the book while retaining the same licensed right.

A future final licence should define the transfer mechanism for physical books, electronic books, replacement copies, and second-hand sales explicitly.

---

## 15. Termination

Rights under this supplemental license terminate if the licensee materially violates its terms and does not cure the violation within a reasonable period after notice, where applicable.

Termination of the supplemental licence does not revoke rights independently available under the public PolyForm Noncommercial License 1.0.0.

---

## 16. Educational purpose, no warranty, and limitation of liability

The covered `adecc/` source code is written and published primarily for education, professional development, technical discussion, experimentation, and reusable example-oriented library use.

THE SOFTWARE IS PROVIDED **"AS IS"**, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING, TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, ANY WARRANTY OR REPRESENTATION OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, CORRECTNESS, RELIABILITY, AVAILABILITY, PERFORMANCE, SECURITY, NON-INFRINGEMENT, OR SUITABILITY FOR PRODUCTION USE.

No statement in **Rethinking C++ (C++ neu denken)**, in DeckKernel documentation, in source comments, in examples, in streams, or in related educational material creates a warranty or guarantee for the covered software.

To the maximum extent permitted by applicable law, the authors, copyright holders, publishers, and licensors shall not be liable for any claim, damages, loss, or other liability arising from, out of, or in connection with the covered software or its use, modification, integration, distribution, or other dealings in the software.

This includes, without limitation and where legally excludable:

- loss or corruption of data,
- loss of profit or revenue,
- business interruption,
- loss of availability,
- consequential or indirect loss,
- security incidents,
- incompatibility with a particular system,
- or damage resulting from assumptions that educational, experimental, or example-oriented code is suitable for a specific production environment.

The licensee remains responsible for reviewing, testing, validating, securing, documenting, and adapting the covered code before using it in proprietary, safety-relevant, security-relevant, or production software.

Nothing in this section excludes, restricts, or limits liability, warranty rights, or other claims where such exclusion, restriction, or limitation is prohibited by applicable law.


---

## 17. Relationship to future editions

Later editions of **Rethinking C++ (C++ neu denken)** may update the supplemental licence.

The licence applicable to a particular developer should be the version associated with the qualifying edition or purchase unless the copyright holder explicitly grants a later version.

The repository should therefore version this document.

Suggested identifier:

```text
adecc Book License 1.0
```

---

## 18. Intent

The intent of this licensing model is straightforward:

- DeckKernel remains a non-commercial learning and community project.
- The reusable adecc libraries can be studied and used non-commercially by everyone under the public project licence.
- Readers who support the associated work by purchasing **Rethinking C++ (C++ neu denken)** receive additional permission to use the reusable adecc sources in their own proprietary software.
- The additional permission is bounded by the clear `adecc/` directory boundary.
- Third-party licences remain untouched.

This model is intended to connect the book, the educational project, and practical professional C++ development without making the entire DeckKernel application commercially reusable.

---

## 19. Legal review

This document is a project licence draft and not legal advice.

Before publication of the final book or reliance on this licence for commercial distribution, the final wording should be reviewed for:

- German and EU contract and copyright law,
- consumer-law implications,
- electronic-book distribution,
- second-hand physical books,
- company purchases and developer-seat assignment,
- transfer and termination rules,
- warranty and liability wording,
- international distribution.
