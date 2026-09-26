// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file grid_wrapper.h
\brief Typed backend-neutral grid model with range, proxy, and sink semantics.

\details
Provides the central grid abstraction used to access rows and cells, project column selections, expose
writable proxies, and integrate standard ranges and algorithms. Concrete VCL, FMX, Qt, or other UI grids
remain replaceable physical adapters at the system boundary.

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

// ============================================================================
// grid_core.h — backend-independent abstraction plus VCL/FMX grid adapters (C++23)
// ----------------------------------------------------------------------------
// \brief   Abstract grid core with a clear concept, random-access range, and
//          Output sink. Physical VCL/FMX adapters satisfy the concept.
// \details Conversion uses ConvertTo/ConvertOpt supplied externally
//          gestellt). Optional-Semantik: get<T> wirft bei leer, get<optional<T>>
//          returns nullopt; set<optional<T>> writes an empty cell.
// ============================================================================

#include "tools.h"
#include "convert_core.h"
#include "type_lists.h"
#include "value_types.h"
#include "stream_tools.h"
#include "wrapper_basic.h"
#include "grid_wrapper_basic.h"

#include <cstddef>
#include <optional>
#include <tuple>
#include <vector>
#include <array>
#include <utility>
#include <type_traits>
#include <concepts>
#include <compare>
#include <ranges>
#include <stdexcept>
#include <memory>



namespace adecc {

   namespace grid {



      // ============================================================================
      // Backend concepts are defined in grid_backend_concepts.h
      // ============================================================================

      // ============================================================================
      // Abstract grid wrapper without dependencies on a concrete grid implementation
      // ============================================================================

      /*!
       \brief     Generic grid wrapper over a table_type backend
       \tparam    Backend Concrete backend type satisfying table_type
       \tparam    Owned    true: owned (unique_ptr), false: borrowed
       \details   Handles optional semantics, ranges, and the output sink.
      */
      template <table_type Backend>
      class GridModel {
      public:
         using backend_ty = Backend;
         using size_type = std::size_t;

      public:

         GridModel() = default;

         explicit GridModel(backend_ty&& theBackend) : theBackend_{ std::move(theBackend) } {}

         /*!
         \brief   Constructor with variadic captions
         \details Validates the actual caption signature through requires.
         \param   pGrid Pointer to the concrete backend
         \param   vecCaps Backenddefinierter vector<tuple<Args...>>
         \pre     pGrid != nullptr
         */

         template <class... Args> requires has_set_caption_for<Backend, Args...>
         explicit GridModel(backend_ty&& theBackend,
            std::vector<std::tuple<Args...>> const& vecCaps, bool clear = true) :
            theBackend_{ std::move(theBackend) } {
            if (theBackend_) {
               theBackend_->template set_caption<Args...>(vecCaps, clear);
               iCols_ = theBackend_->columns();
               iRows_ = theBackend_->rows();
               }
            }

         GridModel(GridModel const&) = delete;
         GridModel& operator=(GridModel const&) = delete;

         GridModel(GridModel&&) noexcept = default;
         GridModel& operator=(GridModel&&) noexcept = default;

         ~GridModel() = default;

         // ------------------------------------------------------------------------
         // Dimensionen
         // ------------------------------------------------------------------------
         [[nodiscard]] size_type rows()    const noexcept { return iRows_; }
         [[nodiscard]] size_type columns() const noexcept { return iCols_; }

         [[nodiscard]] size_type current_row() const {
            return backend_().current_row();
            }

         // ------------------------------------------------------------------------
         //   reset and clear
         // ------------------------------------------------------------------------
         void reset() {
            if (theBackend_) {
               backend_().reset(true);
               iCols_ = backend_().columns();
               iRows_ = backend_().rows();
               }
            }

         void clear() {
            if (theBackend_) {
               backend_().reset(false);
               iCols_ = backend_().columns();
               iRows_ = backend_().rows();
               }
            }

