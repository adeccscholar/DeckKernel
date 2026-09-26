// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file piggyback_exceptions.h
\brief Exception enrichment infrastructure with source location, timestamps, and diagnostic history.

\details
Adds contextual history to standard exception types without replacing their normal C++ semantics. Diagnostic
entries can accumulate as an exception crosses abstraction boundaries, preserving where and when additional
information was attached.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Exceptions in the Domain Core: Standard C++ with a Backpack".
- "What Conversion and Error Handling Achieve Together".
- "Tests as Architectural Proof".

\see ARCHITECTURE.md#exceptions-as-diagnostic-carriers

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

/*
TODO:
Missing timezone support, using Compiletime directive CHRONO_MISSED_ZONEDB
Time currently assumes Central European Time; replace this when timezone support is available
European time formatting is currently the default and can be made configurable  
*/

#include "value_types.h"
#include "circular_buffer.h"

#include <string>
#include <string_view>
#include <exception>
#include <stdexcept>
#include <tuple>
#include <chrono>
#include <functional>
#include <filesystem>
#include <source_location>
#include <format>
#include <print>
#include <concepts>

namespace fs = std::filesystem;
using namespace std::string_literals;

namespace adecc {

struct HistoryEntry  {
   std::string  message;
   src_loc      location;
   timestamp_ty timepoint;
   };


class ExceptionInformation {
private:
   using data_ty = std::tuple<std::string, timestamp_ty, src_loc>;
   mutable data_ty data;
   mutable CircularBuffer<HistoryEntry , 10> history {};
public:
   ExceptionInformation(std::string const& msg = "Exception throw at",
                        src_loc const& loc = src_loc::current(), 
                        timestamp_ty const& timept = std::chrono::system_clock::now())
		 : data { msg, timept, loc} {}

   ExceptionInformation(ExceptionInformation const&)            = default;
   ExceptionInformation(ExceptionInformation&&) noexcept        = default;
   ExceptionInformation& operator=(ExceptionInformation const&) = default;
   ExceptionInformation& operator=(ExceptionInformation&&) noexcept = default;
   virtual ~ExceptionInformation()                              = default;

   [[nodiscard]] std::string  const& message(void) const { return std::get<0>(data); }
   [[nodiscard]] timestamp_ty const& timepoint(void) const { return std::get<1>(data); }
   [[nodiscard]] src_loc      const& location(void) const { return std::get<2>(data); };

   [[nodiscard]] std::string TimePosition() const { return TimePosition(message(), timepoint(), location()); }
   [[nodiscard]] std::string Time()         const { return Time(timepoint()); }
   [[nodiscard]] std::string Position()     const { return Position(location()); }
   [[nodiscard]] std::string FilePosition() const { return FilePosition(location()); }

   
   /// \brief Appends a history entry
   ExceptionInformation const& renew(std::string const& msg = "",
                               src_loc const& loc = src_loc::current(),
                               timestamp_ty const& tp = std::chrono::system_clock::now()) const {
      history.push(HistoryEntry { std::get<0>(data), std::get<2>(data), std::get<1>(data) });
   
      std::get<0>(data) = msg;
      std::get<1>(data) = tp;
      std::get<2>(data) = loc; 
      rebuild();
      return *this;
      }

   /// \brief Appends a history entry
   ExceptionInformation const& trace(std::string_view svMsg, src_loc const& loc = src_loc::current(),
              timestamp_ty const& tp = std::chrono::system_clock::now()) const {
      history.push(HistoryEntry{ std::string{ svMsg }, loc, tp });
      rebuild();
      return *this;
      }

   ExceptionInformation const& trace(ExceptionInformation const& info) const {
      history.push(HistoryEntry{ info.message(), info.location(), info.timepoint() });
      rebuild();
      return *this;
      }
      
   [[nodiscard]] std::string Message() const {
      return TimePosition(message(), timepoint(), location()); 
      }
      
   /// \brief true if the history is not empty
   [[nodiscard]] bool hasHistory() const noexcept {
      return !history.view().empty();
      }

