// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file file_range.h
\brief Range view exposing text files as line-oriented string_view sources.

\details
Supports whole-file and streaming modes with optional CR stripping. In whole-file mode views remain stable
because the file is retained in one buffer; in streaming mode the view follows the current line buffer. The
abstraction models files as standard-compatible input ranges.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Files as Typed Data Flows".
- "Files as Ranges: Resources, Tuples, and RAII".
- "Source and Sink: Data Flows in Type Space".
- "Ranges, Lazy Processing, and Materialization".

\see ARCHITECTURE.md#files-as-typed-data-flows

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

#include "piggyback_exceptions.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <ranges>
#include <iterator>
#include <system_error>
#include <optional>
#include <charconv>
#include <format>

namespace adecc {

   namespace fs = std::filesystem;

   /*!
   \brief Line range over a file with compile-time switches
   \details
   ReadWhole == true reads the complete file into the internal buffer, so all string_view values remain stable.
   ReadWhole == false streams through std::ifstream; only the most recently read string_view remains valid.
   StripCR optionally removes a trailing '\r' from each line.
   */
   template <bool ReadWhole = true, bool StripCR = true>
   class FileLineRangeView : public std::ranges::view_interface<FileLineRangeView<ReadWhole, StripCR>> {
      [[noreturn]] void throw_open_error(ExceptionInformation&& info, std::error_code ec, fs::path const& p, std::string_view svMessage) {
         throw StandardError<std::system_error>{ std::forward<ExceptionInformation>(info),
            in_place_exception, ec,
            std::format("{}: {}", svMessage, p.string()) };
      }

      [[noreturn]] void throw_open_error(ExceptionInformation&& info, fs::path const& p, std::string_view svMessage) {
         std::error_code ec;
         auto const theStatus = fs::status(p, ec);

         std::error_code err = ec 
            ? ec 
            : fs::exists(theStatus)
            ? std::make_error_code(std::errc::permission_denied)
            : std::make_error_code(std::errc::no_such_file_or_directory);

         throw StandardError<std::system_error>{ std::forward<ExceptionInformation>(info),
                       in_place_exception, err, std::format("{}: {}", svMessage, p.string()) };
      }

   public:
      class iterator {
      public:
         using iterator_concept = std::input_iterator_tag;
         using iterator_category = std::input_iterator_tag;
         using value_type = std::string_view;
         using difference_type = std::ptrdiff_t;
         using reference = value_type;

         iterator() = default;

         explicit iterator(std::string const* pBuffer) : pBuffer_{ pBuffer } {
            if constexpr (ReadWhole) {
               if (pBuffer_ != nullptr) {
                  readNextWhole_();
               }
            }
         }

         explicit iterator(std::ifstream* pIfs, std::string* pLineBuf) : pIfs_{ pIfs }, pLineBuf_{ pLineBuf } {
            if constexpr (!ReadWhole) {
               if (pIfs_ != nullptr && pLineBuf_ != nullptr && *pIfs_) {
                  readNextStream_();
               }
            }
         }

         reference operator*() const noexcept {
            return current_;
         }

         value_type const* operator->() const noexcept {
            return &current_;
         }

         iterator& operator++() {
            if constexpr (ReadWhole) {
               readNextWhole_();
            }
            else {
               readNextStream_();
            }

            return *this;
         }

         void operator++(int) {
            ++(*this);
            }

         friend bool operator==(iterator const& it, std::default_sentinel_t) noexcept {
            return it.atEnd_;
            }

         friend bool operator==(std::default_sentinel_t, iterator const& it) noexcept {
            return it.atEnd_;
            }

         friend bool operator!=(iterator const& it, std::default_sentinel_t s) noexcept {
            return !(it == s);
            }

         friend bool operator!=(std::default_sentinel_t s, iterator const& it) noexcept {
            return !(s == it);
            }
         