         template <class... Args> requires has_set_caption_for<Backend, Args...>
         void set_captions(std::vector<std::tuple<Args...>> const& vecCaps, bool clear = true) {
            if (theBackend_) {
               reset();
               theBackend_->template set_caption<Args...>(vecCaps, clear);
               iCols_ = theBackend_->columns();
               iRows_ = theBackend_->rows();
               }
            }

         // -----------------------------------------------------------------------------------------------
         // Alternative method for setting columns with bands, including cxGrid backends
         // The concept enables this alternative only if the backend supports it
         // -----------------------------------------------------------------------------------------------
         template <class bands_ty, class caps_ty>
               requires has_prepare_layout_for<backend_ty, bands_ty, caps_ty>
         void prepare_layout(bands_ty const& theBands, caps_ty const& theCaps, bool const bClear = true) {
            auto guard = freeze_guard();
            backend_().prepare_layout(theBands, theCaps, bClear);
            iCols_ = backend_().columns();
            iRows_ = backend_().rows();
            }

         template <class repository_ty, class bands_ty, class caps_ty>
               requires (has_set_repository_for<backend_ty, repository_ty> &&
                         has_prepare_layout_for<backend_ty, bands_ty, caps_ty>
                        )
         void prepare_layout(bands_ty const& theBands, caps_ty const& theCaps,
                             repository_ty const& theRepository, bool const bClear = true) {
            auto guard = freeze_guard();
            backend_().set_repository(theRepository);
            backend_().prepare_layout(theBands, theCaps, bClear);
            iCols_ = backend_().columns();
            iRows_ = backend_().rows();
            }

         // -----------------------------------------------------------------------------------------------
         //  Ende des Einschubs
         // -----------------------------------------------------------------------------------------------


         [[nodiscard]] bool freeze() {
            return backend_().freeze();
            }

         void unfreeze(bool const bFrozen) {
            backend_().unfreeze(bFrozen);
            }

         template <adecc::CaptionVector range_ty>
         GridModel& operator = (range_ty&& theRange) {
            set_captions(std::forward<range_ty>(theRange));
            return *this;
            }


         template <std::ranges::input_range range_ty>
            requires adecc::is_tuple_v<std::remove_cvref_t<std::ranges::range_reference_t<range_ty>>> &&
                      (!adecc::CaptionVector<range_ty>)
            GridModel& operator +=(range_ty&& theRange) {
               if (theBackend_) {
                  using tuple_ty = std::remove_cvref_t<std::ranges::range_reference_t<range_ty>>;

                  [&] <std::size_t... I>(std::index_sequence<I...>) {
                     auto theSink = this->template sink<std::tuple_element_t<I, tuple_ty>...>();
                     std::ranges::copy(std::forward<range_ty>(theRange), theSink);
                     }(std::make_index_sequence<std::tuple_size_v<tuple_ty>>{});
                  }
               return *this;
               }

         template <std::ranges::input_range range_ty>
            requires adecc::is_tuple_v<std::remove_cvref_t<std::ranges::range_reference_t<range_ty>>> &&
                        (!adecc::CaptionVector<range_ty>)
            GridModel& operator = (range_ty&& theRange) {
            if (theBackend_) {
               clear();
               operator += (theRange);
               }
            return *this;
            }


         // ------------------------------------------------------------------------
         // Row operations
         // ------------------------------------------------------------------------
         size_type insertRow() {
            auto const r = backend_().insert_row();
            iRows_ = backend_().rows();
            return r;
            }

         template <class... Ts>
         GridModel& operator += (std::tuple<Ts...> const& t) {
            appendRow(t);
            return *this;
            }

         template <class ty>
         [[nodiscard]] ty get(size_type const iRow, size_type const iCol) const {
            using base_ty = value_type_or_self_t<ty>;

            static_assert(requires(Backend & b) { b.template get<std::optional<base_ty>>(iRow, iCol); },
               "Backend muss get<std::optional<T>>(row,col) bereitstellen.");

            auto opt = backend_().template get<std::optional<base_ty>>(iRow, iCol);

            if constexpr (adecc::is_optional_v<ty>) {
               return opt;
               }
            else {
               if (!opt) {
                  if constexpr (std::is_same_v<base_ty, std::string> ||
                     std::is_same_v<base_ty, std::wstring>) {
                     return base_ty{};
                     }
                  else {
                     throw std::runtime_error{ "get: empty cell but non-optional requested" };
                     }
                  }
               return *opt;
               }
            }


