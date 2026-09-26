// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file fixed_string.h
\brief Compile-time fixed string type suitable for structural and template use.

\details
Stores a character sequence with compile-time size and exposes lightweight conversions to string_view and C
strings. The type provides a small value-level building block for expressing textual information in template
and compile-time contexts.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Programming with Types".
- "Type Sequences and Variadic Structures".
- "Type Lists as Structured Type Relations".
- "The Cost Model of Abstraction".

\see ARCHITECTURE.md#type-space-core-and-boundary

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

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace adecc {

   template<std::size_t uSize>
   struct FixedString {
      char chData[uSize + 1]{};

      constexpr FixedString(char const (&chParam)[uSize + 1]) {
         std::copy_n(chParam, uSize + 1, chData);
      }

      [[nodiscard]] constexpr char const* data() const noexcept {
         return chData;
      }

      [[nodiscard]] static consteval std::size_t size() noexcept {
         return uSize;
      }

      [[nodiscard]] constexpr operator std::string_view() const noexcept {
         return std::string_view{ chData, size() };
      }

      [[nodiscard]] constexpr operator char const* () const noexcept {
         return chData;
      }
   };

   template<std::size_t uArraySize>
   FixedString(char const (&)[uArraySize]) -> FixedString<uArraySize - 1>;

}