// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file text_wrapper.h
\brief Backend-neutral text models exposing text controls and sequences as typed ranges and sinks.

\details
Defines concepts, proxies, views, writable models, and in-memory reference backends for line-oriented text.
Physical VCL, FMX, DevExpress, and Qt controls can be adapted at the boundary while the core model remains
standard C++.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Text Wrapper and the Universal Wrapper Concept".
- "Text as a Range".
- "Output Iterators and Standard Algorithms".
- "Text as a Source".

\see ARCHITECTURE.md#text-wrappers-and-stream-integration

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
// text_wrapper.h - backend-neutral abstraction and adapter core for text controls
// ----------------------------------------------------------------------------
// \brief   Abstract text core with a clear concept, random-access range, and
//          output sink. Physical adapters for VCL/FMX, DevExpress, and Qt
//          satisfy the concept.
// \details The wrapper is line-oriented. Its primary transport type is
//          std::basic_string_view<char_type>. Backends may use their own
//          string types internally, but must read and write string_view_type.
//          Text fields, list boxes, and combo boxes use the same abstraction.
// ============================================================================

#include "stream_tools.h"
#include "wrapper_basic.h"
#include "convert_core.h"

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstddef>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <variant>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include <print>

namespace adecc {

   namespace text {

      // ============================================================================
      // StreamPolicy extension for string_view_type
      // ============================================================================

      template <StreamPolicy stream_ty>
      struct StreamPolicyTraits {
         using char_type = typename stream_ty::char_type;
         using string_type = typename stream_ty::string_type;
         using string_view_type = std::basic_string_view<char_type, typename stream_ty::traits_type>;
      };

      template <class value_ty, class string_ty>
      concept line_convertible_to =
         std::convertible_to<value_ty, std::basic_string_view<typename string_ty::value_type, typename string_ty::traits_type>> ||
         std::same_as<std::remove_cvref_t<value_ty>, string_ty> ||
         requires(value_ty && v) {
            { ConvertTo<string_ty>(std::forward<value_ty>(v)) } -> std::same_as<string_ty>;
      };

      // ============================================================================
      // Backend Concepts
      // ============================================================================

      // Shared wrapper building blocks: reset/freeze/wait cursor/signal blocker are in wrapper_basic.h

      template <class backend_ty>
      concept text_type =
         adecc::wrapper::has_reset_for<backend_ty> &&
         adecc::wrapper::has_freeze_for<backend_ty> &&
         requires(backend_ty & a, std::size_t i, typename backend_ty::string_view_type svText) {
         typename backend_ty::char_type;
         typename backend_ty::string_type;
         typename backend_ty::string_view_type;
         typename backend_ty::stream_policy_type;
         { a.lines() }                -> std::convertible_to<std::size_t>;
         { a.get_line(i) }            -> std::same_as<typename backend_ty::string_type>;
         { a.set_line(i, svText) }    -> std::same_as<void>;
         { a.insert_line(i, svText) } -> std::same_as<void>;
         { a.append_line(svText) }    -> std::convertible_to<std::size_t>;
         { a.erase_line(i) }          -> std::same_as<void>;
         { a.current_line() }         -> std::convertible_to<std::size_t>;
         { a.focus_line(i) }          -> std::same_as<bool>;
      };

      template <class backend_ty>
      concept write_text_type =
         adecc::wrapper::has_reset_for<backend_ty> &&
         adecc::wrapper::has_freeze_for<backend_ty> &&
         requires(backend_ty & a, typename backend_ty::string_view_type svText) {
         typename backend_ty::char_type;
         typename backend_ty::string_type;
         typename backend_ty::string_view_type;
         typename backend_ty::stream_policy_type;
         { a.lines() }             -> std::convertible_to<std::size_t>;
         { a.append_line(svText) } -> std::convertible_to<std::size_t>;
      };

      // ============================================================================
      // Abstract line-oriented text wrapper without UI dependencies
      // ============================================================================

      /*!
       \brief     Generic text wrapper over a text_type backend
       \details   Provides random-access views, writable proxy views, and an
                  output sink for arbitrary input ranges.
       \tparam    Backend Concrete backend type satisfying text_type
      */
      template <text_type Backend>
      class TextModel {
      public:
         using backend_ty = Backend;
         using stream_policy_type = typename backend_ty::stream_policy_type;
         using char_type = typename backend_ty::char_type;
         using string_type = typename backend_ty::string_type;
         using string_view_type = typename backend_ty::string_view_type;
		 using size_type = std::size_t;

      public:
         TextModel() = default;

         explicit TextModel(backend_ty&& theBackend) : theBackend_{ std::move(theBackend) } {
            if (theBackend_) {
               iLines_ = backend_().lines();
            }
         }

         TextModel(TextModel const&) = delete;
         TextModel& operator=(TextModel const&) = delete;

         TextModel(TextModel&&) noexcept = default;
         TextModel& operator=(TextModel&&) noexcept = default;

         ~TextModel() = default;