         template <class ty>
         void set(size_type const iRow, size_type const iCol, ty const& theVal) {
            //             "Use set<std::optional<T>>(...) for optional values.");
            static_assert(requires(Backend & b) { b.template set<ty>(iRow, iCol, theVal); },
               "Backend muss set<T>(row,col,T const&) bereitstellen.");
            backend_().template set<ty>(iRow, iCol, theVal);
         }

         template <class ty>
         void set(size_type const iRow, size_type const iCol, std::optional<ty> const& theVal) {
            static_assert(requires(Backend & b) { b.template set<std::optional<ty>>(iRow, iCol, theVal); },
               "Backend muss set<std::optional<T>>(row,col,optional<T> const&) bereitstellen.");
            backend_().template set<std::optional<ty>>(iRow, iCol, theVal);
            }

         // ------------------------------------------------------------------------
         // Convenience API: read/write whole rows, append rows, and access the column list
         // ------------------------------------------------------------------------
         template <class... Ts>
         [[nodiscard]] std::tuple<Ts...> getRow(size_type const iRow) const {
            return[&]<std::size_t... I>(std::index_sequence<I...>) {
               return std::tuple<Ts...>{ get<Ts>(iRow, (size_type)I)... };
               }(std::make_index_sequence<sizeof...(Ts)>{});
            }

         template <class... Ts>
         [[nodiscard]] std::tuple<Ts...>
            getRowCols(size_type const iRow, std::vector<size_type> const& vecCols) const {
            if (vecCols.size() != sizeof...(Ts)) {
               throw std::runtime_error{ "getRowCols: size mismatch" };
               }
            std::array<size_type, sizeof...(Ts)> aCols{};
            std::copy(vecCols.begin(), vecCols.end(), aCols.begin());
            return[&]<std::size_t... I>(std::index_sequence<I...>) {
               return std::tuple<Ts...>{ get<Ts>(iRow, aCols[I])... };
               }(std::make_index_sequence<sizeof...(Ts)>{});
            }

         template <class... Ts>
         void setRow(size_type const iRow, std::tuple<Ts...> const& theTpl) {
            if (sizeof...(Ts) > iCols_) {
               throw std::runtime_error{ std::format("setRow: more values ({}) than columns ({})",  sizeof...(Ts), iCols_) };
               }
            [&] <std::size_t... I>(std::index_sequence<I...>) {
               (set<std::tuple_element_t<I, std::tuple<Ts...>>>(iRow, (size_type)I, std::get<I>(theTpl)), ...);
               }(std::make_index_sequence<sizeof...(Ts)>{});
            }

         template <class... Ts>
         void setRowCols(size_type const iRow, std::vector<size_type> const& vecCols, std::tuple<Ts...> const& theTpl) {
            if (vecCols.size() != sizeof...(Ts)) {
               throw std::runtime_error{ "setRowCols: size mismatch" };
               }
            std::array<size_type, sizeof...(Ts)> aCols{};
            std::copy(vecCols.begin(), vecCols.end(), aCols.begin());
            [&] <std::size_t... I>(std::index_sequence<I...>) {
               (set<std::tuple_element_t<I, std::tuple<Ts...>>>(iRow, aCols[I], std::get<I>(theTpl)), ...);
               }(std::make_index_sequence<sizeof...(Ts)>{});
            }

         template <class... Ts>
         void appendRow(std::tuple<Ts...> const& theTpl) {
            size_type const r = insertRow();
            setRow<Ts...>(r, theTpl);
            }

