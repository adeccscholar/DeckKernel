// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file file_line_sink.h
\brief Buffered line-oriented file sink compatible with standard output-iterator algorithms.

\details
Provides an output iterator and append interface that write line payloads to a file while buffering physical
writes. This turns a file into a sink that can participate in generic range and algorithm pipelines.

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
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <concepts>

namespace adecc {

template <std::size_t FlushBytes = (1u<<20)> // 1 MiB
class line_file_sink {
public:
   explicit line_file_sink(std::filesystem::path const& path) {
      // A policy check for an allowed path could be added here.
      ofs_.open(path, std::ios::binary | std::ios::trunc);
      if(!ofs_.is_open()) {
         throw std::system_error(std::make_error_code(std::errc::no_such_file_or_directory),
                                 "line_file_sink: cannot open " + path.string());
         }
      buf_.reserve(FlushBytes);
      }

   ~line_file_sink() {
      try { flush_(); } catch(...) { /* Destructors must not throw. */ }
      }

   line_file_sink(line_file_sink const&)            = delete;
   line_file_sink& operator=(line_file_sink const&) = delete;
   line_file_sink(line_file_sink&&)                 = default;
   line_file_sink& operator=(line_file_sink&&)      = default;

   class iterator {
   public:
      using iterator_concept = std::output_iterator_tag;
      using difference_type  = std::ptrdiff_t;

      iterator() = default;                         // Required by weakly_incrementable.
      explicit iterator(line_file_sink* s) noexcept : s_(s) {}

      iterator& operator*()  noexcept { return *this; }
      iterator& operator++() noexcept {                // Prefix a newline before the next element.
         if (s_) s_->need_prefix_nl_ = true;
         return *this;
         }
      iterator operator++(int) noexcept { iterator tmp = *this; ++(*this); return tmp; }

      template <typename ty>
      requires std::convertible_to<ty, std::string_view>
      iterator& operator=(ty&& elem) {
         std::string_view sv = static_cast<std::string_view>(std::forward<ty>(elem));
         s_->write_payload_(sv);
         return *this;
         }

       
   private:
      line_file_sink* s_ = nullptr;
   };

   iterator out() noexcept { return iterator{this}; }

   template <typename ty> requires std::convertible_to<ty, std::string_view>
   line_file_sink& operator += (ty&& value) {
      write_payload_(static_cast<std::string_view>(value));
      need_prefix_nl_ = true;
      return *this;
      }
   
private:
   void write_payload_(std::string_view sv) {
      // Optional prefix newline between lines, but never before the first line
      if (need_prefix_nl_) {
         buf_.push_back('\n');
         need_prefix_nl_ = false;
         }
      buf_.append(sv.data(), sv.size());
      if (buf_.size() >= FlushBytes) flush_checked_();
      }

   void flush_checked_() {
      ofs_.write(buf_.data(), static_cast<std::streamsize>(buf_.size()));
      if(!ofs_) {
         throw std::system_error(std::make_error_code(std::errc::io_error),
                                 "line_file_sink: write failed");
         }
      buf_.clear();
      }

   void flush_() {
      if(buf_.empty()) return;
      ofs_.write(buf_.data(), static_cast<std::streamsize>(buf_.size()));
      buf_.clear();
      }

private:
   std::ofstream ofs_;
   std::string   buf_;
   bool          need_prefix_nl_ = false; // Set by ++ and consumed by assignment.
};


} // end of namespace adcc