         [[nodiscard]] size_type lines() const noexcept { return iLines_; }
         [[nodiscard]] size_type size() const noexcept { return iLines_; }
         [[nodiscard]] bool empty() const noexcept { return iLines_ == 0; }

         [[nodiscard]] size_type current_line() const {
            return backend_().current_line();
         }

         void reset() {
            if (theBackend_) {
               if constexpr (requires(backend_ty & a) { a.reset(true); }) {
                  backend_().reset(true);
               }
               else {
                  backend_().reset();
               }
               iLines_ = backend_().lines();
            }
         }

         void clear() {
            if (theBackend_) {
               if constexpr (requires(backend_ty & a) { a.reset(false); }) {
                  backend_().reset(false);
               }
               else {
                  backend_().reset();
               }
               iLines_ = backend_().lines();
            }
         }

         [[nodiscard]] bool freeze() {
            return backend_().freeze();
         }

         void unfreeze(bool const bFrozen) {
            backend_().unfreeze(bFrozen);
         }

         template <std::ranges::input_range range_ty>
            requires (!std::convertible_to<range_ty, string_view_type>)
         TextModel& operator = (range_ty&& theRange) {
            if (theBackend_) {
               clear();
               operator += (std::forward<range_ty>(theRange));
            }
            return *this;
         }

         TextModel& operator = (string_view_type const svText) {
            if (theBackend_) {
               clear();
               appendText(svText);
            }
            return *this;
         }

         TextModel& operator = (string_type const& strText) {
            return operator = (string_view_type{ strText });
         }

         TextModel& operator = (char_type const* szText) {
            return operator = (string_view_type{ szText });
         }

         template <class value_ty>
            requires (!std::ranges::input_range<value_ty> &&
         !std::same_as<std::remove_cvref_t<value_ty>, TextModel>&&
            line_convertible_to<value_ty, string_type>)
            TextModel& operator = (value_ty&& theValue) {
            if (theBackend_) {
               clear();
               appendLine(std::forward<value_ty>(theValue));
            }
            return *this;
         }

         template <std::ranges::input_range range_ty>
            requires (!std::convertible_to<range_ty, string_view_type>)
         TextModel& operator += (range_ty&& theRange) {
            if (theBackend_) {
               auto theSink = sink();
               std::ranges::copy(std::forward<range_ty>(theRange), theSink);
               iLines_ = backend_().lines();
            }
            return *this;
         }

         TextModel& operator += (string_view_type const svLine) {
            appendLine(svLine);
            return *this;
         }

         TextModel& operator += (string_type const& strLine) {
            appendLine(string_view_type{ strLine });
            return *this;
         }

         TextModel& operator += (char_type const* szLine) {
            appendLine(string_view_type{ szLine });
            return *this;
         }

         template <class value_ty>
            requires (!std::ranges::input_range<value_ty>&& line_convertible_to<value_ty, string_type>)
         TextModel& operator += (value_ty&& theValue) {
            appendLine(std::forward<value_ty>(theValue));
            return *this;
         }

         [[nodiscard]] string_type get(size_type const iLine) const {
            bounds_check_(iLine);
            return backend_().get_line(iLine);
         }

         void set(size_type const iLine, string_view_type const svLine) {
            bounds_check_(iLine);
            backend_().set_line(iLine, svLine);
         }

         void set(size_type const iLine, string_type const& strLine) {
            set(iLine, string_view_type{ strLine });
         }

         template <class value_ty>
            requires (!std::same_as<std::remove_cvref_t<value_ty>, string_type> &&
         !std::convertible_to<value_ty, string_view_type>&&
            line_convertible_to<value_ty, string_type>)
            void set(size_type const iLine, value_ty&& theValue) {
            auto strLine = ConvertLine_(std::forward<value_ty>(theValue));
            set(iLine, string_view_type{ strLine });
         }

         void insertLine(size_type const iLine, string_view_type const svLine) {
            if (iLine > iLines_) {
               throw std::out_of_range{ std::format("TextModel::insertLine: line {}", iLine) };
            }
            backend_().insert_line(iLine, svLine);
            iLines_ = backend_().lines();
         }

         size_type appendLine(string_view_type const svLine) {
            auto const iLine = backend_().append_line(svLine);
            iLines_ = backend_().lines();
            return iLine;
         }

         size_type appendLine(string_type const& strLine) {
            return appendLine(string_view_type{ strLine });
         }

         template <class value_ty>
            requires (!std::same_as<std::remove_cvref_t<value_ty>, string_type> &&
         !std::convertible_to<value_ty, string_view_type>&&
            line_convertible_to<value_ty, string_type>)
            size_type appendLine(value_ty&& theValue) {
            auto strLine = ConvertLine_(std::forward<value_ty>(theValue));
            return appendLine(string_view_type{ strLine });
         }