      private:
         void readNextWhole_() {
            auto const& strBuffer = *pBuffer_;

            if (pos_ >= strBuffer.size()) {
               atEnd_ = true;
               current_ = {};
               return;
            }

            auto const uNl = strBuffer.find('\n', pos_);
            std::size_t uLineBegin = pos_;
            std::size_t uLineEnd = (uNl == std::string::npos) ? strBuffer.size() : uNl;

            if constexpr (StripCR) {
               if (uLineEnd > uLineBegin && strBuffer[uLineEnd - 1] == '\r') {
                  --uLineEnd;
               }
            }

            current_ = std::string_view{ strBuffer.data() + uLineBegin, uLineEnd - uLineBegin };
            pos_ = (uNl == std::string::npos) ? strBuffer.size() : uNl + 1;
            atEnd_ = false;
         }

         void readNextStream_() {
            if (pIfs_ == nullptr || pLineBuf_ == nullptr || !std::getline(*pIfs_, *pLineBuf_)) {
               atEnd_ = true;
               current_ = {};
               return;
            }

            if constexpr (StripCR) {
               if (!pLineBuf_->empty() && pLineBuf_->back() == '\r') {
                  pLineBuf_->pop_back();
               }
            }

            current_ = std::string_view{ *pLineBuf_ };
            atEnd_ = false;
         }

      private:
         std::string const* pBuffer_ = nullptr;
         std::size_t pos_ = 0;

         std::ifstream* pIfs_ = nullptr;
         std::string* pLineBuf_ = nullptr;

         std::string_view current_{};
         bool atEnd_ = true;
      };

   public:
      explicit FileLineRangeView(fs::path const& p) {
         if constexpr (ReadWhole) {
            std::error_code ec;
            auto const uSize = fs::file_size(p, ec);

            if (ec) {
               throw_open_error(ExceptionInformation{}, ec, p, "file_size failed");
            }

            std::ifstream ifs{ p, std::ios::in | std::ios::binary };

            if (!ifs.is_open()) {
               throw_open_error(ExceptionInformation{}, p, "can't open file");
            }

            buffer_.resize(static_cast<std::size_t>(uSize));

            if (uSize > 0) {
               ifs.read(buffer_.data(), static_cast<std::streamsize>(uSize));

               if (!ifs) {
                  throw_open_error(ExceptionInformation{}, std::make_error_code(std::errc::io_error), p, "failed to read file");
               }
            }
         }
         else {
            ifs_.open(p, std::ios::in);

            if (!ifs_.is_open()) {
               throw_open_error(ExceptionInformation{}, p, "can't open file");
            }
         }
      }

      FileLineRangeView(FileLineRangeView const&) = delete;
      FileLineRangeView& operator=(FileLineRangeView const&) = delete;

      FileLineRangeView(FileLineRangeView&&) noexcept = default;
      FileLineRangeView& operator=(FileLineRangeView&&) noexcept = default;

      iterator begin() {
         if constexpr (ReadWhole) {
            return iterator{ &buffer_ };
            }
         else {
            return iterator{ ifs_.is_open() ? &ifs_ : nullptr, &lineBuf_ };
            }
         }

      std::default_sentinel_t end() noexcept {
         return {};
         }

      std::default_sentinel_t end() const noexcept {
         return {};
         }

      bool ready() const noexcept {
         if constexpr (ReadWhole) {
            return true;
         }
         else {
            return ifs_.is_open();
         }
      }

      std::string_view buffer() const noexcept requires(ReadWhole) {
         return buffer_;
      }

      std::string GetBuffer() const requires(ReadWhole) {
         return buffer_;
      }

   private:
      std::string buffer_{};
      std::ifstream ifs_{};
      std::string lineBuf_{};
   };

} // namespace adecc

namespace std::ranges {

   template <bool ReadWhole, bool StripCR>
   inline constexpr bool enable_borrowed_range<adecc::FileLineRangeView<ReadWhole, StripCR>> = false;

}