   /// \brief Format of a single-line history entry
   [[nodiscard]] static std::string formatHistoryEntry(HistoryEntry const& e) {
      return std::format("[{}]: {} [{}]", Time(e.timepoint), e.message, FilePosition(e.location));
      }

   /// \brief Complete history as a multi-line string block
   [[nodiscard]] std::string historyText() const {
      if (!hasHistory()) return {};
      std::ostringstream os;
      std::println(os, "History:");
      for (auto const& it : history.view()) std::println(os, "{}", formatHistoryEntry(it));
      return os.str();
      }      

   virtual void rebuild() const {
      }
      
   static std::string cutPath(std::string const& pathString) {
	  if (fs::path path(pathString);  fs::is_regular_file(path)) [[likely]] {
         std::vector<std::string> parts;
         for (const auto& part : path) {
			if (part != path.root_name() && part != "/" && part != "\\") parts.emplace_back(part.string());
            }

		 switch (auto size = parts.size(); size) {
			case 0: return ""s;
			case 1: return parts[0];
			case 2: return parts[0] + "/"s + parts[1];
			[[likely]] default: return "../"s + parts[size - 2] + "/"s + parts[size - 1];
            }
         }
	  else return pathString;
      }

   static std::string TimePosition(std::string const& msg, timestamp_ty const& _time, src_loc const& loc) {
   #if !defined CHRONO_MISSED_ZONEDB
   // #if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
	  auto const cz_ts = std::chrono::current_zone()->to_local(_time);
   #else
	  using namespace std::chrono_literals;
	  auto const cz_ts = _time + 1h;   // aktuell fest für MEZ
   #endif
	  auto const millis = std::chrono::duration_cast<std::chrono::milliseconds>(_time.time_since_epoch()) % 1000;
	  return std::format("[{} {:%d.%m.%Y %X},{:03d} in function \"{}\" in file \"{}\" at line {}]",
							msg, cz_ts, millis.count(), loc.function_name(), cutPath(loc.file_name()), loc.line());
	  }

   static std::string Time(timestamp_ty const& _time) {
   #if !defined CHRONO_MISSED_ZONEDB
	  auto const cz_ts = std::chrono::current_zone()->to_local(_time);
   #else
	  using namespace std::chrono_literals;
	  auto const cz_ts = _time + 1h;   // aktuell fest für MEZ
   #endif
	  auto const millis = std::chrono::duration_cast<std::chrono::milliseconds>(_time.time_since_epoch()) % 1000;
	  return std::format("{:%d.%m.%Y %X},{:03d}", cz_ts, millis.count());
      }

   static std::string Position(src_loc const& loc) {
      return std::format("in file \"{}\" at line {}", cutPath(loc.file_name()), loc.line());
      }

