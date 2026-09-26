// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file stream_wrappers.h
\brief Stream-buffer adapters connecting text and grid models with standard C++ streams.

\details
Transforms model-oriented text and grid targets into stream-compatible sinks and formats incoming character
streams into rows and cells. The adapters let framework-neutral models participate in existing
iostream-based code without making iostreams the domain model.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "StreamPolicy: Type Families as Architectural Building Blocks".
- "Text Wrapper and the Universal Wrapper Concept".
- "Output Iterators and Standard Algorithms".
- "From Text and Grid to a General Adapter Strategy".

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

#include "grid_wrapper.h"
#include "text_grid_wrapper.h"
#include "text_wrapper.h"

#include <algorithm>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <type_traits>
#include <tuple>
#include <vector>

namespace adecc::stream {

   /*!
    \brief   streambuf for TextModel and WriteTextModel
    \details The adapter collects characters until '\\n' and then writes a new
             text line. A preceding '\\r' is removed for CRLF input.
    \tparam  TextModelTy Text model satisfying text_sink_model_type
   */
   template <adecc::text::text_sink_model_type TextModelTy>
   class BasicTextModelStreamBuf
      : public std::basic_streambuf<
      typename TextModelTy::char_type,
      typename TextModelTy::stream_policy_type::traits_type> {
   public:
      using text_model_type = TextModelTy;
      using char_type = typename text_model_type::char_type;
      using traits_type = typename text_model_type::stream_policy_type::traits_type;
      using int_type = typename traits_type::int_type;
      using string_type = typename text_model_type::string_type;
      using string_view_type = typename text_model_type::string_view_type;

   public:
      explicit BasicTextModelStreamBuf(text_model_type& theText)
         : theText_{ theText } {
      }

      BasicTextModelStreamBuf(BasicTextModelStreamBuf const&) = delete;
      BasicTextModelStreamBuf& operator=(BasicTextModelStreamBuf const&) = delete;

      BasicTextModelStreamBuf(BasicTextModelStreamBuf&&) = delete;
      BasicTextModelStreamBuf& operator=(BasicTextModelStreamBuf&&) = delete;

      ~BasicTextModelStreamBuf() override {
         sync();
      }

   protected:
      int_type overflow(int_type const iCh) override {
         if (traits_type::eq_int_type(iCh, traits_type::eof())) {
            return traits_type::not_eof(iCh);
         }

         PutChar_(traits_type::to_char_type(iCh));
         return iCh;
      }

      std::streamsize xsputn(char_type const* pText, std::streamsize const iCount) override {
         for (std::streamsize i = 0; i < iCount; ++i) {
            PutChar_(pText[i]);
         }

         return iCount;
      }

      int sync() override {
         if (!strLine_.empty()) {
            AppendLine_();
         }

         return 0;
      }

   private:
      void PutChar_(char_type const ch) {
         if (ch == static_cast<char_type>('\n')) {
            if (!strLine_.empty() && strLine_.back() == static_cast<char_type>('\r')) {
               strLine_.pop_back();
            }

            AppendLine_();
            return;
         }

         strLine_.push_back(ch);
      }

      void AppendLine_() {
         theText_.appendLine(string_view_type{ strLine_ });
         strLine_.clear();
      }

   private:
      text_model_type& theText_;
      string_type strLine_{};
   };

   /*!
    \brief   ostream for TextModel and WriteTextModel
    \details Owns the streambuf and exposes TextModel as a regular stream.
    \tparam  TextModelTy Text model satisfying text_sink_model_type
   */
   template <adecc::text::text_sink_model_type TextModelTy>
   class BasicTextModelOStream
      : public std::basic_ostream<
      typename TextModelTy::char_type,
      typename TextModelTy::stream_policy_type::traits_type> {
   public:
      using streambuf_type = BasicTextModelStreamBuf<TextModelTy>;
      using char_type = typename TextModelTy::char_type;
      using traits_type = typename TextModelTy::stream_policy_type::traits_type;
      using ostream_type = std::basic_ostream<char_type, traits_type>;

   public:
      explicit BasicTextModelOStream(TextModelTy& theText)
         : ostream_type{ &theBuf_ },
         theBuf_{ theText } {
      }

      ~BasicTextModelOStream() override {
         this->flush();
      }

   private:
      streambuf_type theBuf_;
   };

