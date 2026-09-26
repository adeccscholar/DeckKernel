// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file wrapper_basic.h
\brief Common concepts and RAII helpers shared by backend-neutral text and grid wrappers.

\details
Defines optional backend capabilities such as reset, freeze, signal blocking, and wait cursors, then
composes them into a model-neutral FreezeGuard. Concrete framework adapters can provide these capabilities
without changing the core wrapper contracts.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "The Universal Wrapper Concept".
- "The Grid as a Projection, Not as Truth".
- "Text Wrapper and the Universal Wrapper Concept".
- "Typed Runtime Structure and RAII".

\see ARCHITECTURE.md#grid-projection-and-ui-boundaries

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

// ============================================================================
// Shared wrapper building blocks for backend-independent UI models.
// ----------------------------------------------------------------------------
// \brief   Backend-neutral concepts and RAII helpers for grid and text wrappers.
// \details Provides optional guard building blocks for freeze, signal blockers, and
//          wait cursors. A concrete model must provide backend_ty, freeze(),
//          unfreeze(bool), and for optional backend helpers a private or
//          public backend_() accessor.
// ============================================================================

#include <concepts>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace adecc::wrapper  {

template <class backend_ty>
concept has_reset_for =
   requires(backend_ty& a) {
      { a.reset() } -> std::same_as<void>;
      } ||
   requires(backend_ty& a, bool b) {
      { a.reset(b) } -> std::same_as<void>;
      };

template <class backend_ty>
concept has_freeze_for = requires(backend_ty& a, bool const bFrozen) {
   { a.freeze() } -> std::same_as<bool>;
   { a.unfreeze(bFrozen) } -> std::same_as<void>;
   };

template <class backend_ty>
concept has_signal_blocker_for = requires(backend_ty& a) {
   { a.make_signal_blocker() };
   };

template <class backend_ty, bool bHasSignalBlocker = has_signal_blocker_for<backend_ty>>
struct signal_blocker_type {
   using type = std::monostate;
   };

template <class backend_ty>
struct signal_blocker_type<backend_ty, true> {
   using type = decltype(std::declval<backend_ty&>().make_signal_blocker());
   };

template <class backend_ty>
using signal_blocker_type_t = typename signal_blocker_type<backend_ty>::type;

template <class backend_ty>
concept has_wait_cursor_for = requires(backend_ty& a) {
   { a.make_wait_cursor() };
   };

template <class backend_ty, bool bHasWaitCursor = has_wait_cursor_for<backend_ty>>
struct wait_cursor_type {
   using type = std::monostate;
   };

template <class backend_ty>
struct wait_cursor_type<backend_ty, true> {
   using type = decltype(std::declval<backend_ty&>().make_wait_cursor());
   };

template <class backend_ty>
using wait_cursor_type_t = typename wait_cursor_type<backend_ty>::type;

/*! 
 \brief     RAII guard for freeze/unfreeze, signal blockers, and wait cursors.
 \details   The guard is model-neutral. Signal blockers and wait cursors
            are created only when the backend provides the corresponding methods
            . The model must declare this guard as a friend if
            backend_() is private.
 \tparam    model_ty Concrete wrapper type, for example GridModel or TextModel
*/
template <class model_ty>
class FreezeGuard {
private:
   using backend_ty = typename model_ty::backend_ty;
   using wait_cursor_ty = wait_cursor_type_t<backend_ty>;
   using signal_blocker_ty = signal_blocker_type_t<backend_ty>;

public:
   explicit FreezeGuard(model_ty& theModel) : pModel_{ &theModel },
                                             optWaitCursor_{ MakeWaitCursor_(theModel) },
                                             optBlocker_{ MakeSignalBlocker_(theModel) },
                                             bFrozen_{ theModel.freeze() } {
      }

   FreezeGuard(FreezeGuard const&) = delete;
   FreezeGuard& operator=(FreezeGuard const&) = delete;

   FreezeGuard(FreezeGuard&& rhs) noexcept : pModel_{ rhs.pModel_ },
                                             optWaitCursor_{ std::move(rhs.optWaitCursor_) },
                                             optBlocker_{ std::move(rhs.optBlocker_) },
                                             bFrozen_{ rhs.bFrozen_ } {
      rhs.pModel_ = nullptr;
      rhs.bFrozen_ = false;
      }

   ~FreezeGuard() {
      if(pModel_) {
         pModel_->unfreeze(bFrozen_);
         }
      }

private:
   static wait_cursor_ty MakeWaitCursor_(model_ty& theModel) {
      if constexpr(has_wait_cursor_for<backend_ty>) {
         return theModel.backend_().make_wait_cursor();
         }
      else {
         return std::monostate{};
         }
      }

   static signal_blocker_ty MakeSignalBlocker_(model_ty& theModel) {
      if constexpr(has_signal_blocker_for<backend_ty>) {
         return theModel.backend_().make_signal_blocker();
         }
      else {
         return std::monostate{};
         }
      }

private:
   model_ty* pModel_ = nullptr;
   std::optional<wait_cursor_ty> optWaitCursor_;
   std::optional<signal_blocker_ty> optBlocker_;
   bool bFrozen_ = false;
   };


} // namespace adecc::wrapper