   static std::string FilePosition(src_loc const& loc) {
      return std::format("in function \"{}\" in file \"{}\" at line {}",
                           loc.function_name(), cutPath(loc.file_name()), loc.line());
      }

};



template <typename ty>
concept WrappedException = std::is_base_of_v<std::exception, ty> &&
   requires(ty t, ty const& t_ref) {
      { ty(t_ref) } -> std::convertible_to<ty>;
      { t.what() }  -> std::same_as<const char*>;
   };

struct in_place_exception_t { explicit in_place_exception_t() = default; };
inline constexpr in_place_exception_t in_place_exception{};

struct in_place_format_t { explicit in_place_format_t() = default; };
inline constexpr in_place_format_t in_place_format{};
   
template<class ty> struct error_name { static constexpr std::string_view value = "error"; };
template<> struct error_name<std::logic_error> { static constexpr std::string_view value = "logic error"; };
template<> struct error_name<std::invalid_argument> { static constexpr std::string_view value = "invalid_argument"; };
template<> struct error_name<std::domain_error> { static constexpr std::string_view value = "domain_error"; };
template<> struct error_name<std::length_error> { static constexpr std::string_view value = "length_error"; };
template<> struct error_name<std::out_of_range> { static constexpr std::string_view value = "out_of_range"; };
// need <future> template<> struct error_name<std::future_error> { static constexpr std::string_view value =
// "future_error"; };
template<> struct error_name<std::runtime_error> { static constexpr std::string_view value = "runtime_error"; };
template<> struct error_name<std::range_error> { static constexpr std::string_view value = "range_error"; };
template<> struct error_name<std::overflow_error> { static constexpr std::string_view value = "overflow_error"; };
template<> struct error_name<std::underflow_error> { static constexpr std::string_view value = "underflow_error"; };
// need <regex> template<> struct error_name<std::regex_error> { static constexpr std::string_view value =
// "regex_error"; };
template<> struct error_name<std::system_error> { static constexpr std::string_view value = "system_error"; };
template<> struct error_name<std::ios_base::failure> { static constexpr std::string_view value = "ios_base::failure"; };
template<> struct error_name<std::filesystem::filesystem_error> { static constexpr std::string_view value = "filesystem::filesystem_error"; };
// need TM TS template<> struct error_name<std::tx_exception> { static constexpr std::string_view value =
// "tx_exception"; };
// next version template<> struct error_name<std::chrono::nonexistent_local_time> { static constexpr std::string_view
// value = "chrono::nonexistent_local_time"; };
// next version template<> struct error_name<std::chrono::ambiguous_local_time> { static constexpr std::string_view
// value = "chrono::ambiguous_local_time"; };
template<> struct error_name<std::format_error> { static constexpr std::string_view value = "format_error"; };
template<> struct error_name<std::bad_typeid> { static constexpr std::string_view value = "bad_typeid"; };
template<> struct error_name<std::bad_cast> { static constexpr std::string_view value = "bad_cast"; };
// need <any> template<> struct error_name<std::bad_any_cast> { static constexpr std::string_view value =
// "bad_any_cast"; };
template<> struct error_name<std::bad_optional_access> { static constexpr std::string_view value = "bad_optional_access"; };
// required template argument template<> struct error_name<std::bad_expected_access> { static constexpr std::string_view
// value = "bad_expected_access"; };
template<> struct error_name<std::bad_weak_ptr> { static constexpr std::string_view value = "bad_weak_ptr"; };
template<> struct error_name<std::bad_function_call> { static constexpr std::string_view value = "bad_function_call"; };
template<> struct error_name<std::bad_alloc> { static constexpr std::string_view value = "bad_alloc"; };
template<> struct error_name<std::bad_array_new_length> { static constexpr std::string_view value = "bad_array_new_length"; };
template<> struct error_name<std::bad_exception> { static constexpr std::string_view value = "bad_exception"; };
template<> struct error_name<std::bad_variant_access> { static constexpr std::string_view value = "bad_variant_access"; };


[[nodiscard]] inline src_loc loc(src_loc const& loc_ = src_loc::current()) { return loc_; }
[[nodiscard]] inline timestamp_ty tp(timestamp_ty const& tp_ = std::chrono::system_clock::now()) { return tp_; }

/// \brief Standardized exception wrapper
/// \details Builds the final message in the constructor so that \what() is O(1).
/// \tparam ty Wrapped exception type, for example std::runtime_error
template <WrappedException ty>
class StandardError : public ExceptionInformation, public ty {
protected:
   /// \brief Stores the final string returned by \what()
   mutable std::string finalMessage_; ///< Hilfsvariable um Speicherdauer der Rückgabe sicherzustellen

   /// \brief Helper used to build the final message
   /// \note typeid(ty).name() ist implementation-defined; optional ersetzbar
   void buildFinal_() const {
      finalMessage_ = std::format("{}: {}\n{}",
         error_name<ty>::value, ty::what(), ExceptionInformation::Message());
      if (this->hasHistory()) {
         finalMessage_ += "\n\n";
         finalMessage_ += this->historyText();
         }
      }

public:
   using used_exception_type = ty;

   StandardError(StandardError const&)            = default;
   StandardError(StandardError&&) noexcept        = default;
   StandardError& operator=(StandardError const&) = default;
   StandardError& operator=(StandardError&&) noexcept = default;
   ~StandardError() override                      = default;

