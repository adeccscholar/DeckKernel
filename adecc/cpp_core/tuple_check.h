// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file tuple_check.h
\brief Diagnostic helpers for inspecting tuple elements and optional values.

\details
Provides compile-time format capability checks and tuple iteration helpers that print each element in
positional order. It is intended primarily for diagnostics and tests of tuple-based data structures.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "std::tuple: A Type Sequence as a Single Value".
- "Type Lists as Structured Type Relations".
- "From Type Sequence to Data Class".
- "SystemData as an Application of the Type List".

\see ARCHITECTURE.md#type-lists-tuples-and-systemdata

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

#include "type_lists.h"

#include <tuple>
#include <optional>
#include <type_traits>
#include <format>
#include <print>


namespace adecc {
// --- "formattable?" (for std::println) ---------------------------------------

template <class ty>
concept formattable_to_string = requires(ty const& aVal) {
   std::format("{}", aVal);
};

// --- Element output ----------------------------------------------------------

template <class ty>
static void PrintValue_(ty const& aVal) {
   if constexpr (formattable_to_string<ty>) {
      std::println("{}", aVal);
      }
   else {
      std::println("<unformattable type>");
      }
   }

template <class ty>
static void PrintElement_(std::size_t uIndex, ty const& aVal) {
   if constexpr (is_optional_v<ty>) {
      std::println("[{}] optional: has_value={}", uIndex, aVal.has_value());
      if (aVal.has_value()) {
         std::print("     value=");
         PrintValue_(*aVal);
         }
      }
   else {
      std::print("[{}] value=", uIndex);
      PrintValue_(aVal);
      }
   }

// --- Tuple-Iteration ----------------------------------------------------------

template <class Tup, std::size_t... Is>
static void PrintTupleImpl_(Tup const& aTup, std::index_sequence<Is...>) {
   (PrintElement_(Is, std::get<Is>(aTup)), ...);
   }

template <class Tup>
static void PrintTuple(Tup const& aTup) {
   PrintTupleImpl_(aTup, std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<Tup>>>{});
   }


} // namespace adecc