         template <class value_ty>
            requires (!std::same_as<std::remove_cvref_t<value_ty>, string_type> &&
         !std::convertible_to<value_ty, string_view_type>&&
            line_convertible_to<value_ty, string_type>)
            void insertLine(size_type const iLine, value_ty&& theValue) {
            auto strLine = ConvertLine_(std::forward<value_ty>(theValue));
            insertLine(iLine, string_view_type{ strLine });
         }

         void eraseLine(size_type const iLine) {
            bounds_check_(iLine);
            backend_().erase_line(iLine);
            iLines_ = backend_().lines();
         }

         bool focus_line(size_type const iLine) {
            return backend_().focus_line(iLine);
         }

         void appendText(string_view_type const svText) {
            size_type iBegin = 0;
            for (size_type i = 0; i < svText.size(); ++i) {
               if (svText[i] == static_cast<char_type>('\n')) {
                  size_type iEnd = i;
                  if (iEnd > iBegin && svText[iEnd - 1] == static_cast<char_type>('\r')) {
                     --iEnd;
                  }
                  appendLine(svText.substr(iBegin, iEnd - iBegin));
                  iBegin = i + 1;
               }
            }

            if (iBegin < svText.size()) {
               appendLine(svText.substr(iBegin));
            }
            else if (!svText.empty() && svText.back() == static_cast<char_type>('\n')) {
               appendLine(string_view_type{});
            }
         }

         [[nodiscard]] std::vector<string_type> to_vector() const {
            std::vector<string_type> vecLines;
            vecLines.reserve(lines());
            for (auto const& strLine : makeLineView()) {
               vecLines.push_back(strLine);
            }
            return vecLines;
         }

         using FreezeGuard = adecc::wrapper::FreezeGuard<TextModel>;

         [[nodiscard]] FreezeGuard freeze_guard() {
            return FreezeGuard{ *this };
         }

         template <class text_ty>
         class LineProxy {
         public:
            using size_type = typename TextModel::size_type;

            LineProxy() = default;
            LineProxy(text_ty* p, size_type const iLine) : p_{ p }, iLine_{ iLine } {}

            operator string_type() const {
               return p_->get(iLine_);
            }

            template <class value_ty>
               requires (!std::is_const_v<text_ty>&& line_convertible_to<value_ty, string_type>)
            LineProxy& operator = (value_ty&& theLine) {
               p_->set(iLine_, std::forward<value_ty>(theLine));
               return *this;
            }

         private:
            text_ty* p_ = nullptr;
            size_type iLine_ = 0;
         };

         using line_type = LineProxy<TextModel>;
         using const_line_type = LineProxy<TextModel const>;

         [[nodiscard]] line_type operator[](size_type const iLine) {
            bounds_check_(iLine);
            return line_type{ this, iLine };
         }

         [[nodiscard]] const_line_type operator[](size_type const iLine) const {
            bounds_check_(iLine);
            return const_line_type{ this, iLine };
         }

         class LineIter {
         public:
            using iterator_concept = std::random_access_iterator_tag;
            using iterator_category = std::random_access_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = string_type;
            using reference = value_type;

            LineIter() = default;
            LineIter(TextModel const* p, size_type const iLine) : p_{ p }, iLine_{ iLine } {}

            reference operator*() const { return p_->get(iLine_); }

            value_type operator[](difference_type const n) const {
               return p_->get(static_cast<size_type>(static_cast<difference_type>(iLine_) + n));
            }

            LineIter& operator++() { ++iLine_; return *this; }
            LineIter operator++(int) { auto tmp = *this; ++*this; return tmp; }
            LineIter& operator--() { --iLine_; return *this; }
            LineIter operator--(int) { auto tmp = *this; --*this; return tmp; }

            LineIter& operator += (difference_type const n) {
               iLine_ = static_cast<size_type>(static_cast<difference_type>(iLine_) + n);
               return *this;
            }

            LineIter& operator -= (difference_type const n) {
               iLine_ = static_cast<size_type>(static_cast<difference_type>(iLine_) - n);
               return *this;
            }

            friend LineIter operator + (LineIter it, difference_type const n) { it += n; return it; }
            friend LineIter operator - (LineIter it, difference_type const n) { it -= n; return it; }
            friend LineIter operator + (difference_type const n, LineIter it) { it += n; return it; }

            friend difference_type operator - (LineIter a, LineIter b) {
               return static_cast<difference_type>(a.iLine_) - static_cast<difference_type>(b.iLine_);
            }

            friend auto operator <=>(LineIter const&, LineIter const&) = default;

         private:
            TextModel const* p_ = nullptr;
            size_type iLine_ = 0;
         };

         class LineView : public std::ranges::view_interface<LineView> {
         public:
            LineView() = default;
            explicit LineView(TextModel const* p) : p_{ p } {}

            LineIter begin() const { return LineIter{ p_, 0 }; }
            LineIter end() const { return LineIter{ p_, p_->lines() }; }
            size_type size() const { return p_->lines(); }

         private:
            TextModel const* p_ = nullptr;
         };

         class LineRefProxy {
         public:
            LineRefProxy() = default;
            LineRefProxy(TextModel* p, std::ptrdiff_t const iLine) : p_{ p }, iLine_{ iLine } {}

