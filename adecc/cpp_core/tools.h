// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file tools.h
\brief Small generic utility helpers for ownership and range-based text output.

\details
Contains lightweight helpers that do not justify a larger subsystem, including unique_ptr creation with a
custom deleter and algorithm-based writing of string-convertible ranges. The functions remain
framework-independent and composable with standard C++.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Library Instead of Framework".
- "The Cost Model of Abstraction".
- "Efficiency Is Responsibility".
- "The Core Belongs in C++".

\see ARCHITECTURE.md#cost-model-and-efficiency

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

#include <memory>
#include <string>
#include <concepts>
#include <ranges>


namespace adecc {

template<typename ty, typename Deleter, typename... CtorArgs>
std::unique_ptr<ty, Deleter> make_unique_with_deleter(Deleter deleter, CtorArgs&&... args) {
   return std::unique_ptr<ty, Deleter>{ new ty { std::forward<CtorArgs>(args)... }, std::move(deleter) };
   }



template <std::ranges::input_range RangeT>
	requires std::convertible_to<std::ranges::range_value_t<RangeT>, std::string>
void writeLines(std::ostream& os, RangeT const& theRange) {
   auto theView = theRange | std::views::transform([](auto const & elem) { return std::string(elem); });
   std::ostream_iterator<std::string> it{os, "\n"};
   std::ranges::copy(theView, it);
   return;
   }

} // namespace adecc