         template <class... Ts>
         void append(Ts&&... vs) {
            appendRow(std::tuple<std::decay_t<Ts>...>{ std::forward<Ts>(vs)... });
            }

         template <typename... Ts>
         [[nodiscard]] std::optional<size_type> findRow(std::vector<size_type> const& vecCols, std::tuple<Ts...> const& theValues) const {
            auto theView = makeRowView<Ts...>(vecCols);
            auto it = std::ranges::find(theView, theValues);
            if (it == theView.end()) return std::nullopt;
            return static_cast<size_type>(std::ranges::distance(theView.begin(), it));
            }


         bool focus_row(size_type iRow) {
            return backend_().focus_row(iRow);
            }
         using FreezeGuard = adecc::wrapper::FreezeGuard<GridModel>;

         [[nodiscard]] FreezeGuard freeze_guard() {
            return FreezeGuard{ *this };
            }

         template <class grid_ty>
         class CellProxy {
         public:
            using size_type = typename GridModel::size_type;

            CellProxy(grid_ty* p, size_type r, size_type c) : p_{ p }, r_{ r }, c_{ c } {}

            // optionaler GET: opt_ty = std::optional<ty>, ty in defined_values_types
            template<class opt_ty> requires (adecc::is_optional_v<opt_ty>&&
               adecc::is_in_type_list_v<typename opt_ty::value_type, adecc::defined_values_types>)
               opt_ty get() const {
               using ty = typename opt_ty::value_type;
               return p_->template get<ty>(r_, c_, std::type_identity<std::optional<ty>>{});
            }

            // Non-optional: ty is contained in defined_values_types
            template<class ty>
               requires (adecc::is_in_type_list_v<ty, adecc::defined_values_types> && !adecc::is_optional_v<ty>)
            ty get() const {
               // delegiert an GridModel::get<T>(row,col)
               return p_->template get<ty>(r_, c_);
            }

            // ------- SET: only available when the parent is non-const -------

            template<class ty>
               requires (!std::is_const_v<grid_ty>&& adecc::is_in_type_list_v<ty, adecc::defined_param_types>)
            void set(ty const& v) {
               p_->template set<ty>(r_, c_, v);
            }

            template<class ty>
               requires (!std::is_const_v<grid_ty>&& adecc::is_in_type_list_v<ty, adecc::defined_param_types>)
            void set(std::optional<ty> const& v) {
               p_->template set<ty>(r_, c_, v);
            }

            // Convenience: assigning to a cell forwards to set(...)
            template<class ty>
               requires (!std::is_const_v<grid_ty>&& adecc::is_in_type_list_v<ty, adecc::defined_param_types>)
            CellProxy& operator=(ty const& v) { set<ty>(v); return *this; }

            template<class ty>
               requires (!std::is_const_v<grid_ty>&& adecc::is_in_type_list_v<ty, adecc::defined_param_types>)
            CellProxy& operator=(std::optional<ty> const& v) { set<ty>(v); return *this; }

         private:
            grid_ty* p_{};
            size_type r_{}, c_{};
         };

         using cell_type = CellProxy<GridModel>;
         using const_cell_type = CellProxy<GridModel const>;

         [[nodiscard]] cell_type operator[](size_type r, size_type c) {
            bounds_check_(r, c);
            return cell_type{ this, r, c };
         }

         [[nodiscard]] const_cell_type operator[](size_type r, size_type c) const {
            bounds_check_(r, c);
            return const_cell_type{ this, r, c };
         }

         // ======================================================================
         // Ranges: random-access iterator/view over data rows
         // ======================================================================
         template <class... Ts>
         class RowProxy {
         public:
            RowProxy() = default;
            RowProxy(GridModel const* p, size_type const i) : p_{ p }, iRow_{ i } {}
            operator std::tuple<Ts...>() const { return p_->template getRow<Ts...>(iRow_); }
            RowProxy& operator=(std::tuple<Ts...> const& theT) { p_->template setRow<Ts...>(iRow_, theT); return *this; }
         private:
            GridModel const* p_ = nullptr;
            size_type iRow_ = 0;
         };