            operator string_type() const {
               auto const n = static_cast<std::ptrdiff_t>(p_->lines());
               if (iLine_ < 0 || iLine_ >= n) {
                  throw std::out_of_range{ std::format("LineRefProxy::get: line {}", iLine_) };
               }
               return p_->get(static_cast<size_type>(iLine_));
            }

            LineRefProxy& operator = (string_view_type const svLine)& { write_(svLine); return *this; }
            LineRefProxy const& operator = (string_view_type const svLine) const& { write_(svLine); return *this; }
            LineRefProxy const& operator = (string_view_type const svLine) const&& { write_(svLine); return *this; }

            LineRefProxy& operator = (string_type const& strLine)& { write_(string_view_type{ strLine }); return *this; }
            LineRefProxy const& operator = (string_type const& strLine) const& { write_(string_view_type{ strLine }); return *this; }
            LineRefProxy const& operator = (string_type const& strLine) const&& { write_(string_view_type{ strLine }); return *this; }

            template <class value_ty>
               requires (!std::same_as<std::remove_cvref_t<value_ty>, string_type> &&
            !std::convertible_to<value_ty, string_view_type>&&
               line_convertible_to<value_ty, string_type>)
               LineRefProxy& operator = (value_ty&& theLine)& {
               write_(std::forward<value_ty>(theLine));
               return *this;
            }

            template <class value_ty>
               requires (!std::same_as<std::remove_cvref_t<value_ty>, string_type> &&
            !std::convertible_to<value_ty, string_view_type>&&
               line_convertible_to<value_ty, string_type>)
               LineRefProxy const& operator = (value_ty&& theLine) const& {
               write_(std::forward<value_ty>(theLine));
               return *this;
            }

            template <class value_ty>
               requires (!std::same_as<std::remove_cvref_t<value_ty>, string_type> &&
            !std::convertible_to<value_ty, string_view_type>&&
               line_convertible_to<value_ty, string_type>)
               LineRefProxy const& operator = (value_ty&& theLine) const&& {
               write_(std::forward<value_ty>(theLine));
               return *this;
            }

            friend void iter_swap(LineRefProxy a, LineRefProxy b) {
               auto strA = static_cast<string_type>(a);
               auto strB = static_cast<string_type>(b);
               a = string_view_type{ strB };
               b = string_view_type{ strA };
            }

            friend string_type iter_move(LineRefProxy const& a) {
               return static_cast<string_type>(a);
            }

         private:
            void write_(string_view_type const svLine) const {
               auto const n = static_cast<std::ptrdiff_t>(p_->lines());
               if (iLine_ < 0 || iLine_ >= n) {
                  throw std::out_of_range{ std::format("LineRefProxy::set: line {}", iLine_) };
               }
               p_->set(static_cast<size_type>(iLine_), svLine);
            }

            template <class value_ty>
            void write_(value_ty&& theLine) const {
               auto strLine = ConvertLine_(std::forward<value_ty>(theLine));
               write_(string_view_type{ strLine });
            }

         private:
            TextModel* p_ = nullptr;
            std::ptrdiff_t iLine_ = 0;
         };

         struct LineRefSentinel {
            std::ptrdiff_t n{};
         };

         class LineRefIter {
         public:
            using iterator_concept = std::random_access_iterator_tag;
            using iterator_category = std::random_access_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = string_type;
            using reference = LineRefProxy;

            LineRefIter() = default;
            LineRefIter(TextModel* p, std::ptrdiff_t const iLine) : p_{ p }, iLine_{ iLine } {}

            reference operator*() const { return reference{ p_, iLine_ }; }
            reference operator[](difference_type const n) const { return reference{ p_, iLine_ + n }; }

            LineRefIter& operator++() { ++iLine_; return *this; }
            LineRefIter operator++(int) { LineRefIter t = *this; ++*this; return t; }
            LineRefIter& operator--() { --iLine_; return *this; }
            LineRefIter operator--(int) { LineRefIter t = *this; --*this; return t; }

            LineRefIter& operator += (difference_type const n) { iLine_ += n; return *this; }
            LineRefIter& operator -= (difference_type const n) { iLine_ -= n; return *this; }

            friend LineRefIter operator + (LineRefIter it, difference_type const n) { it += n; return it; }
            friend LineRefIter operator - (LineRefIter it, difference_type const n) { it -= n; return it; }
            friend LineRefIter operator + (difference_type const n, LineRefIter it) { it += n; return it; }
            friend difference_type operator - (LineRefIter a, LineRefIter b) { return a.iLine_ - b.iLine_; }
            friend bool operator == (LineRefIter const& a, LineRefIter const& b) { return a.p_ == b.p_ && a.iLine_ == b.iLine_; }
            friend std::strong_ordering operator <=> (LineRefIter const& a, LineRefIter const& b) { return a.iLine_ <=> b.iLine_; }