   /*!
    \brief   streambuf for WriteGridModel
    \details '\\t' terminates the current cell; '\\n' terminates the current cell
             and logically starts the next row.
    \tparam  GridModelTy WriteGridModel
   */
   template <adecc::grid::write_grid_model_type GridModelTy>
   class BasicWriteGridModelStreamBuf
      : public std::basic_streambuf<
      typename GridModelTy::backend_ty::stream_policy_type::char_type,
      typename GridModelTy::backend_ty::stream_policy_type::traits_type> {
   public:
      using grid_model_type = GridModelTy;
      using stream_policy_type = typename grid_model_type::backend_ty::stream_policy_type;
      using char_type = typename stream_policy_type::char_type;
      using traits_type = typename stream_policy_type::traits_type;
      using int_type = typename traits_type::int_type;
      using string_type = typename stream_policy_type::string_type;

   public:
      explicit BasicWriteGridModelStreamBuf(grid_model_type& theGrid)
         : theGrid_{ theGrid } {
      }

      BasicWriteGridModelStreamBuf(BasicWriteGridModelStreamBuf const&) = delete;
      BasicWriteGridModelStreamBuf& operator=(BasicWriteGridModelStreamBuf const&) = delete;

      BasicWriteGridModelStreamBuf(BasicWriteGridModelStreamBuf&&) = delete;
      BasicWriteGridModelStreamBuf& operator=(BasicWriteGridModelStreamBuf&&) = delete;

      ~BasicWriteGridModelStreamBuf() override {
         sync();
      }

   protected:
      int_type overflow(int_type const iCh) override {
         if (traits_type::eq_int_type(iCh, traits_type::eof())) {
            return traits_type::not_eof(iCh);
         }

         PutChar_(traits_type::to_char_type(iCh));
         return iCh;
      }

      std::streamsize xsputn(char_type const* pText, std::streamsize const iCount) override {
         for (std::streamsize i = 0; i < iCount; ++i) {
            PutChar_(pText[i]);
         }

         return iCount;
      }

      int sync() override {
         if (bRowOpen_ || !strCell_.empty()) {
            FlushCell_();
         }

         return 0;
      }

   private:
      void PutChar_(char_type const ch) {
         if (ch == static_cast<char_type>('\t')) {
            EnsureRow_();
            FlushCell_();
            return;
         }

         if (ch == static_cast<char_type>('\n')) {
            EnsureRow_();

            if (!strCell_.empty() && strCell_.back() == static_cast<char_type>('\r')) {
               strCell_.pop_back();
            }

            FlushCell_();
            bRowOpen_ = false;
            return;
         }

         EnsureRow_();
         strCell_.push_back(ch);
      }

      void EnsureRow_() {
         if (!bRowOpen_) {
            theGrid_.appendRow();
            bRowOpen_ = true;
         }
      }

      void FlushCell_() {
         theGrid_.template appendCol<string_type>(strCell_);
         strCell_.clear();
      }

   private:
      grid_model_type& theGrid_;
      string_type strCell_{};
      bool bRowOpen_ = false;
   };

