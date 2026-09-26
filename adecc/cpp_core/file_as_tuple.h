// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file file_as_tuple.h
\brief Typed file-to-tuple and tuple-to-file adapters for delimited data flows.

\details
Combines line ranges, ConvertTo, generators, and type lists to read delimited files as typed tuples and to
write tuple-like values back through a line sink. Files are treated as typed sources and sinks rather than
as an isolated I/O model.

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

#include "convert_core.h"
#include "type_lists.h" 

#include "file_range.h"
#include "file_line_sink.h"
#include "generator.h" // workaround für C++Builder

#include <tuple>
#include <string>
#include <string_view>
#include <sstream>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <filesystem>

namespace adecc {

inline std::tuple<std::string_view, std::string_view> SplitOnce(std::string_view svRow, std::string_view svDelim) {
   if (svDelim.empty()) [[unlikely]] {
      throw std::runtime_error("delimiter must not be empty");
      }
   else {
      if (auto const uPos = svRow.find(svDelim); uPos != std::string_view::npos) [[likely]] {
         return { svRow.substr(0, uPos), svRow.substr(uPos + svDelim.size()) };
         }
      else {
         return { svRow, std::string_view{} };
         }
      }      
   }

template <class ty>
ty ParseField(std::string_view svField) {
   if constexpr (is_optional_v<ty>) {
      if (svField.empty()) return std::nullopt;
      else {
         using used_type = optional_value_t<ty>;
         used_type retval = ConvertTo<used_type>(svField);
         return std::make_optional<used_type>( retval );
         }
      } 
   else {
      if (svField.empty()) [[unlikely]]
         throw std::runtime_error("empty field is not allowed for non-optional type");
      else 
         return ConvertTo<ty>(svField);
      }
   }



   template <std::size_t uIndex, class Tup>
   void AssignIndex(Tup& theOut, std::string_view& svRest, std::string_view svDelim) {
      constexpr std::size_t uCount = std::tuple_size_v<Tup>;

      auto [svField, svNext] = SplitOnce(svRest, svDelim);

      if constexpr (uIndex + 1U < uCount) {
         if (svNext.empty())
            throw std::runtime_error("too few fields");
      }

      using elem_ty = std::tuple_element_t<uIndex, Tup>;
      std::get<uIndex>(theOut) = ParseField<elem_ty>(svField);

      svRest = svNext;
   }

   template <class Tup, std::size_t... uIs>
   Tup TransformTupleImpl(std::string_view svRow, std::string_view svDelim, std::index_sequence<uIs...>) {
      static_assert(std::tuple_size_v<Tup> > 0, "TransformTuple requires at least one element");

      Tup theOut{};
      std::string_view svRest = svRow;

      (AssignIndex<uIs, Tup>(theOut, svRest, svDelim), ...);

      if (!svRest.empty())
         throw std::runtime_error("too many fields");

      return theOut;
   }

   template <class Tup>
   Tup TransformTuple(std::string_view svRow, std::string_view svDelim) {
      return TransformTupleImpl<Tup>(
         svRow,
         svDelim,
         std::make_index_sequence<std::tuple_size_v<Tup>>{}
      );
   }

   // API design: Args... appear only here; subsequent code uses Tup plus indices
   template <class... Args>
   std::tuple<Args...> Transform(std::string_view svRow, std::string_view svDelimiter) {
      using tup_ty = std::tuple<Args...>;
      return TransformTuple<tup_ty>(svRow, svDelimiter);
   }


   // File -> generator<Tup> through FileLineRangeView (string_view).
   template <bool ReadWhole, bool StripCR, class... Args>
   Generator<std::tuple<Args...>> FromCsvFile(std::filesystem::path const& thePath,
                                              std::string_view svDelimiter = ";",
                                              bool const boSkipFirst = false) {
   using tup_ty = std::tuple<Args...>;

   FileLineRangeView<ReadWhole, StripCR> theLines(thePath);
   std::size_t uLine = boSkipFirst ? 1 : 0; // Used for diagnostics and the initial drop.

   auto rngLines = std::move(theLines) | std::views::drop(uLine);

   for (std::string_view svLine : rngLines) {
      ++uLine;

      try {
         co_yield TransformTuple<tup_ty>(svLine, svDelimiter);
         }
      catch (std::exception const& e) {
         throw std::runtime_error(std::format("line {}: {}", uLine, e.what()));
         }
      }
   }
   
   template <bool ReadWhole, bool StripCR, adecc::defined_type_list_ty types_list>
   [[nodiscard]] auto FromCsvFileTyped(std::filesystem::path const& theFile,
                                       std::string_view svDelimiter = ";",
                                       bool const boSkipFirst = false) {
      return types_list::invoke([&]<class... Ts>() {
         return adecc::FromCsvFile<ReadWhole, StripCR, Ts...>(theFile, svDelimiter, boSkipFirst);
         });
      }   


struct Delimiter {
   std::string svStart;
   std::string svDelimiter;
   std::string svEnd;
   };
   
   
template <typename ty>
struct Quote {
   static std::string inline  strQuote = "";
   };
   
template <>
struct Quote<std::string> {
   static std::string inline strQuote = "\"";
   };


   
class tuple_as_file {
private:
   std::filesystem::path thePath;
   Delimiter             theDelimiter;
   line_file_sink<true>  sink;
public:
   tuple_as_file(std::filesystem::path const& path, Delimiter del = { "", ";", "" }) noexcept
                 : thePath(path), sink(thePath), theDelimiter(del) {
      }

   tuple_as_file(std::filesystem::path&& path, Delimiter del = { "", ";", "" }) noexcept
                 : thePath(std::move(path)), sink(thePath), theDelimiter(del) {
      }


   // Defaulted special member functions
   tuple_as_file(tuple_as_file const&)            = delete;
   tuple_as_file(tuple_as_file&&) noexcept        = default;
   tuple_as_file& operator=(tuple_as_file const&) = delete;
   tuple_as_file& operator=(tuple_as_file&&) noexcept = default;

   ~tuple_as_file() = default;

   template <tuple_like Tup> // zusätzlich prüft, ob das tuple_like konvertierbar zu unser Datei
   tuple_as_file& operator+=(Tup const& theTuple) {
      std::ostringstream os;
      bool bFirst = true;
      os << theDelimiter.svStart;
      std::apply([&](auto const&... theXs) {
         ((WriteField(os, bFirst, theXs)), ...);
         }, theTuple);
      os << theDelimiter.svEnd;
      sink += os.str();
      
      return *this;
   }

private:
    template <typename ty>
    std::string ValueTo(ty&& value) {
      if constexpr (is_optional_v<remove_cvref_t<ty>>) {
         if (!value.has_value()) { return ""; }
         else return ValueTo(*std::forward<ty>(value)); 
         }
      else return ConvertTo<std::string>(value); 
      }

   template <class ty>
   void WriteField(std::ostream& theOut, bool& bFirst, ty const& theValue) {
      if (!bFirst) {
         theOut.write(theDelimiter.svDelimiter.data(), static_cast<std::streamsize>(theDelimiter.svDelimiter.size()));
         } 
      else {
         bFirst = false;
         }

      std::string const strField = ValueTo(theValue);
      theOut.write(strField.data(), static_cast<std::streamsize>(strField.size()));
   }
   
};   
   
} // namespace adecc