            friend void iter_swap(LineRefIter a, LineRefIter b) {
               auto strA = static_cast<string_type>(*a);
               auto strB = static_cast<string_type>(*b);
               *a = string_view_type{ strB };
               *b = string_view_type{ strA };
            }

            friend string_type iter_move(LineRefIter it) {
               return static_cast<string_type>(*it);
            }

            friend bool operator == (LineRefIter it, LineRefSentinel s) { return it.iLine_ == s.n; }
            friend bool operator == (LineRefSentinel s, LineRefIter it) { return it.iLine_ == s.n; }
            friend difference_type operator - (LineRefSentinel s, LineRefIter it) { return s.n - it.iLine_; }
            friend difference_type operator - (LineRefIter it, LineRefSentinel s) { return it.iLine_ - s.n; }

         private:
            TextModel* p_ = nullptr;
            std::ptrdiff_t iLine_ = 0;
         };

         class LineRefView : public std::ranges::view_interface<LineRefView> {
         public:
            LineRefView() = default;
            explicit LineRefView(TextModel* p) : p_{ p } {}

            LineRefIter begin() const { return LineRefIter{ p_, 0 }; }
            LineRefSentinel end() const { return LineRefSentinel{ static_cast<std::ptrdiff_t>(p_->lines()) }; }
            size_type size() const { return p_->lines(); }

         private:
            TextModel* p_ = nullptr;
         };

         [[nodiscard]] LineView makeLineView() const { return LineView{ this }; }
         [[nodiscard]] LineRefView makeLineRefView() { return LineRefView{ this }; }

         class OutIter {
         public:
            using iterator_category = std::output_iterator_tag;
            using iterator_concept = std::output_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = void;
            using reference = void;
            using pointer = void;

            explicit OutIter(TextModel& theText) : pText_{ &theText } {}

            OutIter& operator = (string_view_type const svLine) {
               pText_->appendLine(svLine);
               return *this;
            }

            OutIter& operator = (string_type const& strLine) {
               pText_->appendLine(string_view_type{ strLine });
               return *this;
            }

            template <class value_ty>
               requires (!std::same_as<std::remove_cvref_t<value_ty>, string_type> &&
            !std::convertible_to<value_ty, string_view_type>&&
               line_convertible_to<value_ty, string_type>)
               OutIter& operator = (value_ty&& theLine) {
               pText_->appendLine(std::forward<value_ty>(theLine));
               return *this;
            }

            OutIter& operator*() { return *this; }
            OutIter& operator++() { return *this; }
            OutIter operator++(int) { OutIter tmp{ *this }; return tmp; }

         private:
            TextModel* pText_ = nullptr;
         };

         [[nodiscard]] OutIter sink() { return OutIter{ *this }; }

      private:
         friend class adecc::wrapper::FreezeGuard<TextModel>;

         template <class value_ty>
         static string_type ConvertLine_(value_ty&& theValue) {
            if constexpr (std::same_as<std::remove_cvref_t<value_ty>, string_type>) {
               return string_type{ std::forward<value_ty>(theValue) };
            }
            else if constexpr (std::convertible_to<value_ty, string_view_type>) {
               return string_type{ string_view_type{std::forward<value_ty>(theValue)} };
            }
            else {
               return ConvertTo<string_type>(std::forward<value_ty>(theValue));
            }
         }

         backend_ty& backend_() {
            if (!theBackend_) {
               throw std::logic_error("TextModel: backend not initialized");
            }
            return *theBackend_;
         }

         backend_ty const& backend_() const {
            if (!theBackend_) {
               throw std::logic_error("TextModel: backend not initialized");
            }
            return *theBackend_;
         }

         void bounds_check_(size_type const iLine) const {
#ifndef NDEBUG
            if (iLine >= iLines_) {
               throw std::out_of_range{ std::format("TextModel::line: index {} out of range", iLine) };
            }
#else
            (void)iLine;
#endif
         }

      private:
         std::optional<backend_ty> theBackend_;
         size_type iLines_{ 0 };
      };

      // ============================================================================
      // WriteOnly TextModel
      // ============================================================================

      /*!
       \brief     Writable text model for sequential text output
       \details   The model supports only append, reset, ranges, and an
                  output sink. It is intended for stream output, loggers, and reports.
       \tparam    Backend Backend type satisfying write_text_type
      */
      template <write_text_type Backend>
      class WriteTextModel {
      public:
         using backend_ty = Backend;
         using stream_policy_type = typename backend_ty::stream_policy_type;
         using char_type = typename backend_ty::char_type;
         using string_type = typename backend_ty::string_type;
         using string_view_type = typename backend_ty::string_view_type;
         using size_type = std::size_t;

         using FreezeGuard = adecc::wrapper::FreezeGuard<WriteTextModel>;

      public:
         WriteTextModel() = default;

         explicit WriteTextModel(backend_ty&& theBackend)
            : theBackend_{ std::move(theBackend) } {
            if (theBackend_) {
               iLines_ = backend_().lines();
            }
         }