         template <class... Ts>
         class RowIter {
         public:
            using iterator_concept = std::random_access_iterator_tag;
            using iterator_category = std::random_access_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = std::tuple<Ts...>;
            using reference = value_type;

            RowIter() = default;
            RowIter(GridModel const* p, size_type const i) : p_{ p }, iRow_{ i } {}

            reference operator*() const { return p_->template getRow<Ts...>(iRow_); }
            value_type operator[](difference_type const n) const {
               return p_->template getRow<Ts...>((size_type)((std::ptrdiff_t)iRow_ + n));
            }


            RowIter& operator++() { ++iRow_; return *this; }
            RowIter operator++(int) { auto tmp = *this; ++*this; return tmp; }
            RowIter& operator--() { --iRow_; return *this; }
            RowIter operator--(int) { auto tmp = *this; --*this; return tmp; }

            RowIter& operator+=(difference_type const n) { iRow_ = (size_type)((std::ptrdiff_t)iRow_ + n); return *this; }
            RowIter& operator-=(difference_type const n) { iRow_ = (size_type)((std::ptrdiff_t)iRow_ - n); return *this; }

            friend RowIter operator+(RowIter it, difference_type const n) { it += n; return it; }
            friend RowIter operator-(RowIter it, difference_type const n) { it -= n; return it; }
            friend RowIter operator+(difference_type n, RowIter it) { it += n; return it; }

            friend difference_type operator-(RowIter a, RowIter b) { return (difference_type)((std::ptrdiff_t)a.iRow_ - (std::ptrdiff_t)b.iRow_); }

            //friend bool operator==(RowIter a, RowIter b) { return a.p_ == b.p_ && a.iRow_ == b.iRow_; }
            friend auto operator<=>(RowIter const&, RowIter const&) = default;
         private:
            GridModel const* p_ = nullptr;
            size_type iRow_ = 0;
         };

         template <class... Ts>
         class RowView : public std::ranges::view_interface<RowView<Ts...>> {
         public:
            RowView() = default;
            explicit RowView(GridModel const* p) : p_{ p } {}

            RowIter<Ts...> begin() const { return RowIter<Ts...>{ p_, 0 }; }
            RowIter<Ts...> end()   const { return RowIter<Ts...>{ p_, p_->rows() }; }
            size_type size() const { return p_->rows(); }
         private:
            GridModel const* p_ = nullptr;
         };

         template<class... Ts>
         class RowRefProxy {
         public:
            using tuple_type = std::tuple<Ts...>;

            RowRefProxy() = default;
            RowRefProxy(GridModel* p, std::ptrdiff_t r) : p_{ p }, r_{ r } {}
            void reset(GridModel* p, std::ptrdiff_t r) { p_ = p; r_ = r; }

            ~RowRefProxy() noexcept = default;

            // Lesen (wie gehabt)
            operator tuple_type() const {
               auto const n = static_cast<std::ptrdiff_t>(p_->rows());
               if (r_ < 0 || r_ >= n) throw std::out_of_range{ std::format("RowRefProxy::get: row {}", r_) };
               return p_->template getRow<Ts...>(static_cast<typename GridModel::size_type>(r_));
            }

            // **Writing** – six overloads so libc++ accepts all write expressions

            // lvalue
            RowRefProxy& operator=(tuple_type const& t)& {
               write_(t); return *this;
            }
            RowRefProxy& operator=(tuple_type&& t)& {
               write_(std::move(t)); return *this;
            }

            // const lvalue
            RowRefProxy const& operator=(tuple_type const& t) const& {
               write_(t); return *this;
            }
            RowRefProxy const& operator=(tuple_type&& t) const& {
               write_(std::move(t)); return *this;
            }

            // const rvalue (tested by libc++ concepts)
            RowRefProxy const& operator=(tuple_type const& t) const&& {
               write_(t); return *this;
            }
            RowRefProxy const& operator=(tuple_type&& t) const&& {
               write_(std::move(t)); return *this;
            }