   /*!
    \brief   streambuf for direct text-based grid output
    \details The buffer formats incoming stream output as a text table and
             writes each complete row directly to a target streambuf.
             '\t' ends the current cell; '\n' ends the current row.
             Es gibt keine nachgelagerte Exportphase.
    \tparam  SP StreamPolicy, typically AnsiStreamPolicy or WideStreamPolicy
   */
   template <adecc::StreamPolicy SP = adecc::AnsiStreamPolicy>
   class TextGridStreamBuf
      : public std::basic_streambuf<typename SP::char_type, typename SP::traits_type>,
      private adecc::grid::TextGridFormatBase<SP> {
   public:
      using stream_policy_type = SP;
      using base_type = adecc::grid::TextGridFormatBase<stream_policy_type>;
      using grid_backend_type = base_type;
      using char_type = typename SP::char_type;
      using traits_type = typename SP::traits_type;
      using int_type = typename traits_type::int_type;
      using ostream_type = typename SP::ostream;
      using streambuf_type = std::basic_streambuf<char_type, traits_type>;
      using string_type = typename SP::string_type;
      using string_view_type = std::basic_string_view<char_type, traits_type>;
      using separators_type = typename base_type::STextSeparators;
      using size_type = typename base_type::size_type;

   public:
      /*!
       \brief   Creates a TextGrid stream buffer without a bound target
       \details AttachTarget() must be called before the first output.
                '\\t' is a control character used only for changing columns and is never copied
                into the rendered output row.
       \tparam  Args Types of the caption tuples
       \param   vecCaptions Captions containing at least name, width, and alignment
       \param   theSeparators Separators used for text rendering
       \param   bSuppressFirstCaptionRow Suppresses a first data row that
                exactly matches the captions.
      */
      template <class... Args>
      explicit TextGridStreamBuf(
         std::vector<std::tuple<Args...>> const& vecCaptions,
         separators_type const& theSeparators = base_type::TableSeparators(),
         bool const bSuppressFirstCaptionRow = true) {
         Initialize_(vecCaptions, theSeparators, bSuppressFirstCaptionRow);
      }

      /*!
       \brief   Creates a TextGrid stream buffer bound to a target stream
       \details The currently installed rdbuf() of the target stream is used as the output target
                as the output target. The same stream can then be redirected to this buffer
                with StreamRedirect.
       \tparam  Args Types of the caption tuples
       \param   os Target stream whose current rdbuf() receives output
       \param   vecCaptions Captions containing at least name, width, and alignment
       \param   theSeparators Separators used for text rendering
       \param   bSuppressFirstCaptionRow Suppresses a first data row that
                exactly matches the captions.
       \pre     os.rdbuf() != this
       \throw   std::logic_error if os already points to this buffer
      */
      template <class... Args>
      explicit TextGridStreamBuf(
         ostream_type& os,
         std::vector<std::tuple<Args...>> const& vecCaptions,
         separators_type const& theSeparators = base_type::TableSeparators(),
         bool const bSuppressFirstCaptionRow = true) {
         Initialize_(vecCaptions, theSeparators, bSuppressFirstCaptionRow);
         AttachTarget(os);
      }

      /*!
       \brief   Creates a TextGrid stream buffer bound to a target buffer
       \details The supplied streambuf is written directly.
       \tparam  Args Types of the caption tuples
       \param   theTargetBuf Target buffer for formatted table rows
       \param   vecCaptions Captions containing at least name, width, and alignment
       \param   theSeparators Separators used for text rendering
       \param   bSuppressFirstCaptionRow Suppresses a first data row that
                exactly matches the captions.
      */
      template <class... Args>
      explicit TextGridStreamBuf(
         streambuf_type& theTargetBuf,
         std::vector<std::tuple<Args...>> const& vecCaptions,
         separators_type const& theSeparators = base_type::TableSeparators(),
         bool const bSuppressFirstCaptionRow = true) {
         Initialize_(vecCaptions, theSeparators, bSuppressFirstCaptionRow);
         AttachTarget(theTargetBuf);
      }

      TextGridStreamBuf(TextGridStreamBuf const&) = delete;
      TextGridStreamBuf& operator=(TextGridStreamBuf const&) = delete;

      TextGridStreamBuf(TextGridStreamBuf&&) = delete;
      TextGridStreamBuf& operator=(TextGridStreamBuf&&) = delete;

      ~TextGridStreamBuf() override {
         try {
            Close();
         }
         catch (...) {
         }
      }

      /*!
       \brief   Binds a target stream
       \details The stream's current rdbuf() is stored. The method must
                be called before redirecting the same stream to this buffer.
       \param   os Target stream
       \pre     os.rdbuf() != this
       \throw   std::logic_error if os already points to this buffer
       \throw   std::invalid_argument if os does not provide an rdbuf.
      */
      void AttachTarget(ostream_type& os) {
         auto* const pBuf = os.rdbuf();

         if (pBuf == this) {
            throw std::logic_error{
               "TextGridStreamBuf::AttachTarget: stream already uses this buffer"
            };
         }

         AttachTarget(pBuf);
      }

      /*!
       \brief   Binds a target buffer
       \details Binding itself produces no output. Header and separator lines are emitted only
                when the first real character output is produced.
       \param   pTargetBuf Target buffer
       \pre     pTargetBuf != nullptr
       \throw   std::invalid_argument if pTargetBuf is null.
      */
      void AttachTarget(streambuf_type* const pTargetBuf) {
         if (pTargetBuf == nullptr) {
            throw std::invalid_argument{
               "TextGridStreamBuf::AttachTarget: pTargetBuf must not be null"
            };
         }

         pTargetBuf_ = pTargetBuf;
      }

      /*!
       \brief   Binds a target buffer
       \param   theTargetBuf Target buffer
      */
      void AttachTarget(streambuf_type& theTargetBuf) {
         AttachTarget(std::addressof(theTargetBuf));
      }

      /*!
       \brief   Unbinds the target buffer
       \details A partially written row is completed first. Afterwards
                further output is rejected until AttachTarget() is called again.
      */
      void DetachTarget() {
         Close();
         pTargetBuf_ = nullptr;
      }

      /*!
       \brief   Completes a partially written data row
       \details This is not called from sync(), so std::flush does not create cells
                cells. std::endl already terminates the row through its contained '\\n'.
      */
      void Close() {
         if (!strCell_.empty() || !vecCells_.empty()) {
            FlushCell_();
            WriteCurrentRow_();
         }

         if (pTargetBuf_ != nullptr) {
            pTargetBuf_->pubsync();
         }
      }

      /*!
       \brief   Resets the logical table state
       \details Clears only buffered input cells. Already written output
                cannot be retracted by a streambuf.
      */
      void Clear() {
         strCell_.clear();
         vecCells_.clear();
         bFirstRow_ = true;
         iRows_ = 0;
      }

      [[nodiscard]] bool HasTarget() const noexcept {
         return pTargetBuf_ != nullptr;
      }

      [[nodiscard]] size_type rows() const noexcept {
         return iRows_;
      }

      [[nodiscard]] size_type columns() const noexcept {
         return this->vecCols_.size();
      }

      void set_text_separators(separators_type const& theSeparators) {
         base_type::set_text_separators(theSeparators);
      }

      [[nodiscard]] separators_type const& text_separators() const noexcept {
         return base_type::text_separators();
      }

      static separators_type PlainSeparators() {
         return base_type::PlainSeparators();
      }

      static separators_type TableSeparators() {
         return base_type::TableSeparators();
      }

      static separators_type CompactTableSeparators() {
         return base_type::CompactTableSeparators();
      }

   protected:
      int_type overflow(int_type const iCh) override {
         if (traits_type::eq_int_type(iCh, traits_type::eof())) {
            return traits_type::not_eof(iCh);
         }

         PutChar_(traits_type::to_char_type(iCh));
         return iCh;
      }

      std::streamsize xsputn(char_type const* pText, std::streamsize const iCount) override {
         for (std::streamsize i = 0; i < iCount; ++i) {
            PutChar_(pText[i]);
         }

         return iCount;
      }

      int sync() override {
         if (pTargetBuf_ != nullptr) {
            return pTargetBuf_->pubsync();
         }

         return 0;
      }

   private:
      template <class... Args>
      void Initialize_(std::vector<std::tuple<Args...>> const& vecCaptions,
         separators_type const& theSeparators,
         bool const bSuppressFirstCaptionRow) {
         static_assert(sizeof...(Args) >= 3,
            "TextGridStreamBuf captions require at least caption, width and alignment");

         this->set_text_separators(theSeparators);
         bSuppressFirstCaptionRow_ = bSuppressFirstCaptionRow;
         this->vecCols_.clear();
         this->vecCols_.reserve(vecCaptions.size());

         for (auto const& theCaption : vecCaptions) {
            this->vecCols_.push_back(typename base_type::SColumn{
               .strCaption = SanitizeOutputText_(adecc::ConvertTo<string_type>(std::get<0>(theCaption))),
               .iWidth = static_cast<int>(std::get<1>(theCaption)),
               .eAlignment = base_type::ReadAlignment(std::get<2>(theCaption))
               });
         }
      }

      void PutChar_(char_type const ch) {
         if (pTargetBuf_ == nullptr) {
            throw std::logic_error{
               "TextGridStreamBuf: no target streambuf attached"
            };
         }

         EnsureHeader_();

         if (traits_type::eq(ch, static_cast<char_type>('\t'))) {
            FlushCell_();
            return;
         }

         if (traits_type::eq(ch, static_cast<char_type>('\n'))) {
            if (!strCell_.empty() && traits_type::eq(strCell_.back(), static_cast<char_type>('\r'))) {
               strCell_.pop_back();
            }

            FlushCell_();
            WriteCurrentRow_();
            return;
         }

         strCell_.push_back(ch);
      }

      void FlushCell_() {
         vecCells_.push_back(SanitizeOutputText_(strCell_));
         strCell_.clear();
      }

      void EnsureHeader_() {
         if (!bHeaderWritten_ && pTargetBuf_ != nullptr) {
            WriteHeader_();
            bHeaderWritten_ = true;
         }
      }

      void WriteHeader_() {
         if (this->vecCols_.empty()) {
            return;
         }

         string_type strLine;

         AppendTo_(strLine, this->text_separators().strRowBegin);

         for (size_type i = 0; i < this->vecCols_.size(); ++i) {
            if (i > 0) {
               AppendTo_(strLine, this->text_separators().strColSep);
            }

            auto const& theCol = this->vecCols_[i];
            AppendTo_(strLine,
               base_type::FormatCell(theCol.strCaption, theCol.iWidth, theCol.eAlignment));
         }

         AppendTo_(strLine, this->text_separators().strRowEnd);
         WriteLine_(strLine);

         strLine.clear();
         AppendTo_(strLine, this->text_separators().strRuleBegin);

         for (size_type i = 0; i < this->vecCols_.size(); ++i) {
            if (i > 0) {
               AppendTo_(strLine, this->text_separators().strRuleSep);
            }

            int const iWidth = (std::max)(1, this->vecCols_[i].iWidth);
            AppendTo_(strLine,
               string_type(static_cast<std::size_t>(iWidth), static_cast<char_type>('-')));
         }

         AppendTo_(strLine, this->text_separators().strRuleEnd);
         WriteLine_(strLine);
      }

      [[nodiscard]] bool IsCaptionRow_() const {
         if (vecCells_.size() != this->vecCols_.size()) {
            return false;
         }

         for (size_type i = 0; i < vecCells_.size(); ++i) {
            if (vecCells_[i] != this->vecCols_[i].strCaption) {
               return false;
            }
         }

         return true;
      }

      void WriteCurrentRow_() {
         if (bFirstRow_ && bSuppressFirstCaptionRow_ && IsCaptionRow_()) {
            vecCells_.clear();
            bFirstRow_ = false;
            return;
         }

         bFirstRow_ = false;

         string_type strLine;

         AppendTo_(strLine, this->text_separators().strRowBegin);

         for (size_type i = 0; i < vecCells_.size(); ++i) {
            if (i > 0) {
               AppendTo_(strLine, this->text_separators().strColSep);
            }

            if (i < this->vecCols_.size()) {
               auto const& theCol = this->vecCols_[i];
               AppendTo_(strLine,
                  base_type::FormatCell(vecCells_[i], theCol.iWidth, theCol.eAlignment));
            }
            else {
               AppendTo_(strLine, vecCells_[i]);
            }
         }

         AppendTo_(strLine, this->text_separators().strRowEnd);
         WriteLine_(strLine);

         vecCells_.clear();
         ++iRows_;
      }

      [[nodiscard]] static string_type SanitizeOutputText_(string_type const& strValue) {
         string_type strResult;
         strResult.reserve(strValue.size());

         for (char_type const ch : strValue) {
            if (traits_type::eq(ch, static_cast<char_type>('\t'))) {
               strResult.push_back(static_cast<char_type>(' '));
               continue;
            }

            if (traits_type::eq(ch, static_cast<char_type>('\n')) ||
               traits_type::eq(ch, static_cast<char_type>('\r'))) {
               continue;
            }

            strResult.push_back(ch);
         }

         return strResult;
      }

      static void AppendTo_(string_type& strTarget, string_type const& strValue) {
         strTarget += strValue;
      }

      void WriteLine_(string_type const& strLine) {
         if (pTargetBuf_ == nullptr) {
            return;
         }

         pTargetBuf_->sputn(strLine.data(), static_cast<std::streamsize>(strLine.size()));
         pTargetBuf_->sputc(SP::cNL);
      }

   private:
      streambuf_type* pTargetBuf_ = nullptr;
      std::vector<string_type> vecCells_{};
      string_type strCell_{};
      size_type iRows_ = 0;
      bool bHeaderWritten_ = false;
      bool bFirstRow_ = true;
      bool bSuppressFirstCaptionRow_ = true;
   };