         WriteTextModel(WriteTextModel const&) = delete;
         WriteTextModel& operator=(WriteTextModel const&) = delete;

         WriteTextModel(WriteTextModel&&) noexcept = default;
         WriteTextModel& operator=(WriteTextModel&&) noexcept = default;

         ~WriteTextModel() = default;

         [[nodiscard]] size_type lines() const noexcept {
            return iLines_;
         }

         [[nodiscard]] size_type size() const noexcept {
            return iLines_;
         }

         [[nodiscard]] bool empty() const noexcept {
            return iLines_ == 0;
         }

         void reset() {
            if (theBackend_) {
               backend_().reset(true);
               iLines_ = backend_().lines();
            }
         }

         void clear() {
            if (theBackend_) {
               backend_().reset(false);
               iLines_ = backend_().lines();
            }
         }

         [[nodiscard]] bool freeze() {
            return backend_().freeze();
         }

         void unfreeze(bool const bFrozen) {
            backend_().unfreeze(bFrozen);
         }

         [[nodiscard]] FreezeGuard freeze_guard() {
            return FreezeGuard{ *this };
         }

         template <std::ranges::input_range range_ty>
            requires (!std::convertible_to<range_ty, string_view_type>)
         WriteTextModel& operator = (range_ty&& theRange) {
            if (theBackend_) {
               clear();
               operator += (std::forward<range_ty>(theRange));
            }
            return *this;
         }

         WriteTextModel& operator = (string_view_type const svText) {
            if (theBackend_) {
               clear();
               appendText(svText);
            }
            return *this;
         }

         WriteTextModel& operator = (string_type const& strText) {
            return operator = (string_view_type{ strText });
         }

         WriteTextModel& operator = (char_type const* szText) {
            return operator = (string_view_type{ szText });
         }

         template <class value_ty>
            requires (!std::ranges::input_range<value_ty> &&
         !std::same_as<std::remove_cvref_t<value_ty>, WriteTextModel>&&
            line_convertible_to<value_ty, string_type>)
            WriteTextModel& operator = (value_ty&& theValue) {
            if (theBackend_) {
               clear();
               appendLine(std::forward<value_ty>(theValue));
            }
            return *this;
         }

         template <std::ranges::input_range range_ty>
            requires (!std::convertible_to<range_ty, string_view_type>)
         WriteTextModel& operator += (range_ty&& theRange) {
            if (theBackend_) {
               auto theSink = sink();
               std::ranges::copy(std::forward<range_ty>(theRange), theSink);
               iLines_ = backend_().lines();
            }
            return *this;
         }

         WriteTextModel& operator += (string_view_type const svLine) {
            appendLine(svLine);
            return *this;
         }

         WriteTextModel& operator += (string_type const& strLine) {
            appendLine(string_view_type{ strLine });
            return *this;
         }

         WriteTextModel& operator += (char_type const* szLine) {
            appendLine(string_view_type{ szLine });
            return *this;
         }

         template <class value_ty>
            requires (!std::ranges::input_range<value_ty>&&
         line_convertible_to<value_ty, string_type>)
            WriteTextModel& operator += (value_ty&& theValue) {
            appendLine(std::forward<value_ty>(theValue));
            return *this;
         }

         size_type appendLine(string_view_type const svLine) {
            auto const iLine = backend_().append_line(svLine);
            iLines_ = backend_().lines();
            return iLine;
         }

         size_type appendLine(string_type const& strLine) {
            return appendLine(string_view_type{ strLine });
         }

         template <class value_ty>
            requires (!std::same_as<std::remove_cvref_t<value_ty>, string_type> &&
         !std::convertible_to<value_ty, string_view_type>&&
            line_convertible_to<value_ty, string_type>)
            size_type appendLine(value_ty&& theValue) {
            auto strLine = ConvertLine_(std::forward<value_ty>(theValue));
            return appendLine(string_view_type{ strLine });
         }

         void appendText(string_view_type const svText) {
            size_type iBegin = 0;

            for (size_type i = 0; i < svText.size(); ++i) {
               if (svText[i] == static_cast<char_type>('\n')) {
                  size_type iEnd = i;

                  if (iEnd > iBegin && svText[iEnd - 1] == static_cast<char_type>('\r')) {
                     --iEnd;
                  }

                  appendLine(svText.substr(iBegin, iEnd - iBegin));
                  iBegin = i + 1;
               }
            }

            if (iBegin < svText.size()) {
               appendLine(svText.substr(iBegin));
            }
            else if (!svText.empty() && svText.back() == static_cast<char_type>('\n')) {
               appendLine(string_view_type{});
            }
         }

         class OutIter {
         public:
            using iterator_category = std::output_iterator_tag;
            using iterator_concept = std::output_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = void;
            using reference = void;
            using pointer = void;

            explicit OutIter(WriteTextModel& theText)
               : pText_{ &theText } {
            }

            OutIter& operator = (string_view_type const svLine) {
               pText_->appendLine(svLine);
               return *this;
            }

