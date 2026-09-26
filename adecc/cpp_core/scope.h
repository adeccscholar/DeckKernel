// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file scope.h
\brief Small RAII scope-exit guard for deterministic cleanup actions.

\details
Executes a callable when a scope ends unless the guard is explicitly released. The utility supports move
transfer of responsibility and follows the library's broader use of RAII for lifetime and state management.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Typed Runtime Structure and RAII".
- "stream_redirect: RAII over Changed State".
- "Files as Ranges: Resources, Tuples, and RAII".
- "The Cost Model of Abstraction".

\see ARCHITECTURE.md#raii-and-deterministic-state

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

#include <type_traits>
#include <utility>


// Helper for scope-exit handling; replaceable when an equivalent standard facility is available
namespace adecc {

/**
   \brief Minimaler Scope-Exit-Guard
   \details Executes the supplied callable at the end of the scope.
            Movable but not copyable; can be disabled with \c dismiss().
   \tparam func_ty Functor/callable type, normally deduced via CTAD
*/
template <typename func_ty> requires std::invocable<func_ty>
class ScopeExit {
public:
   /// \brief Constructs and activates the guard
   /// \param aFn Callable executed at scope exit
   explicit ScopeExit(func_ty aFn) noexcept(std::is_nothrow_move_constructible_v<func_ty>)
      : aFn{std::move(aFn)}, uActive{true} { }

   /// \brief Executes the callable if the guard is active
   ~ScopeExit() noexcept {
      if (uActive) { aFn(); }
      }

   ScopeExit(ScopeExit const&) = delete;
   ScopeExit& operator=(ScopeExit const&) = delete;

   /// \brief Move constructor transfers responsibility
   ScopeExit(ScopeExit&& aOther) noexcept(std::is_nothrow_move_constructible_v<func_ty>)
      : aFn{std::move(aOther.aFn)}, uActive{aOther.uActive} {
      aOther.uActive = false;
      }

   ScopeExit& operator=(ScopeExit&&) = delete;

   /// \brief Disables execution at scope exit
   void dismiss() noexcept { uActive = false; }

private:
   func_ty aFn;
   bool uActive;
};

/**
   \brief Helper function for type deduction (CTAD)
   \details Creates a \c ScopeExit without explicitly specifying the functor type.
   \tparam func_ty Deduced callable type
   \param aFn Callable executed at scope exit
   \returns ScopeExit<TFn>
*/
template <typename func_ty> requires std::invocable<func_ty>
auto makeScopeExit(func_ty&& aFn) {
   using TDecayed = std::decay_t<func_ty>;
   return ScopeExit<TDecayed>{std::forward<func_ty>(aFn)};
   }


} // namespace adecc