   /*!
    \brief   ostream for WriteGridModel
    \details Allows a sequential grid model to be used as a stream.
    \tparam  GridModelTy WriteGridModel
   */
   template <adecc::grid::write_grid_model_type GridModelTy>
   class BasicWriteGridModelOStream
      : public std::basic_ostream<
      typename GridModelTy::backend_ty::stream_policy_type::char_type,
      typename GridModelTy::backend_ty::stream_policy_type::traits_type> {
   public:
      using streambuf_type = BasicWriteGridModelStreamBuf<GridModelTy>;
      using char_type = typename GridModelTy::backend_ty::stream_policy_type::char_type;
      using traits_type = typename GridModelTy::backend_ty::stream_policy_type::traits_type;
      using ostream_type = std::basic_ostream<char_type, traits_type>;

   public:
      explicit BasicWriteGridModelOStream(GridModelTy& theGrid)
         : ostream_type{ &theBuf_ },
         theBuf_{ theGrid } {
      }

      ~BasicWriteGridModelOStream() override {
         this->flush();
      }

   private:
      streambuf_type theBuf_;
   };

   /*!
    \brief   streambuf for GridModel
    \details Writes row by row into a random-access grid. New rows are created with
             insertRow() creates rows, and set(row, col, value) writes cells.
    \tparam  GridModelTy GridModel
   */
   template <adecc::grid::grid_model_type GridModelTy>
   class BasicGridModelStreamBuf
      : public std::basic_streambuf<char, std::char_traits<char>> {
   public:
      using grid_model_type = GridModelTy;
      using char_type = char;
      using traits_type = std::char_traits<char>;
      using int_type = typename traits_type::int_type;
      using string_type = std::string;
      using size_type = typename grid_model_type::size_type;

   public:
      explicit BasicGridModelStreamBuf(grid_model_type& theGrid)
         : theGrid_{ theGrid } {
      }

      BasicGridModelStreamBuf(BasicGridModelStreamBuf const&) = delete;
      BasicGridModelStreamBuf& operator=(BasicGridModelStreamBuf const&) = delete;

      BasicGridModelStreamBuf(BasicGridModelStreamBuf&&) = delete;
      BasicGridModelStreamBuf& operator=(BasicGridModelStreamBuf&&) = delete;

      ~BasicGridModelStreamBuf() override {
         sync();
      }

   protected:
      int_type overflow(int_type const iCh) override {
         if (traits_type::eq_int_type(iCh, traits_type::eof())) {
            return traits_type::not_eof(iCh);
         }

         PutChar_(traits_type::to_char_type(iCh));
         return iCh;
      }

      std::streamsize xsputn(char_type const* pText, std::streamsize const iCount) override {
         for (std::streamsize i = 0; i < iCount; ++i) {
            PutChar_(pText[i]);
         }

         return iCount;
      }

      int sync() override {
         if (bRowOpen_ || !strCell_.empty()) {
            FlushCell_();
         }

         return 0;
      }

   private:
      void PutChar_(char_type const ch) {
         if (ch == '\t') {
            EnsureRow_();
            FlushCell_();
            return;
         }

         if (ch == '\n') {
            EnsureRow_();

            if (!strCell_.empty() && strCell_.back() == '\r') {
               strCell_.pop_back();
            }

            FlushCell_();
            bRowOpen_ = false;
            iCurrentCol_ = 0;
            return;
         }

         EnsureRow_();
         strCell_.push_back(ch);
      }

      void EnsureRow_() {
         if (!bRowOpen_) {
            iCurrentRow_ = theGrid_.insertRow();
            iCurrentCol_ = 0;
            bRowOpen_ = true;
         }
      }

      void FlushCell_() {
         theGrid_.template set<string_type>(iCurrentRow_, iCurrentCol_, strCell_);
         strCell_.clear();
         ++iCurrentCol_;
      }

   private:
      grid_model_type& theGrid_;
      string_type strCell_{};
      size_type iCurrentRow_ = 0;
      size_type iCurrentCol_ = 0;
      bool bRowOpen_ = false;
   };