            OutIter& operator = (string_type const& strLine) {
               pText_->appendLine(strLine);
               return *this;
            }

            template <class value_ty>
               requires (!std::same_as<std::remove_cvref_t<value_ty>, string_type> &&
            !std::convertible_to<value_ty, string_view_type>&&
               line_convertible_to<value_ty, string_type>)
               OutIter& operator = (value_ty&& theLine) {
               auto strLine = WriteTextModel::ConvertLine_(std::forward<value_ty>(theLine));
               pText_->appendLine(string_view_type{ strLine });
               return *this;
            }

            OutIter& operator*() {
               return *this;
            }

            OutIter& operator++() {
               return *this;
            }

            OutIter operator++(int) {
               OutIter tmp{ *this };
               return tmp;
            }

         private:
            WriteTextModel* pText_ = nullptr;
         };

         [[nodiscard]] OutIter sink() {
            return OutIter{ *this };
         }

      private:
         friend class adecc::wrapper::FreezeGuard<WriteTextModel>;

         template <class value_ty>
         static string_type ConvertLine_(value_ty&& theValue) {
            if constexpr (std::same_as<std::remove_cvref_t<value_ty>, string_type>) {
               return string_type{ std::forward<value_ty>(theValue) };
            }
            else if constexpr (std::convertible_to<value_ty, string_view_type>) {
               return string_type{ string_view_type{std::forward<value_ty>(theValue)} };
            }
            else {
               return ConvertTo<string_type>(std::forward<value_ty>(theValue));
            }
         }

         backend_ty& backend_() {
            if (!theBackend_) {
               throw std::logic_error{ "WriteTextModel: backend not initialized" };
            }
            return *theBackend_;
         }

         backend_ty const& backend_() const {
            if (!theBackend_) {
               throw std::logic_error{ "WriteTextModel: backend not initialized" };
            }
            return *theBackend_;
         }

      private:
         std::optional<backend_ty> theBackend_;
         size_type iLines_{ 0 };
      };

      // ============================================================================
      // Traits and model detection
      // ============================================================================

      template <typename ty>
      struct TextModelTraits;

      template <text_type Backend>
      struct TextModelTraits<TextModel<Backend>> {
         using text_type = TextModel<Backend>;
         using backend_type = Backend;
         using stream_policy_type = typename text_type::stream_policy_type;
         using char_type = typename text_type::char_type;
         using string_type = typename text_type::string_type;
         using string_view_type = typename text_type::string_view_type;
         using size_type = typename text_type::size_type;
         using line_type = typename text_type::line_type;
         using const_line_type = typename text_type::const_line_type;
         using line_view_type = typename text_type::LineView;
         using line_ref_view_type = typename text_type::LineRefView;
         using out_iter_type = typename text_type::OutIter;
      };

      template <typename ty>
      struct is_TextModel : std::false_type {};

      template <text_type Backend>
      struct is_TextModel<TextModel<Backend>> : std::true_type {};

      template <typename ty>
      inline constexpr bool is_TextModel_v = is_TextModel<std::remove_cvref_t<ty>>::value;

      template <typename ty>
      concept text_model_type = is_TextModel_v<ty>;

      template <typename ty>
      struct WriteTextModelTraits;

      template <write_text_type Backend>
      struct WriteTextModelTraits<WriteTextModel<Backend>> {
         using text_type = WriteTextModel<Backend>;
         using backend_type = Backend;
         using stream_policy_type = typename text_type::stream_policy_type;
         using char_type = typename text_type::char_type;
         using string_type = typename text_type::string_type;
         using string_view_type = typename text_type::string_view_type;
         using size_type = typename text_type::size_type;
         using out_iter_type = typename text_type::OutIter;
      };

      template <typename ty>
      struct is_WriteTextModel : std::false_type {};

      template <write_text_type Backend>
      struct is_WriteTextModel<WriteTextModel<Backend>> : std::true_type {};

      template <typename ty>
      inline constexpr bool is_WriteTextModel_v =
         is_WriteTextModel<std::remove_cvref_t<ty>>::value;

      template <typename ty>
      concept write_text_model_type = is_WriteTextModel_v<ty>;

      template <typename ty>
      concept text_sink_model_type =
         text_model_type<ty> || write_text_model_type<ty>;

      // ============================================================================
      // In-memory backend for testing and as a reference implementation
      // ============================================================================

      template <StreamPolicy stream_ty = AnsiStreamPolicy>
      class VectorTextBackend {
      public:
         using stream_policy_type = stream_ty;
         using char_type = typename stream_ty::char_type;
         using string_type = typename stream_ty::string_type;
         using string_view_type = typename StreamPolicyTraits<stream_ty>::string_view_type;

         [[nodiscard]] std::size_t lines() const noexcept { return vecLines_.size(); }

         [[nodiscard]] string_type get_line(std::size_t const iLine) const {
            return vecLines_.at(iLine);
         }

         void set_line(std::size_t const iLine, string_view_type const svLine) {
            vecLines_.at(iLine) = string_type{ svLine };
         }