   explicit StandardError(std::string const& strMsg,
                          src_loc const& loc = src_loc::current(),
                          timestamp_ty const& tp = std::chrono::system_clock::now())
      : ExceptionInformation{ "Exception throw at", loc, tp }, ty{ strMsg } {
      buildFinal_();
      }

   template <class S>
      requires (std::constructible_from<ty, S const&> &&
                std::constructible_from<std::string, S const&>)
   explicit StandardError(ExceptionInformation info, S const& msg)
      : ExceptionInformation{ std::move(info) }, ty{ msg } {
      buildFinal_();
      }

   explicit StandardError(char const* cmsg,
                          src_loc const& loc = src_loc::current(),
                          timestamp_ty const& tp = std::chrono::system_clock::now())
      : ExceptionInformation{ "Exception throw at", loc, tp }, ty{ cmsg } {
      buildFinal_();
      }

   template <class... Args>
   explicit StandardError(in_place_exception_t,
                          src_loc loc, timestamp_ty tp,
                          Args&&... args)
      : ExceptionInformation{ "Exception throw at", loc, tp }, ty{ std::forward<Args>(args)... } {
      buildFinal_();
      }

   template <class... Args>
      requires std::constructible_from<ty, Args...>
   explicit StandardError(ExceptionInformation info, in_place_exception_t, Args&&... args)
      : ExceptionInformation{ std::move(info) }, ty{ std::forward<Args>(args)... } {
      buildFinal_();
      }

   template <class... Args>
   explicit StandardError(in_place_exception_t, Args&&... args)
      : ExceptionInformation{}, ty{ std::forward<Args>(args)... } {
      buildFinal_();
	  }

   template <class... FArgs>
   explicit StandardError(in_place_format_t,
						  std::format_string<FArgs...> fmt, FArgs&&... fargs)
	  : ExceptionInformation {}, ty{ std::format(fmt, std::forward<FArgs>(fargs)...) } {
	  buildFinal_();
	  }

   template <class... FArgs>
   explicit StandardError(in_place_format_t,
						  src_loc loc, timestamp_ty tp,
						  std::format_string<FArgs...> fmt, FArgs&&... fargs)
	  : ExceptionInformation{ "Exception at", loc, tp }, ty{ std::format(fmt, std::forward<FArgs>(fargs)...) } {
	  buildFinal_();
	  }

   template <class... FArgs>
	  requires std::constructible_from<ty, std::string>
   explicit StandardError(ExceptionInformation info,
						  in_place_format_t,
						  std::format_string<FArgs...> fmt, FArgs&&... fargs)
	  : ExceptionInformation{ std::move(info) },
		ty{ std::format(fmt, std::forward<FArgs>(fargs)...) } {
	  buildFinal_();
	  }

   virtual void rebuild() const override {
      buildFinal_();
      }
   
   /// \brief Appends a trace message and updates the final error text
   StandardError& appendTrace(std::string_view svMsg,
                              src_loc const& loc = src_loc::current(),
                              timestamp_ty const& tp = std::chrono::system_clock::now()) {
      this->addHistory(svMsg, loc, tp);   // von ExceptionInformation
      buildFinal_();                      // finalMessage_ inkl. History neu aufbauen
      return *this;
   }

   /// \brief Returns the precomputed error message
   /// \returns Pointer to null-terminated storage for the final message
   /// \throw Does not throw
   char const* what() const noexcept override {
      return finalMessage_.c_str();
   }
};


inline adecc::ExceptionInformation const* Catch_StdException(std::exception const& ex,
                                                             src_loc const& loc = src_loc::current(),
                                                             timestamp_ty const& tp = std::chrono::system_clock::now() ) {
   if (auto p = dynamic_cast<adecc::ExceptionInformation const*>(&ex)) {
      p->renew("Exception caught at", loc, tp);
      return p;      
      }
   else return nullptr;
   }

} // namespace adecc
