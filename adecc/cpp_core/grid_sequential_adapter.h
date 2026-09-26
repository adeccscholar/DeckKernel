// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file grid_sequential_adapter.h
\brief Adapter exposing a random-access grid backend as a sequential write target.

\details
Bridges grid backends that support indexed cell access to append-oriented algorithms and pipelines. The
adapter preserves the backend-neutral grid model while enabling sequential data transfer from ranges.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Grid Models as the Next Step".
- "Grids as Ranges: The UI Loses Its Special Status".
- "The Grid as a Projection, Not as Truth".
- "From Text and Grid to a General Adapter Strategy".

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

#include "grid_backend_concepts.h"

#include <cstddef>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace adecc {
namespace grid {

/*!
 \brief   Adapter from a regular random-access grid to a sequential WriteGrid
 \details Uses insert_row() and set(row,col,value), but exposes append_row()
          and append_col(). This allows ordinary UI grids to work with
          used as a WriteGridModel.
 \tparam  Backend Regular grid backend satisfying grid_table_backend_type
*/
template <grid_table_backend_type Backend>
class TableToSequentialGridBackend {
public:
   using backend_ty = Backend;
   using size_type = std::size_t;

public:
   explicit TableToSequentialGridBackend(backend_ty&& theBackend)
      : theBackend_{std::move(theBackend)} {
      iCurrentRow_ = theBackend_.rows();
      }

   TableToSequentialGridBackend(TableToSequentialGridBackend const&) = delete;
   TableToSequentialGridBackend& operator=(TableToSequentialGridBackend const&) = delete;

   TableToSequentialGridBackend(TableToSequentialGridBackend&&) noexcept = default;
   TableToSequentialGridBackend& operator=(TableToSequentialGridBackend&&) noexcept = default;

   ~TableToSequentialGridBackend() = default;

   template <class... Args>
      requires has_set_caption_for<backend_ty, Args...>
   void set_caption(std::vector<std::tuple<Args...>> const& vecCaps,
                    bool const bClear = true) {
      theBackend_.template set_caption<Args...>(vecCaps, bClear);
      iCurrentCol_ = 0;
      iCurrentRow_ = theBackend_.rows();
      }

   [[nodiscard]] size_type rows() const noexcept {
      return theBackend_.rows();
      }

   [[nodiscard]] size_type columns() const noexcept {
      return theBackend_.columns();
      }

   void reset(bool const bFull = true) {
      theBackend_.reset(bFull);
      iCurrentCol_ = 0;
      iCurrentRow_ = theBackend_.rows();
      }

   [[nodiscard]] bool freeze() {
      return theBackend_.freeze();
      }

   void unfreeze(bool const bFrozen) {
      theBackend_.unfreeze(bFrozen);
      }

   void append_row() {
      iCurrentRow_ = theBackend_.insert_row();
      iCurrentCol_ = 0;
      }

   template <class ty>
      requires adecc::is_in_type_list_v<ty, adecc::defined_param_types>
   void append_col(ty const& theValue) {
      theBackend_.template set<ty>(iCurrentRow_, iCurrentCol_, theValue);
      ++iCurrentCol_;
      }

   template <class opt_ty>
      requires (adecc::is_optional_v<opt_ty> &&
                adecc::is_in_type_list_v<typename opt_ty::value_type, adecc::defined_param_types>)
   void append_col(opt_ty const& theValue) {
      theBackend_.template set<opt_ty>(iCurrentRow_, iCurrentCol_, theValue);
      ++iCurrentCol_;
      }

   [[nodiscard]] backend_ty& backend() noexcept {
      return theBackend_;
      }

   [[nodiscard]] backend_ty const& backend() const noexcept {
      return theBackend_;
      }

private:
   backend_ty theBackend_;
   size_type iCurrentRow_ = 0;
   size_type iCurrentCol_ = 0;
};

template <class Backend>
   requires grid_table_backend_type<std::remove_cvref_t<Backend>>
[[nodiscard]] auto MakeSequentialGridBackend(Backend&& theBackend) {
   return TableToSequentialGridBackend<std::remove_cvref_t<Backend>>{
      std::forward<Backend>(theBackend)
      };
}

} // namespace grid
} // namespace adecc