         void insert_line(std::size_t const iLine, string_view_type const svLine) {
            vecLines_.insert(vecLines_.begin() + static_cast<std::ptrdiff_t>(iLine), string_type{ svLine });
         }

         std::size_t append_line(string_view_type const svLine) {
            vecLines_.push_back(string_type{ svLine });
            return vecLines_.size() - 1;
         }

         void erase_line(std::size_t const iLine) {
            vecLines_.erase(vecLines_.begin() + static_cast<std::ptrdiff_t>(iLine));
         }

         void reset(bool = true) {
            vecLines_.clear();
         }

         [[nodiscard]] std::size_t current_line() const noexcept {
            return iCurrentLine_;
         }

         bool focus_line(std::size_t const iLine) {
            if (iLine >= vecLines_.size()) {
               return false;
            }
            iCurrentLine_ = iLine;
            return true;
         }

         bool freeze() {
            return false;
         }

         void unfreeze(bool const) {
         }

      private:
         std::vector<string_type> vecLines_;
         std::size_t iCurrentLine_ = 0;
      };

      // ============================================================================
      // std::ostream backend for WriteTextModel
      // ============================================================================

      template <StreamPolicy stream_ty = AnsiStreamPolicy>
      class OStreamTextBackend {
      public:
         using stream_policy_type = stream_ty;
         using char_type = typename stream_ty::char_type;
         using string_type = typename stream_ty::string_type;
         using string_view_type = typename StreamPolicyTraits<stream_ty>::string_view_type;
         using ostream_type = typename stream_ty::ostream;

         explicit OStreamTextBackend(ostream_type& os)
            : os_{ os } {
         }

         OStreamTextBackend(OStreamTextBackend const&) = delete;
         OStreamTextBackend& operator=(OStreamTextBackend const&) = delete;
         OStreamTextBackend& operator=(OStreamTextBackend&&) = delete;

         OStreamTextBackend(OStreamTextBackend&& rhs) noexcept
            : os_{ rhs.os_ }, iLines_{ rhs.iLines_ } {
            rhs.bMovedFrom_ = true;
            rhs.iLines_ = 0;
         }

         ~OStreamTextBackend() = default;

         [[nodiscard]] std::size_t lines() const noexcept {
            return iLines_;
         }

         std::size_t append_line(string_view_type const svLine) {
            Print_(string_type{ svLine });
            os_.put(stream_ty::cNL);
            auto const iLine = iLines_;
            ++iLines_;
            return iLine;
         }

         void reset(bool const bFull = true) {
            (void)bFull;
            iLines_ = 0;
         }

         [[nodiscard]] bool freeze() {
            return false;
         }

         void unfreeze(bool const) {
         }

      private:
         void Print_(string_type const& strText) {
            if constexpr (std::same_as<char_type, char>) {
               std::print(os_, "{}", strText);
            }
            else {
               os_ << strText;
            }
         }

      private:
         ostream_type& os_;
         std::size_t iLines_ = 0;
         bool bMovedFrom_ = false;
      };

      static_assert(
         text_type<VectorTextBackend<AnsiStreamPolicy>>,
         "TextBackend requirements not satisfied."
      );
      static_assert(
         text_model_type<TextModel<VectorTextBackend<AnsiStreamPolicy>>>,
         "TextModel requirements not satisfied."
      );

      static_assert(
         write_text_type<OStreamTextBackend<AnsiStreamPolicy>>,
         "WriteTextBackend requirements not satisfied."
      );
      static_assert(
         write_text_model_type<WriteTextModel<OStreamTextBackend<AnsiStreamPolicy>>>,
         "WriteTextModel requirements not satisfied."
      );

      // ============================================================================
      // Adapter sketches: method contract for physical UI adapters
      // ============================================================================

      /*!
       \brief   Expected contract for VCL/FMX Memo, RichEdit, ListBox, and ComboBox
       \details The adapter should encapsulate the concrete UI API and expose only the
                text_type contract. For VCL/FMX, Lines->Strings[i] or Items->Strings[i]
                is the natural access point.
       \note    RichEdit is deliberately treated as a plain-text line model here.
                Formatted runs can later be added through a dedicated rich_text policy.
      */
      template <class control_ty, StreamPolicy stream_ty = WideStreamPolicy>
      class VclFmxTextBackendTemplate;

      /*!
       \brief   Expected contract for Qt QTextEdit, QPlainTextEdit, QListWidget, and
                QComboBox
       \details For QTextEdit/QPlainTextEdit, the adapter may internally use
                QTextDocument/QTextBlock. For QListWidget and QComboBox,
                item(i)->text() or itemText(i) is the natural access point.
       \warning QString stores UTF-16. The adapter must convert between QString and
                string_view_type without creating views of temporary QString conversions.
                get_line therefore always returns string_type by value.
      */
      template <class widget_ty, StreamPolicy stream_ty = WideStreamPolicy>
      class QtTextBackendTemplate;

   } // end of namespace text
} // namespace adecc