            friend void iter_swap(RowRefProxy a, RowRefProxy b) {
               auto ta = static_cast<tuple_type>(a);
               auto tb = static_cast<tuple_type>(b);
               a = std::move(tb);
               b = std::move(ta);
            }
            friend tuple_type iter_move(RowRefProxy const& a) { return static_cast<tuple_type>(a); }

         private:
            template<class U>
            void write_(U&& t) const {
               auto const n = static_cast<std::ptrdiff_t>(p_->rows());
               if (r_ < 0 || r_ >= n) throw std::out_of_range{ "RowRefProxy::set: row" };
               p_->template setRow<Ts...>(static_cast<typename GridModel::size_type>(r_), std::forward<U>(t));
            }

            GridModel* p_ = nullptr;
            std::ptrdiff_t r_ = 0;
         };

         struct RowRefSentinel { std::ptrdiff_t n{}; };

         template<class... Ts>
         class RowRefIter {
         public:
            using iterator_concept = std::random_access_iterator_tag;
            using iterator_category = std::random_access_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = std::tuple<Ts...>;
            using reference = RowRefProxy<Ts...>;          // prvalue laut libc++-Erwartung

            RowRefIter() = default;
            RowRefIter(GridModel* p, std::ptrdiff_t i) : p_{ p }, r_{ i } {}
            ~RowRefIter() noexcept = default;

            reference operator*() const {
#ifndef NDEBUG
               auto const n = static_cast<std::ptrdiff_t>(p_->rows());
               if (r_ < 0 || r_ >= n) std::unreachable();
#endif
               return reference{ p_, r_ };                         // prvalue Proxy
            }

            reference operator[](difference_type n) const {
#ifndef NDEBUG
               auto const N = static_cast<std::ptrdiff_t>(p_->rows());
               auto const idx = r_ + n;
               if (idx < 0 || idx >= N) std::unreachable();
#endif
               return reference{ p_, r_ + n };                     // prvalue Proxy
            }

            RowRefIter& operator++() { ++r_; return *this; }
            RowRefIter  operator++(int) { RowRefIter t = *this; ++*this; return t; }
            RowRefIter& operator--() { --r_; return *this; }
            RowRefIter  operator--(int) { RowRefIter t = *this; --*this; return t; }

            RowRefIter& operator+=(difference_type n) { r_ += n; return *this; }
            RowRefIter& operator-=(difference_type n) { r_ -= n; return *this; }

            friend RowRefIter operator+(RowRefIter it, difference_type n) { it += n; return it; }
            friend RowRefIter operator-(RowRefIter it, difference_type n) { it -= n; return it; }
            friend RowRefIter operator+(difference_type n, RowRefIter it) { it += n; return it; }

            friend difference_type operator-(RowRefIter a, RowRefIter b) { return a.r_ - b.r_; }
            friend bool operator==(RowRefIter const& a, RowRefIter const& b) { return a.p_ == b.p_ && a.r_ == b.r_; }
            friend std::strong_ordering operator<=>(RowRefIter const& a, RowRefIter const& b) { return a.r_ <=> b.r_; }

            // ADL-Hooks (swap/move via Tupel)
            friend void iter_swap(RowRefIter a, RowRefIter b) {
               using tup = value_type;
               auto ta = static_cast<tup>(*a);
               auto tb = static_cast<tup>(*b);
               *a = std::move(tb);
               *b = std::move(ta);
            }
            friend value_type iter_move(RowRefIter it) { return static_cast<value_type>(*it); }

            // Binary sentinel interoperability for BCC64X
            friend bool operator==(RowRefIter it, RowRefSentinel s) { return it.r_ == s.n; }
            friend bool operator==(RowRefSentinel s, RowRefIter it) { return it.r_ == s.n; }
            friend difference_type operator-(RowRefSentinel s, RowRefIter it) { return s.n - it.r_; }
            friend difference_type operator-(RowRefIter it, RowRefSentinel s) { return it.r_ - s.n; }

