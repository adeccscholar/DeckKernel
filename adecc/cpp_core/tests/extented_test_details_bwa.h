// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file extented_test_details_bwa.h
\brief Typed monthly business-analysis value definitions and date helpers for accounting tests.

\details
Defines the type list and accumulator structures used to represent monthly revenue, expense, result, and
posting counts. The remaining helpers extract calendar components from the std::chrono-based core date type.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Practical Example: Financial Data as a Typed Data Flow".
- "Tests as Architectural Proof".
- "Data Movement Between Source and Sink".
- "The Core Belongs in C++".

\see ../ARCHITECTURE.md#tests-as-architectural-proof

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

#include "database.h"
#include "type_lists.h"
#include "fixed_numeric.h"
#include "convert_fixed.h"

#include "extented_test_types.h"
#include "extented_test_helper.h"

#include <iostream>
#include <string>
#include <string_view>
#include <map>
#include <vector>
#include <ranges>
#include <algorithm>
#include <numeric>
#include <print>

namespace extented_test {

   using monthly_bwa_list = adecc::defined_type_list<
      int,                 // Year
      int,                 // Month
      adecc::money_ty,     // Revenue
      adecc::money_ty,     // Expense
      adecc::money_ty,     // Result
      int                  // Number of postings
      >;

   using monthly_bwa_ty = typename monthly_bwa_list::type_list;

   struct monthly_bwa_accu_ty {
      adecc::money_ty mRevenue {};
      adecc::money_ty mExpense {};
      int iPostingCount {};
      };

   struct monthly_bwa_effect_ty {
      int iYear {};
      int iMonth {};
      adecc::money_ty mRevenueEffect {};
      adecc::money_ty mExpenseEffect {};
      };

   [[nodiscard]] inline int YearOf(adecc::date_ty const& aDate) {
	  return static_cast<int>(aDate.year());
	  }

   [[nodiscard]] inline int MonthOf(adecc::date_ty const& aDate) {
      return static_cast<int>(static_cast<unsigned>(aDate.month()));
   }



} // namespace extented_test