   /*!
    \brief   ostream for GridModel
    \details Macht ein Random-Access-Grid als std::ostream nutzbar.
    \tparam  GridModelTy GridModel
   */
   template <adecc::grid::grid_model_type GridModelTy>
   class BasicGridModelOStream : public std::ostream {
   public:
      using streambuf_type = BasicGridModelStreamBuf<GridModelTy>;

   public:
      explicit BasicGridModelOStream(GridModelTy& theGrid)
         : std::ostream{ &theBuf_ },
         theBuf_{ theGrid } {
      }

      ~BasicGridModelOStream() override {
         this->flush();
      }

   private:
      streambuf_type theBuf_;
   };

   /*!
    \brief   Factory for a TextModel stream
    \param   theText Target model
    \returns ostream Wrapper
   */
   template <adecc::text::text_sink_model_type TextModelTy>
   [[nodiscard]] BasicTextModelOStream<TextModelTy> MakeOStream(TextModelTy& theText) {
      return BasicTextModelOStream<TextModelTy>{theText};
   }

   /*!
    \brief   Factory for a WriteGridModel stream
    \param   theGrid Target model
    \returns ostream Wrapper
   */
   template <adecc::grid::write_grid_model_type GridModelTy>
   [[nodiscard]] BasicWriteGridModelOStream<GridModelTy> MakeOStream(GridModelTy& theGrid) {
      return BasicWriteGridModelOStream<GridModelTy>{theGrid};
   }

   /*!
    \brief   Factory for a GridModel stream
    \param   theGrid Target model
    \returns ostream Wrapper
   */
   template <adecc::grid::grid_model_type GridModelTy>
   [[nodiscard]] BasicGridModelOStream<GridModelTy> MakeOStream(GridModelTy& theGrid) {
      return BasicGridModelOStream<GridModelTy>{theGrid};
   }

} // namespace adecc::stream