         private:
            GridModel* p_ = nullptr;
            std::ptrdiff_t r_ = 0;
         };



         template <class... Ts>
         class RowRefView : public std::ranges::view_interface<RowRefView<Ts...>> {
         public:
            using size_type = typename GridModel::size_type;
            RowRefView() = default;
            explicit RowRefView(GridModel* p) : p_{ p } {}
            RowRefIter<Ts...> begin() const { return RowRefIter<Ts...>{ p_, 0 }; }
            RowRefSentinel    end()   const { return RowRefSentinel{ static_cast<std::ptrdiff_t>(p_->rows()) }; }
            size_type size() const { return p_->rows(); }
         private:
            GridModel* p_ = nullptr;
         };




         // ======================================================================
         // Ranges: random-access iterator/view over data rows with column selection
         // ======================================================================
         template <class... Ts>
         class RowColsIter {
         public:
            using iterator_concept = std::random_access_iterator_tag;
            using iterator_category = std::random_access_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = std::tuple<Ts...>;
            using reference = value_type;   // by-value (prvalue)

            RowColsIter() = default;
            RowColsIter(GridModel const* p, size_type const i,
               std::vector<size_type> const* pCols)
               : p_{ p }, iRow_{ i }, pCols_{ pCols } {
            }

            reference   operator*() const { return p_->template getRowCols<Ts...>(iRow_, *pCols_); }
            value_type  operator[](difference_type n) const {
               return p_->template getRowCols<Ts...>((size_type)((std::ptrdiff_t)iRow_ + n), *pCols_);
            }

            RowColsIter& operator++() { ++iRow_; return *this; }
            RowColsIter  operator++(int) { auto tmp = *this; ++*this; return tmp; }
            RowColsIter& operator--() { --iRow_; return *this; }
            RowColsIter  operator--(int) { auto tmp = *this; --*this; return tmp; }

            RowColsIter& operator+=(difference_type n) { iRow_ = (size_type)((std::ptrdiff_t)iRow_ + n); return *this; }
            RowColsIter& operator-=(difference_type n) { iRow_ = (size_type)((std::ptrdiff_t)iRow_ - n); return *this; }

            friend RowColsIter operator+(RowColsIter it, difference_type n) { it += n; return it; }
            friend RowColsIter operator-(RowColsIter it, difference_type n) { it -= n; return it; }
            friend RowColsIter operator+(difference_type n, RowColsIter it) { it += n; return it; }

            friend difference_type operator-(RowColsIter a, RowColsIter b) {
               return (difference_type)((std::ptrdiff_t)a.iRow_ - (std::ptrdiff_t)b.iRow_);
            }

            friend auto operator<=>(RowColsIter const&, RowColsIter const&) = default;

         private:
            GridModel const* p_ = nullptr;
            size_type                   iRow_ = 0;
            std::vector<size_type> const* pCols_ = nullptr;
         };

         template <class... Ts>
         class RowColsView : public std::ranges::view_interface<RowColsView<Ts...>> {
         public:
            RowColsView() = default;

            explicit RowColsView(GridModel const* p, std::vector<size_type> cols)
               : p_{ p }, cols_{ std::move(cols) } {
               if (cols_.size() != sizeof...(Ts)) {
                  throw std::runtime_error{ "RowColsView: column count mismatch to Ts..." };
               }
            }

            RowColsIter<Ts...> begin() const { return RowColsIter<Ts...>{ p_, 0, & cols_ }; }
            RowColsIter<Ts...> end()   const { return RowColsIter<Ts...>{ p_, p_->rows(), & cols_ }; }
            size_type size() const { return p_->rows(); }

            // Optional access to the column list
            std::vector<size_type> const& columns() const noexcept { return cols_; }

         private:
            GridModel const* p_ = nullptr;
            std::vector<size_type> cols_;
         };

         template <class... Ts>
         [[nodiscard]] RowView<Ts...> makeRowView() const { return RowView<Ts...>{ this }; }

