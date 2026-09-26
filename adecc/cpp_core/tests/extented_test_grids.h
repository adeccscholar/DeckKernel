// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file extented_test_grids.h
\brief Column and formatting metadata for textual projections of the extended accounting tests.

\details
Defines captions, widths, and alignments for account, posting, and balance views. These declarations
deliberately describe presentation only; the underlying accounting values remain in the typed core data
structures.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "The Grid as a Projection, Not as Truth".
- "Grid Models as the Next Step".
- "Practical Example: Financial Data as a Typed Data Flow".
- "Tests as Architectural Proof".

\see ../ARCHITECTURE.md#grid-projection-and-ui-boundaries

\version 1.0
\date 26.09.2026
\author Volker Hillmann (adecc Systemhaus GmbH)

\copyright Copyright © 2021 - 2026 adecc Systemhaus GmbH

\licenseblock{LicenseRef-PolyForm-Noncommercial-1.0.0}
This file is licensed under the PolyForm Noncommercial License 1.0.0.
Use, modification, and distribution are permitted only as defined by that license.
The complete and controlling terms are available at
https://polyformproject.org/licenses/noncommercial/1.0.0/.
Any use not permitted by that license requires separate permission or a separate
license from adecc Systemhaus GmbH.
\endlicenseblock

*/

#pragma once

#include "stream_tools.h"

using namespace std::literals;



inline adecc::vecCaptions<adecc::AnsiStreamPolicy> vecAccountClassesCaps = {
      { "ID",            8, adecc::EAlignmentType::right  },
      { "Name",         20, adecc::EAlignmentType::left  },
      { "Description",  70, adecc::EAlignmentType::left  },
      { "Type Code",    15, adecc::EAlignmentType::left  },
      { "Balance Side", 15, adecc::EAlignmentType::left  }
   };

inline adecc::vecCaptions<adecc::AnsiStreamPolicy> vecAccountCaps = {
      { "ID"s,           8, adecc::EAlignmentType::right  },
      { "Account"s,       10, adecc::EAlignmentType::right },
      { "Account name"s,   50, adecc::EAlignmentType::left },
      { "Account type"s,    10, adecc::EAlignmentType::left },
      { "Account class"s, 12, adecc::EAlignmentType::left },
      { "Currency"s,     10, adecc::EAlignmentType::center },
      { "Active"s,       10, adecc::EAlignmentType::center }
   };

inline adecc::vecCaptions<adecc::AnsiStreamPolicy> vecPostingCaps = {
      { "ID"s,              9, adecc::EAlignmentType::right },
      { "Posting date"s,  14, adecc::EAlignmentType::left },
      { "Document No."s,      11, adecc::EAlignmentType::left },
      { "Debit account"s,      10, adecc::EAlignmentType::right },
      { "Credit account"s,     10, adecc::EAlignmentType::right },
      { "Amount"s,         12, adecc::EAlignmentType::right },
      { "Tax class",    12, adecc::EAlignmentType::left },
      { "Posting text"s,   55, adecc::EAlignmentType::left }
   };

inline adecc::vecCaptions<adecc::AnsiStreamPolicy> vecBalAcctByYearCaps = {
      { "Account"s,          10, adecc::EAlignmentType::right },
      { "Account name",       50, adecc::EAlignmentType::left  },
      { "Account type",        20, adecc::EAlignmentType::left  },
      { "Amount"s,         12, adecc::EAlignmentType::right }
   };
