// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file text_grid_wrapper.h
\brief Text and HTML grid backends for rendering typed grid data without a GUI framework.

\details
Implements textual grid formatting, column widths, alignments, separators, and stream-oriented rendering
while satisfying the same backend contracts as graphical grids. It demonstrates that a grid is a projection
of typed data rather than the owner of that data.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "The Grid as a Projection, Not as Truth".
- "Grid Models as the Next Step".
- "Text Wrapper and the Universal Wrapper Concept".
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

#include "stream_tools.h"
#include "convert_core.h"
#include "type_lists.h"
#include "value_types.h"
#include "text_wrapper.h"
#include "wrapper_basic.h"
#include "grid_backend_concepts.h"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <format>
#include <optional>
#include <print>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace adecc::grid {

   // ============================================================================
   // Block 1: Gemeinsame Formatbasis
   // Concepts are defined in grid_backend_concepts.h
   // ============================================================================

   template <adecc::StreamPolicy SP = adecc::AnsiStreamPolicy>
   class TextGridFormatBase {
   public:
      using size_type = std::size_t;
      using string_ty = typename SP::string_type;
      using char_ty = typename SP::char_type;

      struct SColumn {
         string_ty      strCaption{};
         int            iWidth{};
         EAlignmentType eAlignment{ EAlignmentType::left };
      };

      struct STextSeparators {
         string_ty strRowBegin{};
         string_ty strColSep{ string_ty{1, static_cast<char_ty>(' ')} };
         string_ty strRowEnd{};

         string_ty strRuleBegin{};
         string_ty strRuleSep{ string_ty{1, static_cast<char_ty>(' ')} };
         string_ty strRuleEnd{};
      };

      void set_text_separators(STextSeparators const& theSeparators) {
         theSeparators_ = theSeparators;
      }

      [[nodiscard]] STextSeparators const& text_separators() const noexcept {
         return theSeparators_;
      }

      static string_ty MakeString(std::string_view const svText) {
         string_ty strResult;
         strResult.reserve(svText.size());

         for (char const ch : svText) {
            strResult.push_back(static_cast<char_ty>(ch));
         }

         return strResult;
      }

      static STextSeparators PlainSeparators() {
         return STextSeparators{};
      }

      static STextSeparators MakeSeparators(std::string_view const svRowBegin,
         std::string_view const svColSep,
         std::string_view const svRowEnd) {
         auto theSeparators = PlainSeparators();

         theSeparators.strRowBegin = MakeString(svRowBegin);
         theSeparators.strColSep = MakeString(svColSep);
         theSeparators.strRowEnd = MakeString(svRowEnd);

         return theSeparators;
      }

      static STextSeparators MakeSeparators(std::string_view const svRowBegin,
         std::string_view const svColSep,
         std::string_view const svRowEnd,
         std::string_view const svRuleBegin,
         std::string_view const svRuleSep,
         std::string_view const svRuleEnd) {
         auto theSeparators = MakeSeparators(svRowBegin, svColSep, svRowEnd);

         theSeparators.strRuleBegin = MakeString(svRuleBegin);
         theSeparators.strRuleSep = MakeString(svRuleSep);
         theSeparators.strRuleEnd = MakeString(svRuleEnd);

         return theSeparators;
      }

      static STextSeparators TableSeparators() {
         return MakeSeparators("| ", " | ", " |", "+-", "-+-", "-+");
      }

      static STextSeparators CompactTableSeparators() {
         return MakeSeparators("|", "|", "|", "+", "+", "+");
      }

      template <class ty>
      static EAlignmentType ReadAlignment(ty const& theValue) {
         using value_ty = std::remove_cvref_t<ty>;

         if constexpr (std::is_same_v<value_ty, EAlignmentType>) {
            return theValue;
         }
         else if constexpr (std::is_integral_v<value_ty>) {
            switch (static_cast<int>(theValue)) {
            case 2:  return EAlignmentType::right;
            case 3:  return EAlignmentType::center;
            case 1: [[fallthrough]];
            default: return EAlignmentType::left;
            }
         }
         else {
            return EAlignmentType::left;
         }
      }

      static size_type DisplayWidth(string_ty const& strValue) {
         if constexpr (std::same_as<char_ty, char>) {
            size_type iWidth = 0;

            for (char_ty const ch : strValue) {
               auto const uCh = static_cast<unsigned char>(ch);

               if ((uCh & 0xC0U) != 0x80U) {
                  ++iWidth;
               }
            }

            return iWidth;
         }
         else {
            return strValue.size();
         }
      }

      static string_ty FormatCell(string_ty const& strValue,
         int const iWidth,
         EAlignmentType const eAlignment) {
         int const iEffectiveWidth = (std::max)(0, iWidth);
         size_type const iDisplayWidth = DisplayWidth(strValue);

         if (iEffectiveWidth == 0 || static_cast<int>(iDisplayWidth) >= iEffectiveWidth) {
            return strValue;
         }

         std::size_t const uPadding =
            static_cast<std::size_t>(iEffectiveWidth - static_cast<int>(iDisplayWidth));

         switch (eAlignment) {
         case EAlignmentType::right:
            return string_ty(uPadding, static_cast<char_ty>(' ')) + strValue;

         case EAlignmentType::center: {
            std::size_t const uLeft = uPadding / 2;
            std::size_t const uRight = uPadding - uLeft;
            return string_ty(uLeft, static_cast<char_ty>(' ')) +
               strValue +
               string_ty(uRight, static_cast<char_ty>(' '));
         }

         case EAlignmentType::left:
         case EAlignmentType::unknown:
         default:
            return strValue + string_ty(uPadding, static_cast<char_ty>(' '));
         }
      }

   protected:
      std::vector<SColumn> vecCols_{};
      STextSeparators theSeparators_{};
      size_type iRows_ = 0;
      size_type iCurrentCol_ = 0;
   };

   // ============================================================================
   // Block 2: WriteOnlyGrid Modell
   // ============================================================================

   template <sequential_write_grid_backend_type Backend>
   class WriteGridModel {
   public:
      using backend_ty = Backend;
      using size_type = std::size_t;

      using FreezeGuard = adecc::wrapper::FreezeGuard<WriteGridModel>;

      WriteGridModel() = default;

      template <class... Args>
         requires has_write_set_caption_for<Backend, Args...>
      explicit WriteGridModel(backend_ty&& theBackend,
         std::vector<std::tuple<Args...>> const& vecCaps,
         bool const bClear = true)
         : theBackend_{ std::move(theBackend) } {
         if (theBackend_) {
            auto theGuard = freeze_guard();
            theBackend_->template set_caption<Args...>(vecCaps, bClear);
            iRows_ = theBackend_->rows();
            iCols_ = theBackend_->columns();
         }
      }

      WriteGridModel(WriteGridModel const&) = delete;
      WriteGridModel& operator=(WriteGridModel const&) = delete;

      WriteGridModel(WriteGridModel&&) noexcept = default;
      WriteGridModel& operator=(WriteGridModel&&) noexcept = default;

      ~WriteGridModel() = default;

      [[nodiscard]] size_type rows() const noexcept {
         return iRows_;
      }

      [[nodiscard]] size_type columns() const noexcept {
         return iCols_;
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

      void reset(bool const bFull = true) {
         auto theGuard = freeze_guard();
         backend_().reset(bFull);
         iRows_ = backend_().rows();
         iCols_ = backend_().columns();
      }

      template <class... Args>
         requires has_write_set_caption_for<Backend, Args...>
      WriteGridModel& operator=(std::vector<std::tuple<Args...>> const& vecCaps) {
         auto theGuard = freeze_guard();

         backend_().template set_caption<Args...>(vecCaps, true);

         iRows_ = backend_().rows();
         iCols_ = backend_().columns();

         return *this;
      }

      template <std::ranges::input_range range_ty>
         requires is_tuple_v<std::ranges::range_value_t<range_ty>>
      WriteGridModel& operator=(range_ty&& theRange) {
         auto theGuard = freeze_guard();

         backend_().reset(false);
         AppendRange_(std::forward<range_ty>(theRange));

         iRows_ = backend_().rows();
         iCols_ = backend_().columns();

         return *this;
      }

      void appendRow() {
         backend_().append_row();
         iRows_ = backend_().rows();
         iCols_ = backend_().columns();
      }

      template <class ty>
      void appendCol(ty const& theValue) {
         backend_().template append_col<ty>(theValue);
         iCols_ = backend_().columns();
      }

      template <class... Ts>
      void appendRow(std::tuple<Ts...> const& theTuple) {
         appendRow();
         std::apply([this](auto const&... xs) {
            (appendCol(xs), ...);
            }, theTuple);
      }

      template <class... Ts>
      void appendRow(std::tuple<Ts...>&& theTuple) {
         appendRow();
         std::apply([this](auto&&... xs) {
            (appendCol(std::forward<decltype(xs)>(xs)), ...);
            }, std::move(theTuple));
      }

      template <class... Ts>
      WriteGridModel& operator+=(std::tuple<Ts...> const& theTuple) {
         appendRow(theTuple);
         return *this;
      }

      template <class... Ts>
      WriteGridModel& operator+=(std::tuple<Ts...>&& theTuple) {
         appendRow(std::move(theTuple));
         return *this;
      }

      template <std::ranges::input_range range_ty>
         requires is_tuple_v<std::ranges::range_value_t<range_ty>>
      WriteGridModel& operator+=(range_ty&& theRange) {
         auto theGuard = freeze_guard();

         AppendRange_(std::forward<range_ty>(theRange));

         iRows_ = backend_().rows();
         iCols_ = backend_().columns();

         return *this;
      }

      template <class... Ts>
      class OutIter {
      public:
         using iterator_category = std::output_iterator_tag;
         using iterator_concept = std::output_iterator_tag;
         using difference_type = std::ptrdiff_t;
         using value_type = void;
         using reference = void;
         using pointer = void;

         explicit OutIter(WriteGridModel& theGrid)
            : pGrid_{ &theGrid } {
         }

         OutIter& operator=(std::tuple<Ts...> const& theTuple) {
            pGrid_->appendRow(theTuple);
            return *this;
         }

         OutIter& operator=(std::tuple<Ts...>&& theTuple) {
            pGrid_->appendRow(std::move(theTuple));
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
         WriteGridModel* pGrid_ = nullptr;
      };

      template <class... Ts>
      [[nodiscard]] OutIter<Ts...> sink() {
         return OutIter<Ts...>{*this};
      }

   private:
      friend class adecc::wrapper::FreezeGuard<WriteGridModel>;

      backend_ty& backend_() {
         if (!theBackend_) {
            throw std::logic_error{ "WriteGridModel: backend not initialized" };
         }
         return *theBackend_;
      }

      backend_ty const& backend_() const {
         if (!theBackend_) {
            throw std::logic_error{ "WriteGridModel: backend not initialized" };
         }
         return *theBackend_;
      }

      template <std::ranges::input_range range_ty>
      void AppendRange_(range_ty&& theRange) {
         using tuple_ty = std::remove_cvref_t<std::ranges::range_value_t<range_ty>>;

         [&] <std::size_t... I>(std::index_sequence<I...>) {
            auto theSink = this->template sink<std::tuple_element_t<I, tuple_ty>...>();
            std::ranges::copy(std::forward<range_ty>(theRange), theSink);
         }(std::make_index_sequence<std::tuple_size_v<tuple_ty>>{});
      }

   private:
      std::optional<backend_ty> theBackend_;
      size_type iRows_{ 0 };
      size_type iCols_{ 0 };
   };

   template <typename ty>
   struct is_WriteGridModel : std::false_type {};

   template <sequential_write_grid_backend_type Backend>
   struct is_WriteGridModel<WriteGridModel<Backend>> : std::true_type {};

   template <typename ty>
   inline constexpr bool is_WriteGridModel_v =
      is_WriteGridModel<std::remove_cvref_t<ty>>::value;

   template <typename ty>
   concept write_grid_model_type = is_WriteGridModel_v<ty>;

   // ============================================================================
   // Block 3a: output to std::ostream / std::wostream
   // ============================================================================

   template <adecc::StreamPolicy SP = adecc::AnsiStreamPolicy>
   class OStreamGridBackend : private TextGridFormatBase<SP> {
   public:
      using base_ty = TextGridFormatBase<SP>;
      using size_type = typename base_ty::size_type;
      using ostream_ty = typename SP::ostream;
      using string_ty = typename SP::string_type;
      using char_ty = typename SP::char_type;
      using separators_ty = typename base_ty::STextSeparators;

      explicit OStreamGridBackend(ostream_ty& os)
         : os_{ os } {
      }

      explicit OStreamGridBackend(ostream_ty& os, separators_ty const& theSeparators)
         : os_{ os } {
         this->set_text_separators(theSeparators);
      }

      OStreamGridBackend(OStreamGridBackend const&) = delete;
      OStreamGridBackend& operator=(OStreamGridBackend const&) = delete;
      OStreamGridBackend& operator=(OStreamGridBackend&&) = delete;

      OStreamGridBackend(OStreamGridBackend&& rhs) noexcept
         : os_{ rhs.os_ }, bClosed_{ rhs.bClosed_ } {
         this->vecCols_ = std::move(rhs.vecCols_);
         this->theSeparators_ = std::move(rhs.theSeparators_);
         this->iRows_ = rhs.iRows_;
         this->iCurrentCol_ = rhs.iCurrentCol_;

         rhs.bClosed_ = true;
         rhs.bMovedFrom_ = true;
         rhs.iRows_ = 0;
         rhs.iCurrentCol_ = 0;
         rhs.vecCols_.clear();
      }

      ~OStreamGridBackend() {
         if (!bMovedFrom_) {
            reset(true);
         }
      }

      void set_text_separators(separators_ty const& theSeparators) {
         base_ty::set_text_separators(theSeparators);
      }

      [[nodiscard]] separators_ty const& text_separators() const noexcept {
         return base_ty::text_separators();
      }

      static string_ty MakeString(std::string_view const svText) {
         return base_ty::MakeString(svText);
      }

      static separators_ty PlainSeparators() {
         return base_ty::PlainSeparators();
      }

      static separators_ty MakeSeparators(std::string_view const svRowBegin,
         std::string_view const svColSep,
         std::string_view const svRowEnd) {
         return base_ty::MakeSeparators(svRowBegin, svColSep, svRowEnd);
      }

      static separators_ty MakeSeparators(std::string_view const svRowBegin,
         std::string_view const svColSep,
         std::string_view const svRowEnd,
         std::string_view const svRuleBegin,
         std::string_view const svRuleSep,
         std::string_view const svRuleEnd) {
         return base_ty::MakeSeparators(svRowBegin, svColSep, svRowEnd,
            svRuleBegin, svRuleSep, svRuleEnd);
      }

      static separators_ty TableSeparators() {
         return base_ty::TableSeparators();
      }

      static separators_ty CompactTableSeparators() {
         return base_ty::CompactTableSeparators();
      }

      template <adecc::StreamPolicy CSP = SP>
      void set_caption(adecc::vecCaptions<CSP> const& caps, bool const bClear = true) {
         if (bClear) {
            reset(true);
         }

         this->vecCols_.clear();
         this->vecCols_.reserve(caps.size());

         for (auto const& t : caps) {
            this->vecCols_.push_back(typename base_ty::SColumn{
               .strCaption = adecc::ConvertTo<string_ty>(std::get<0>(t)),
               .iWidth = static_cast<int>(std::get<1>(t)),
               .eAlignment = std::get<2>(t)
               });
         }

         this->iRows_ = 0;
         this->iCurrentCol_ = 0;
         bClosed_ = false;
         WriteHeader_();
      }

      template <class... Args>
         requires (sizeof...(Args) >= 3)
      void set_caption(std::vector<std::tuple<Args...>> const& caps, bool const bClear = true) {
         if (bClear) {
            reset(true);
         }

         this->vecCols_.clear();
         this->vecCols_.reserve(caps.size());

         for (auto const& t : caps) {
            this->vecCols_.push_back(typename base_ty::SColumn{
               .strCaption = adecc::ConvertTo<string_ty>(std::get<0>(t)),
               .iWidth = static_cast<int>(std::get<1>(t)),
               .eAlignment = base_ty::ReadAlignment(std::get<2>(t))
               });
         }

         this->iRows_ = 0;
         this->iCurrentCol_ = 0;
         bClosed_ = false;
         WriteHeader_();
      }

      void append_row() {
         if (this->iRows_ > 0 || this->iCurrentCol_ != 0) {
            WriteRowEnd_();
            PrintNewLine_();
         }

         bClosed_ = false;
         this->iCurrentCol_ = 0;
         ++this->iRows_;

         WriteRowBegin_();
      }

      template <class ty>
         requires adecc::is_in_type_list_v<ty, adecc::defined_param_types>
      void append_col(ty const& theValue) {
         WriteCell_(adecc::ConvertTo<string_ty>(theValue));
      }

      template <class opt_ty>
         requires (adecc::is_optional_v<opt_ty>&&
      adecc::is_in_type_list_v<typename opt_ty::value_type, adecc::defined_param_types>)
         void append_col(opt_ty const& theValue) {
         WriteCell_(theValue ? adecc::ConvertTo<string_ty>(*theValue) : string_ty{});
      }

      [[nodiscard]] size_type rows() const noexcept {
         return this->iRows_;
      }

      [[nodiscard]] size_type columns() const noexcept {
         return this->vecCols_.size();
      }

      void reset(bool const bFull = true) {
         FinishCurrentRow_();

         if (!bClosed_) {
            PrintNewLine_();
         }

         this->iRows_ = 0;
         this->iCurrentCol_ = 0;

         if (bFull) {
            this->vecCols_.clear();
            bClosed_ = true;
         }
         else {
            bClosed_ = false;

            if (!this->vecCols_.empty()) {
               WriteHeader_();
            }
         }
      }

      [[nodiscard]] bool freeze() {
         return false;
      }

      void unfreeze(bool const) {
      }

   private:
      void Print_(string_ty const& strValue) {
         if constexpr (std::same_as<char_ty, char>) {
            std::print(os_, "{}", strValue);
         }
         else {
            os_ << strValue;
         }
      }

      void PrintNewLine_() {
         os_.put(SP::cNL);
      }

      void WriteRowBegin_() {
         Print_(this->theSeparators_.strRowBegin);
      }

      void WriteRowSep_() {
         Print_(this->theSeparators_.strColSep);
      }

      void WriteRowEnd_() {
         Print_(this->theSeparators_.strRowEnd);
      }

      void WriteRuleBegin_() {
         Print_(this->theSeparators_.strRuleBegin);
      }

      void WriteRuleSep_() {
         Print_(this->theSeparators_.strRuleSep);
      }

      void WriteRuleEnd_() {
         Print_(this->theSeparators_.strRuleEnd);
      }

      void FinishCurrentRow_() {
         if (this->iCurrentCol_ != 0) {
            WriteRowEnd_();
            PrintNewLine_();
            this->iCurrentCol_ = 0;
         }
      }

      void WriteHeader_() {
         WriteRowBegin_();

         for (size_type i = 0; i < this->vecCols_.size(); ++i) {
            if (i > 0) {
               WriteRowSep_();
            }

            auto const& theCol = this->vecCols_[i];
            Print_(base_ty::FormatCell(theCol.strCaption, theCol.iWidth, theCol.eAlignment));
         }

         WriteRowEnd_();
         PrintNewLine_();

         WriteSeparatorLine_();
         PrintNewLine_();
      }

      void WriteSeparatorLine_() {
         WriteRuleBegin_();

         for (size_type i = 0; i < this->vecCols_.size(); ++i) {
            if (i > 0) {
               WriteRuleSep_();
            }

            int const iWidth = (std::max)(1, this->vecCols_[i].iWidth);
            Print_(string_ty(static_cast<std::size_t>(iWidth), static_cast<char_ty>('-')));
         }

         WriteRuleEnd_();
      }

      void WriteCell_(string_ty const& strValue) {
         bClosed_ = false;

         if (this->iCurrentCol_ > 0) {
            WriteRowSep_();
         }

         if (this->iCurrentCol_ < this->vecCols_.size()) {
            auto const& theCol = this->vecCols_[this->iCurrentCol_];
            Print_(base_ty::FormatCell(strValue, theCol.iWidth, theCol.eAlignment));
         }
         else {
            Print_(strValue);
         }

         ++this->iCurrentCol_;
      }

   private:
      ostream_ty& os_;
      bool bClosed_ = true;
      bool bMovedFrom_ = false;
   };

   // ============================================================================
   // Block 3b: output to adecc::text::TextModel
   // ============================================================================

   template <adecc::text::text_sink_model_type TextModelTy>
   class TextModelGridBackend : private TextGridFormatBase<typename TextModelTy::stream_policy_type> {
   public:
      using stream_policy_type = typename TextModelTy::stream_policy_type;
      using base_ty = TextGridFormatBase<stream_policy_type>;
      using size_type = typename base_ty::size_type;
      using string_ty = typename base_ty::string_ty;
      using char_ty = typename base_ty::char_ty;
      using separators_ty = typename base_ty::STextSeparators;

      explicit TextModelGridBackend(TextModelTy& theText)
         : theText_{ theText } {
      }

      explicit TextModelGridBackend(TextModelTy& theText, separators_ty const& theSeparators)
         : theText_{ theText } {
         this->set_text_separators(theSeparators);
      }

      TextModelGridBackend(TextModelGridBackend const&) = delete;
      TextModelGridBackend& operator=(TextModelGridBackend const&) = delete;
      TextModelGridBackend& operator=(TextModelGridBackend&&) = delete;

      TextModelGridBackend(TextModelGridBackend&& rhs) noexcept
         : theText_{ rhs.theText_ }, strCurrentRow_{ std::move(rhs.strCurrentRow_) },
         bClosed_{ rhs.bClosed_ } {
         this->vecCols_ = std::move(rhs.vecCols_);
         this->theSeparators_ = std::move(rhs.theSeparators_);
         this->iRows_ = rhs.iRows_;
         this->iCurrentCol_ = rhs.iCurrentCol_;

         rhs.bClosed_ = true;
         rhs.bMovedFrom_ = true;
         rhs.iRows_ = 0;
         rhs.iCurrentCol_ = 0;
         rhs.strCurrentRow_.clear();
         rhs.vecCols_.clear();
      }

      ~TextModelGridBackend() {
         if (!bMovedFrom_) {
            reset(true);
         }
      }

      void set_text_separators(separators_ty const& theSeparators) {
         base_ty::set_text_separators(theSeparators);
      }

      [[nodiscard]] separators_ty const& text_separators() const noexcept {
         return base_ty::text_separators();
      }

      static string_ty MakeString(std::string_view const svText) {
         return base_ty::MakeString(svText);
      }

      static separators_ty PlainSeparators() {
         return base_ty::PlainSeparators();
      }

      static separators_ty MakeSeparators(std::string_view const svRowBegin,
         std::string_view const svColSep,
         std::string_view const svRowEnd) {
         return base_ty::MakeSeparators(svRowBegin, svColSep, svRowEnd);
      }

      static separators_ty MakeSeparators(std::string_view const svRowBegin,
         std::string_view const svColSep,
         std::string_view const svRowEnd,
         std::string_view const svRuleBegin,
         std::string_view const svRuleSep,
         std::string_view const svRuleEnd) {
         return base_ty::MakeSeparators(svRowBegin, svColSep, svRowEnd,
            svRuleBegin, svRuleSep, svRuleEnd);
      }

      static separators_ty TableSeparators() {
         return base_ty::TableSeparators();
      }

      static separators_ty CompactTableSeparators() {
         return base_ty::CompactTableSeparators();
      }

      template <adecc::StreamPolicy CSP = stream_policy_type>
      void set_caption(adecc::vecCaptions<CSP> const& caps, bool const bClear = true) {
         if (bClear) {
            reset(true);
         }

         this->vecCols_.clear();
         this->vecCols_.reserve(caps.size());

         for (auto const& t : caps) {
            this->vecCols_.push_back(typename base_ty::SColumn{
               .strCaption = adecc::ConvertTo<string_ty>(std::get<0>(t)),
               .iWidth = static_cast<int>(std::get<1>(t)),
               .eAlignment = std::get<2>(t)
               });
         }

         this->iRows_ = 0;
         this->iCurrentCol_ = 0;
         strCurrentRow_.clear();
         bClosed_ = false;
         WriteHeader_();
      }

      template <class... Args>
         requires (sizeof...(Args) >= 3)
      void set_caption(std::vector<std::tuple<Args...>> const& caps, bool const bClear = true) {
         if (bClear) {
            reset(true);
         }

         this->vecCols_.clear();
         this->vecCols_.reserve(caps.size());

         for (auto const& t : caps) {
            this->vecCols_.push_back(typename base_ty::SColumn{
               .strCaption = adecc::ConvertTo<string_ty>(std::get<0>(t)),
               .iWidth = static_cast<int>(std::get<1>(t)),
               .eAlignment = base_ty::ReadAlignment(std::get<2>(t))
               });
         }

         this->iRows_ = 0;
         this->iCurrentCol_ = 0;
         strCurrentRow_.clear();
         bClosed_ = false;
         WriteHeader_();
      }

      void append_row() {
         FlushRow_();
         bClosed_ = false;
         this->iCurrentCol_ = 0;
         strCurrentRow_.clear();
         ++this->iRows_;

         WriteRowBegin_();
      }

      template <class ty>
         requires adecc::is_in_type_list_v<ty, adecc::defined_param_types>
      void append_col(ty const& theValue) {
         WriteCell_(adecc::ConvertTo<string_ty>(theValue));
      }

      template <class opt_ty>
         requires (adecc::is_optional_v<opt_ty>&&
      adecc::is_in_type_list_v<typename opt_ty::value_type, adecc::defined_param_types>)
         void append_col(opt_ty const& theValue) {
         WriteCell_(theValue ? adecc::ConvertTo<string_ty>(*theValue) : string_ty{});
      }

      [[nodiscard]] size_type rows() const noexcept {
         return this->iRows_;
      }

      [[nodiscard]] size_type columns() const noexcept {
         return this->vecCols_.size();
      }

      void reset(bool const bFull = true) {
         FlushRow_();

         if (!bClosed_) {
            theText_.appendLine(typename TextModelTy::string_view_type{});
         }

         this->iRows_ = 0;
         this->iCurrentCol_ = 0;
         strCurrentRow_.clear();

         if (bFull) {
            this->vecCols_.clear();
            bClosed_ = true;
         }
         else {
            bClosed_ = false;

            if (!this->vecCols_.empty()) {
               WriteHeader_();
            }
         }
      }

      [[nodiscard]] bool freeze() {
         return theText_.freeze();
      }

      void unfreeze(bool const bFrozen) {
         theText_.unfreeze(bFrozen);
      }

   private:
      void AppendToRow_(string_ty const& strValue) {
         strCurrentRow_ += strValue;
      }

      void WriteRowBegin_() {
         AppendToRow_(this->theSeparators_.strRowBegin);
      }

      void WriteRowSep_() {
         AppendToRow_(this->theSeparators_.strColSep);
      }

      void WriteRowEnd_() {
         AppendToRow_(this->theSeparators_.strRowEnd);
      }

      void WriteRuleBegin_() {
         AppendToRow_(this->theSeparators_.strRuleBegin);
      }

      void WriteRuleSep_() {
         AppendToRow_(this->theSeparators_.strRuleSep);
      }

      void WriteRuleEnd_() {
         AppendToRow_(this->theSeparators_.strRuleEnd);
      }

      void WriteHeader_() {
         strCurrentRow_.clear();

         WriteRowBegin_();

         for (size_type i = 0; i < this->vecCols_.size(); ++i) {
            if (i > 0) {
               WriteRowSep_();
            }

            auto const& theCol = this->vecCols_[i];
            AppendToRow_(base_ty::FormatCell(theCol.strCaption, theCol.iWidth, theCol.eAlignment));
         }

         WriteRowEnd_();
         theText_.appendLine(typename TextModelTy::string_view_type{ strCurrentRow_ });

         strCurrentRow_.clear();

         WriteSeparatorLine_();
         theText_.appendLine(typename TextModelTy::string_view_type{ strCurrentRow_ });
         strCurrentRow_.clear();
      }

      void WriteSeparatorLine_() {
         WriteRuleBegin_();

         for (size_type i = 0; i < this->vecCols_.size(); ++i) {
            if (i > 0) {
               WriteRuleSep_();
            }

            int const iWidth = (std::max)(1, this->vecCols_[i].iWidth);
            AppendToRow_(string_ty(static_cast<std::size_t>(iWidth), static_cast<char_ty>('-')));
         }

         WriteRuleEnd_();
      }

      void WriteCell_(string_ty const& strValue) {
         bClosed_ = false;

         if (this->iCurrentCol_ > 0) {
            WriteRowSep_();
         }

         if (this->iCurrentCol_ < this->vecCols_.size()) {
            auto const& theCol = this->vecCols_[this->iCurrentCol_];
            AppendToRow_(base_ty::FormatCell(strValue, theCol.iWidth, theCol.eAlignment));
         }
         else {
            AppendToRow_(strValue);
         }

         ++this->iCurrentCol_;
      }

      void FlushRow_() {
         if (!strCurrentRow_.empty()) {
            if (this->iCurrentCol_ != 0) {
               WriteRowEnd_();
            }

            theText_.appendLine(typename TextModelTy::string_view_type{ strCurrentRow_ });
            strCurrentRow_.clear();
            this->iCurrentCol_ = 0;
         }
      }

   private:
      TextModelTy& theText_;
      string_ty strCurrentRow_{};
      bool bClosed_ = true;
      bool bMovedFrom_ = false;
   };

   // ============================================================================
   // Block 4: HTML output to std::ostream / std::wostream
   // ============================================================================

   template <adecc::StreamPolicy SP = adecc::AnsiStreamPolicy>
   class HtmlOStreamGridBackend {
   public:
      using size_type = std::size_t;
      using ostream_ty = typename SP::ostream;
      using string_ty = typename SP::string_type;
      using char_ty = typename SP::char_type;

      explicit HtmlOStreamGridBackend(ostream_ty& os)
         : os_{ os } {
      }

      explicit HtmlOStreamGridBackend(ostream_ty& os, int const iWidthScalePx)
         : os_{ os }, iWidthScalePx_{ (std::max)(1, iWidthScalePx) } {
      }

      HtmlOStreamGridBackend(HtmlOStreamGridBackend const&) = delete;
      HtmlOStreamGridBackend& operator=(HtmlOStreamGridBackend const&) = delete;
      HtmlOStreamGridBackend& operator=(HtmlOStreamGridBackend&&) = delete;

      HtmlOStreamGridBackend(HtmlOStreamGridBackend&& rhs) noexcept
         : os_{ rhs.os_ }, vecCols_{ std::move(rhs.vecCols_) }, iRows_{ rhs.iRows_ },
         iCurrentCol_{ rhs.iCurrentCol_ }, iWidthScalePx_{ rhs.iWidthScalePx_ },
         bTableOpen_{ rhs.bTableOpen_ }, bRowOpen_{ rhs.bRowOpen_ } {
         rhs.bTableOpen_ = false;
         rhs.bRowOpen_ = false;
         rhs.bMovedFrom_ = true;
         rhs.iRows_ = 0;
         rhs.iCurrentCol_ = 0;
         rhs.vecCols_.clear();
      }

      ~HtmlOStreamGridBackend() {
         if (!bMovedFrom_) {
            reset(true);
         }
      }

      void set_width_scale_px(int const iWidthScalePx) {
         iWidthScalePx_ = (std::max)(1, iWidthScalePx);
      }

      [[nodiscard]] int width_scale_px() const noexcept {
         return iWidthScalePx_;
      }

      template <adecc::StreamPolicy CSP = SP>
      void set_caption(adecc::vecCaptions<CSP> const& caps, bool const bClear = true) {
         if (bClear) {
            reset(true);
         }

         vecCols_.clear();
         vecCols_.reserve(caps.size());

         for (auto const& t : caps) {
            vecCols_.push_back(SColumn{
               .strCaption = adecc::ConvertTo<string_ty>(std::get<0>(t)),
               .iWidth = static_cast<int>(std::get<1>(t)),
               .eAlignment = std::get<2>(t)
               });
         }

         iRows_ = 0;
         iCurrentCol_ = 0;
         bRowOpen_ = false;

         OpenTable_();
         WriteHeader_();
      }

      template <class... Args>
         requires (sizeof...(Args) >= 3)
      void set_caption(std::vector<std::tuple<Args...>> const& caps, bool const bClear = true) {
         if (bClear) {
            reset(true);
         }

         vecCols_.clear();
         vecCols_.reserve(caps.size());

         for (auto const& t : caps) {
            vecCols_.push_back(SColumn{
               .strCaption = adecc::ConvertTo<string_ty>(std::get<0>(t)),
               .iWidth = static_cast<int>(std::get<1>(t)),
               .eAlignment = TextGridFormatBase<SP>::ReadAlignment(std::get<2>(t))
               });
         }

         iRows_ = 0;
         iCurrentCol_ = 0;
         bRowOpen_ = false;

         OpenTable_();
         WriteHeader_();
      }

      void append_row() {
         CloseRow_();
         Print_("<tr>\n");
         bRowOpen_ = true;
         iCurrentCol_ = 0;
         ++iRows_;
      }

      template <class ty>
         requires adecc::is_in_type_list_v<ty, adecc::defined_param_types>
      void append_col(ty const& theValue) {
         WriteCell_(adecc::ConvertTo<string_ty>(theValue));
      }

      template <class opt_ty>
         requires (adecc::is_optional_v<opt_ty>&&
      adecc::is_in_type_list_v<typename opt_ty::value_type, adecc::defined_param_types>)
         void append_col(opt_ty const& theValue) {
         WriteCell_(theValue ? adecc::ConvertTo<string_ty>(*theValue) : string_ty{});
      }

      [[nodiscard]] size_type rows() const noexcept {
         return iRows_;
      }

      [[nodiscard]] size_type columns() const noexcept {
         return vecCols_.size();
      }

      void reset(bool const bFull = true) {
         bool const bHadTable = bTableOpen_;

         CloseTable_();

         if (bHadTable) {
            Print_("\n");
         }

         iRows_ = 0;
         iCurrentCol_ = 0;
         bRowOpen_ = false;

         if (bFull) {
            vecCols_.clear();
         }
         else if (!vecCols_.empty()) {
            OpenTable_();
            WriteHeader_();
         }
      }

      [[nodiscard]] bool freeze() {
         return false;
      }

      void unfreeze(bool const) {
      }

   private:
      struct SColumn {
         string_ty      strCaption{};
         int            iWidth{};
         EAlignmentType eAlignment{ EAlignmentType::left };
      };

      static std::string_view AlignToCss_(EAlignmentType const eAlignment) {
         switch (eAlignment) {
         case EAlignmentType::right:   return "right";
         case EAlignmentType::center:  return "center";
         case EAlignmentType::left: [[fallthrough]];
         case EAlignmentType::unknown: [[fallthrough]];
         default:                      return "left";
         }
      }

      static string_ty MakeEntity_(std::string_view const svEntity) {
         string_ty strResult;
         strResult.push_back(static_cast<char_ty>('&'));
         for (char const ch : svEntity) {
            strResult.push_back(static_cast<char_ty>(ch));
         }
         strResult.push_back(static_cast<char_ty>(';'));
         return strResult;
      }

      [[nodiscard]] int HtmlWidth_(int const iWidth) const noexcept {
         return (std::max)(0, iWidth) * iWidthScalePx_;
      }

      [[nodiscard]] string_ty MakeCellStyle_(EAlignmentType const eAlignment, int const iWidth) const {
         int const iHtmlWidth = HtmlWidth_(iWidth);

         if (iHtmlWidth > 0) {
            return adecc::ConvertTo<string_ty>(
               std::format("text-align:{}; width:{}px; min-width:{}px; white-space:nowrap; padding:2px 6px;",
                  AlignToCss_(eAlignment),
                  iHtmlWidth,
                  iHtmlWidth));
         }

         return adecc::ConvertTo<string_ty>(
            std::format("text-align:{}; white-space:nowrap; padding:2px 6px;",
               AlignToCss_(eAlignment)));
      }

      static string_ty EscapeHtml_(string_ty const& strValue) {
         string_ty strResult;
         strResult.reserve(strValue.size());

         for (char_ty const ch : strValue) {
            switch (ch) {
            case static_cast<char_ty>('&'):  strResult += MakeEntity_("amp"); break;
            case static_cast<char_ty>('<'):  strResult += MakeEntity_("lt"); break;
            case static_cast<char_ty>('>'):  strResult += MakeEntity_("gt"); break;
            case static_cast<char_ty>('\"'): strResult += MakeEntity_("quot"); break;
            case static_cast<char_ty>('\''): strResult += MakeEntity_("#39"); break;
            default:                         strResult.push_back(ch); break;
            }
         }

         return strResult;
      }

      void Print_(char const* szText) {
         Print_(adecc::ConvertTo<string_ty>(szText));
      }

      void Print_(string_ty const& strText) {
         if constexpr (std::same_as<char_ty, char>) {
            std::print(os_, "{}", strText);
         }
         else {
            os_ << strText;
         }
      }

      void OpenTable_() {
         if (!bTableOpen_) {
            Print_("<table>\n");
            bTableOpen_ = true;
         }
      }

      void CloseRow_() {
         if (bRowOpen_) {
            Print_("</tr>\n");
            bRowOpen_ = false;
         }
      }

      void CloseTable_() {
         CloseRow_();

         if (bTableOpen_) {
            Print_("</tbody>\n");
            Print_("</table>\n");
            bTableOpen_ = false;
         }
      }

      void WriteHeader_() {
         Print_("<thead>\n<tr>\n");

         for (auto const& theCol : vecCols_) {
            Print_("<th style=\"");
            Print_(MakeCellStyle_(theCol.eAlignment, theCol.iWidth));
            Print_("\">");
            Print_(EscapeHtml_(theCol.strCaption));
            Print_("</th>\n");
         }

         Print_("</tr>\n</thead>\n<tbody>\n");
      }

      void WriteCell_(string_ty const& strValue) {
         if (!bRowOpen_) {
            append_row();
         }

         EAlignmentType eAlignment = EAlignmentType::left;
         int iWidth = 0;

         if (iCurrentCol_ < vecCols_.size()) {
            eAlignment = vecCols_[iCurrentCol_].eAlignment;
            iWidth = vecCols_[iCurrentCol_].iWidth;
         }

         Print_("<td style=\"");
         Print_(MakeCellStyle_(eAlignment, iWidth));
         Print_("\">");
         Print_(EscapeHtml_(strValue));
         Print_("</td>\n");

         ++iCurrentCol_;
      }

   private:
      ostream_ty& os_;
      std::vector<SColumn> vecCols_{};
      size_type iRows_ = 0;
      size_type iCurrentCol_ = 0;
      int iWidthScalePx_ = 8;
      bool bTableOpen_ = false;
      bool bRowOpen_ = false;
      bool bMovedFrom_ = false;
   };

} // namespace adecc::grid