         // Factory creating a view with column selection
         template <class... Ts>
         [[nodiscard]] RowColsView<Ts...>
            makeRowView(std::vector<size_type> cols) const {
            return RowColsView<Ts...>{ this, std::move(cols) };
         }

         template <class... Ts>
         [[nodiscard]] RowRefView<Ts...> makeRowRefView() { return RowRefView<Ts...>{ this }; }


         template <class... Ts>
         class OutIter {
         public:
            using iterator_category = std::output_iterator_tag;
            using iterator_concept = std::output_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = void;
            using reference = void;
            using pointer = void;

            explicit OutIter(GridModel& m) : m_{ &m } {}

            OutIter& operator=(std::tuple<Ts...> const& t) {
               m_->appendRow(t);
               return *this;
            }

            OutIter& operator=(std::tuple<Ts...>&& t) {
               m_->appendRow(std::move(t));
               return *this;
            }

            template <class U>
            OutIter& operator=(U&& u) {
               if constexpr (requires { std::tuple_size<std::remove_reference_t<U>>::value; }&&
                  std::tuple_size_v<std::remove_reference_t<U>> == sizeof...(Ts)) {
                  std::apply([&](auto&&... xs) {
                     m_->appendRow(std::tuple<Ts...>{ static_cast<Ts>(std::forward<decltype(xs)>(xs))... });
                     }, std::forward<U>(u));
               }
               else {
                  m_->appendRow(std::tuple<Ts...>{ std::forward<U>(u) });
               }
               return *this;
            }

            // Important: *out is an lvalue assigned to by the STL
            OutIter& operator*() { return *this; }
            OutIter& operator++() { return *this; }
            OutIter  operator++(int) { OutIter tmp{ *this }; return tmp; }

         private:
            GridModel* m_ = nullptr; // nicht-const lvalue Ziel
         };

         template <class... Ts>
         [[nodiscard]] OutIter<Ts...> sink() { return OutIter<Ts...>{ *this }; }

      private:
         friend class adecc::wrapper::FreezeGuard<GridModel>;

         backend_ty& backend_() {
            if (!theBackend_) {
               throw std::logic_error("GridModel: backend not initialized");
            }
            return *theBackend_;
         }

         backend_ty const& backend_() const {
            if (!theBackend_) {
               throw std::logic_error("GridModel: backend not initialized");
            }
            return *theBackend_;
         }

         // Lightweight bounds check; enforced strictly in debug builds
         void bounds_check_(size_type r, size_type c) const {
#ifndef NDEBUG
            if (r >= iRows_ || c >= iCols_)
               throw std::out_of_range{ "GridModel::cell: index out of range" };
#else
            (void)r; (void)c;
#endif
         }

      private:
         std::optional<backend_ty> theBackend_;
         size_type iCols_{ 0 };
         size_type iRows_{ 0 };
      };



      template <typename ty>
      struct GridModelTraits;

      template <table_type Backend>
      struct GridModelTraits<GridModel<Backend>> {
         using grid_type = GridModel<Backend>;
         using backend_type = Backend;
         using size_type = typename grid_type::size_type;
         using cell_type = typename grid_type::cell_type;
         using const_cell_type = typename grid_type::const_cell_type;

         template <class... Ts>
         using row_view_type = typename grid_type::template RowView<Ts...>;

         template <class... Ts>
         using row_ref_view_type = typename grid_type::template RowRefView<Ts...>;

         template <class... Ts>
         using row_cols_view_type = typename grid_type::template RowColsView<Ts...>;

         template <class... Ts>
         using out_iter_type = typename grid_type::template OutIter<Ts...>;
      };


      template <typename ty>
      struct is_GridModel : std::false_type {};

      template <table_type Backend>
      struct is_GridModel<GridModel<Backend>> : std::true_type {};

      template <typename ty>
      inline constexpr bool is_GridModel_v = is_GridModel<std::remove_cvref_t<ty>>::value;


      template <typename ty>
      concept grid_model_type = is_GridModel_v<ty>;

   } // end of namespace grid
} // namespace adecc


