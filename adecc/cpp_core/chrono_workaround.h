// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file chrono_workaround.h
\brief Portable date and time parsing and formatting helpers for incomplete standard-library chrono support.

\details
Implements numeric date, time, and timestamp parsing and formatting without depending on C I/O.
The header keeps std::chrono types as the core representation while isolating compatibility workarounds
required by supported standard libraries and toolchains.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Conversion as a Central Architectural Element".
- "Time Types as a Strategic Boundary".
- "Frameworks at the Boundaries".
- "The Core Belongs in C++".

\see ARCHITECTURE.md#value-types-policies-and-conversion

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
#include <chrono>
#include <string>
#include <string_view>
#include <array>
#include <optional>
#include <charconv>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <type_traits>

namespace adecc {

/*!
\brief Datumsformate (rein numerisch)
*/
enum class DateFmt : uint32_t {
   ISO,             ///< YYYY-MM-DD
   US,              ///< MM/DD/YYYY
   EUROPE,          ///< DD.MM.YYYY
   ISO_COMPACT,     ///< YYYYMMDD
   EUROPE_COMPACT   ///< DDMMYYYY
   };

/*!
\brief Zeitformate (rein numerisch, 24h)
*/
enum class TimeFmt : uint32_t {
   HMS,         //!< HH:MM:SS
   HMS_COMPACT ///< HHMMSS
   };

/*!
\brief Kombinierte Zeitstempel-Formate
\details The pattern is "<Date> <Time>" with a space as separator.
*/
enum class DateTimeFmt : uint32_t {
   ISO_HMS,             ///< "YYYY-MM-DDTHH:MM:SS"
   US_HMS,              ///< "MM/DD/YYYY HH:MM:SS"
   EUROPE_HMS,          ///< "DD.MM.YYYY HH:MM:SS"
   EUROPE_COMPACT_HMS,  ///< DDMMYYYY HH:MM:SS
   ISO_TIMESTAMP        ///< YYYYMMDDHHMMSS
   };

// ============================================================================
// Internal utilities (no C I/O; only charconv and simple checks)
// ============================================================================
namespace chrono_wk {
   inline void append2(std::string & out, unsigned const u) {
      char b[2]{
         static_cast<char>('0' + ((u / 10) % 10)),
         static_cast<char>('0' + (u % 10))
         };
      out.append(b, b + 2);
      }

   inline void append4(std::string & out, int const v) {
      unsigned u = static_cast<unsigned>(v);
      char b[4]{
         static_cast<char>('0' + ((u / 1000) % 10)),
         static_cast<char>('0' + ((u / 100)  % 10)),
         static_cast<char>('0' + ((u / 10)   % 10)),
         static_cast<char>('0' + (u % 10))
         };
      out.append(b, b + 4);
      }

   inline bool expectChar(std::string_view const sv, std::size_t & pos, char const ch) {
      if(pos >= sv.size() || sv[pos] != ch) return false;
      ++pos; return true;
      }

   inline bool readFixedDigits(std::string_view const sv, std::size_t & pos,
                               std::size_t const n, unsigned & out) {
      if(pos + n > sv.size()) return false;
      auto * b = sv.data() + pos;
      auto * e = b + n;
      unsigned tmp{};
      auto rc = std::from_chars(b, e, tmp);
      if(rc.ec != std::errc{} || rc.ptr != e) return false;
      pos += n; out = tmp; return true;
      }

   inline std::optional<std::chrono::year_month_day> makeCheckedYmd(int const y, unsigned const m, unsigned const d) {
      std::chrono::year_month_day ymd{std::chrono::year{y} / std::chrono::month{m} / std::chrono::day{d}};
      if(!ymd.ok()) return std::nullopt;
      return ymd;
      }

} // namespace 

// ============================================================================
// Formatierer
// ============================================================================

/*!
\brief Datum formatieren
\returns z. B. "2025-09-23", "09/23/2025", "23.09.2025"
*/
inline std::string formatDate(std::chrono::year_month_day const & ymd, DateFmt const fmt) {
   std::string s;
   s.reserve(10);
   switch(fmt) {
      case DateFmt::ISO: {
         chrono_wk::append4(s, int(ymd.year()));
         s.push_back('-');
         chrono_wk::append2(s, unsigned(ymd.month()));
         s.push_back('-');
         chrono_wk::append2(s, unsigned(ymd.day()));
         break;
         }
      case DateFmt::US: {
         chrono_wk::append2(s, unsigned(ymd.month()));
         s.push_back('/');
         chrono_wk::append2(s, unsigned(ymd.day()));
         s.push_back('/');
         chrono_wk::append4(s, int(ymd.year()));
         break;
         }
      case DateFmt::EUROPE: {
         chrono_wk::append2(s, unsigned(ymd.day()));
         s.push_back('.');
         chrono_wk::append2(s, unsigned(ymd.month()));
         s.push_back('.');
         chrono_wk::append4(s, int(ymd.year()));
         break;
         }
	  case DateFmt::ISO_COMPACT: {
         chrono_wk::append4(s, int(ymd.year()));
		 chrono_wk::append2(s, unsigned(ymd.month()));
         chrono_wk::append2(s, unsigned(ymd.day()));
         break;
	     }
       
	  case DateFmt::EUROPE_COMPACT: {
		 chrono_wk::append2(s, unsigned(ymd.day()));
         chrono_wk::append2(s, unsigned(ymd.month()));
         chrono_wk::append4(s, int(ymd.year()));
         break;
	     }
      }
   return s;
   }

/*!
\brief Formats a 24-hour time value.
\returns "HH:MM:SS"
*/
inline std::string formatTime(std::chrono::hh_mm_ss<std::chrono::seconds> const & hms, TimeFmt const fmt = TimeFmt::HMS) {
   (void)fmt; // aktuell nur HMS, wird so aber benutzt
   std::string s; s.reserve(8);
   chrono_wk::append2(s, unsigned(hms.hours().count()));
   if(fmt != TimeFmt::HMS_COMPACT) s.push_back(':');
   chrono_wk::append2(s, unsigned(hms.minutes().count()));
   if(fmt != TimeFmt::HMS_COMPACT) s.push_back(':');
   chrono_wk::append2(s, unsigned(hms.seconds().count()));
   return s;
   }

/*!
\brief Zeitstempel formatieren
\returns "<DATE> <TIME>" according to \c DateTimeFmt
*/
inline std::string formatDateTime(std::chrono::system_clock::time_point const tp, DateTimeFmt const fmt) {
   using namespace std::chrono;
   auto tpSec = time_point_cast<seconds>(tp);
   auto day   = floor<days>(tpSec);
   auto tod   = std::chrono::hh_mm_ss<std::chrono::seconds>{duration_cast<seconds>(tpSec - day)};
   std::chrono::year_month_day  ymd{day};

   switch(fmt) {
      case DateTimeFmt::ISO_HMS:            return formatDate(ymd, DateFmt::ISO)             + 'T' + formatTime(tod);
      case DateTimeFmt::US_HMS:             return formatDate(ymd, DateFmt::US)              + ' ' + formatTime(tod);
      case DateTimeFmt::EUROPE_HMS:         return formatDate(ymd, DateFmt::EUROPE)          + ' ' + formatTime(tod);
	  case DateTimeFmt::EUROPE_COMPACT_HMS: return formatDate(ymd, DateFmt::EUROPE_COMPACT)  + ' ' + formatTime(tod);
      case DateTimeFmt::ISO_TIMESTAMP:      return formatDate(ymd, DateFmt::ISO_COMPACT) + formatTime(tod, TimeFmt::HMS_COMPACT);
      }
   return {};
   }

// ============================================================================
// Parsers return std::optional; an empty optional indicates an error
// ============================================================================

/*!
\brief Datum parsen
\returns A valid \c Ymd or \c std::nullopt
*/
inline std::optional<std::chrono::year_month_day> parseDate(std::string_view const sv, DateFmt const fmt) {
   using namespace chrono_wk;
   std::size_t pos { 0 };
   unsigned a { 0 }, b { 0 }, c { 0 };

   switch(fmt) {
      case DateFmt::ISO: {
         if(!readFixedDigits(sv, pos, 4, a)) return std::nullopt;
         if(!expectChar(sv, pos, '-'))       return std::nullopt;
         if(!readFixedDigits(sv, pos, 2, b)) return std::nullopt;
         if(!expectChar(sv, pos, '-'))       return std::nullopt;
         if(!readFixedDigits(sv, pos, 2, c)) return std::nullopt;
         if(pos != sv.size())                return std::nullopt;
         return makeCheckedYmd(static_cast<int>(a), b, c);
         }
      case DateFmt::ISO_COMPACT: {
         if(!readFixedDigits(sv, pos, 4, a)) return std::nullopt;
         if(!readFixedDigits(sv, pos, 2, b)) return std::nullopt;
         if(!readFixedDigits(sv, pos, 2, c)) return std::nullopt;
         if(pos != sv.size())                return std::nullopt;
         return makeCheckedYmd(static_cast<int>(a), b, c);
         }
         
      case DateFmt::US: {
         if(!readFixedDigits(sv, pos, 2, a)) return std::nullopt; // MM
         if(!expectChar(sv, pos, '/'))       return std::nullopt;
         if(!readFixedDigits(sv, pos, 2, b)) return std::nullopt; // DD
         if(!expectChar(sv, pos, '/'))       return std::nullopt;
         if(!readFixedDigits(sv, pos, 4, c)) return std::nullopt; // YYYY
         if(pos != sv.size())                return std::nullopt;
         return makeCheckedYmd(static_cast<int>(c), a, b);
         }
      case DateFmt::EUROPE: {
         if(!readFixedDigits(sv, pos, 2, a)) return std::nullopt; // DD
         if(!expectChar(sv, pos, '.'))       return std::nullopt;
         if(!readFixedDigits(sv, pos, 2, b)) return std::nullopt; // MM
         if(!expectChar(sv, pos, '.'))       return std::nullopt;
         if(!readFixedDigits(sv, pos, 4, c)) return std::nullopt; // YYYY
         if(pos != sv.size())                return std::nullopt;
         return makeCheckedYmd(static_cast<int>(c), b, a);
	     }
	  case DateFmt::EUROPE_COMPACT: {
         if(!readFixedDigits(sv, pos, 2, a)) return std::nullopt; // DD
         if(!readFixedDigits(sv, pos, 2, b)) return std::nullopt; // MM
         if(!readFixedDigits(sv, pos, 4, c)) return std::nullopt; // YYYY
         if(pos != sv.size())                return std::nullopt;
         return makeCheckedYmd(static_cast<int>(c), b, a);
	     }
      }
   return std::nullopt;
   }

/*!
\brief Parses a 24-hour time value.
\returns \c hh_mm_ss<seconds> or \c std::nullopt
*/
inline std::optional<std::chrono::hh_mm_ss<std::chrono::seconds>> parseTime(std::string_view const sv, TimeFmt const fmt = TimeFmt::HMS)  {
   using namespace chrono_wk;
   std::size_t pos = 0;
   unsigned h = 0, m = 0, s = 0;

   if(!readFixedDigits(sv, pos, 2, h)) return std::nullopt;
   if(fmt == TimeFmt::HMS)
      if(!expectChar(sv, pos, ':'))    return std::nullopt;
   if(!readFixedDigits(sv, pos, 2, m)) return std::nullopt;

   if(pos == sv.size()) {
      // The format was hh:mm; set seconds to zero
      s = 0;
      }
   else {
      // The format must be hh:mm:ss
      if(fmt == TimeFmt::HMS)
         if(!expectChar(sv, pos, ':'))    return std::nullopt;
      if(!readFixedDigits(sv, pos, 2, s)) return std::nullopt;
      if(pos != sv.size())                return std::nullopt;
      }

   if(h > 23 || m > 59 || s > 59) return std::nullopt;

   using namespace std::chrono;
   return std::chrono::hh_mm_ss<std::chrono::seconds>{hours{h} + minutes{m} + seconds{s}};
   }

/*!
\brief Zeitstempel parsen
\returns \c sys_time<seconds> or \c std::nullopt
*/
inline std::optional<std::chrono::system_clock::time_point> parseDateTime(std::string_view const sv, DateTimeFmt const fmt) {
   auto splitOnce = [&fmt](std::string_view s) -> std::optional<std::pair<std::string_view,std::string_view>> {
      auto space = (fmt == DateTimeFmt::ISO_HMS ? 'T' : ' ');
      auto p = s.find(space);
      if(p == std::string_view::npos) return std::nullopt;
      return std::pair{s.substr(0, p), s.substr(p + 1)};
      };

   auto partsOpt = splitOnce(sv);
   if(!partsOpt) return std::nullopt;

   auto const & [svDate, svTime] = *partsOpt;

   std::optional<std::chrono::year_month_day> ymd;
   switch(fmt) {
      case DateTimeFmt::ISO_HMS:            ymd = parseDate(svDate, DateFmt::ISO);            break;
      case DateTimeFmt::US_HMS:             ymd = parseDate(svDate, DateFmt::US);             break;
      case DateTimeFmt::EUROPE_HMS:         ymd = parseDate(svDate, DateFmt::EUROPE);         break;
      case DateTimeFmt::EUROPE_COMPACT_HMS: ymd = parseDate(svDate, DateFmt::EUROPE_COMPACT); break;
      case DateTimeFmt::ISO_TIMESTAMP:      ymd = parseDate(svDate, DateFmt::ISO_COMPACT);    break;
      }
   if(!ymd) return std::nullopt;

   auto hms = parseTime(svTime, ( fmt == DateTimeFmt::ISO_TIMESTAMP ?  TimeFmt::HMS_COMPACT : TimeFmt::HMS ) );
   if(!hms) return std::nullopt;

   using namespace std::chrono;
   std::chrono::sys_days sd{*ymd};
   auto dur = hms->to_duration();
   return std::chrono::system_clock::time_point{sd + dur};
   }


[[nodiscard]] constexpr std::string_view ToString(DateFmt const theValue) noexcept {
   switch (theValue) {
      case DateFmt::ISO:
         return "ISO (YYYY-MM-DD)";
      case DateFmt::US:
         return "US (MM/DD/YYYY)";
      case DateFmt::EUROPE:
         return "EUROPE (DD.MM.YYYY)";
      case DateFmt::ISO_COMPACT:
         return "ISO_COMPACT (YYYYMMDD)";
      case DateFmt::EUROPE_COMPACT:
         return "EUROPE_COMPACT (DDMMYYYY)";
      }

   return "UNKNOWN";
}


[[nodiscard]] constexpr std::string_view ToString(TimeFmt const theValue) noexcept {
   switch (theValue) {
      case TimeFmt::HMS:
         return "HMS (HH:MM:SS)";
      case TimeFmt::HMS_COMPACT:
         return "HMS_COMPACT (HHMMSS)";
      }

   return "UNKNOWN";
}



[[nodiscard]] constexpr std::string_view ToString(DateTimeFmt const theValue) noexcept {
   switch (theValue) {
      case DateTimeFmt::ISO_HMS:
         return "ISO_HMS (YYYY-MM-DDTHH:MM:SS)";
      case DateTimeFmt::US_HMS:
         return "US_HMS (MM/DD/YYYY HH:MM:SS)";
      case DateTimeFmt::EUROPE_HMS:
         return "EUROPE_HMS (DD.MM.YYYY HH:MM:SS)";
      case DateTimeFmt::EUROPE_COMPACT_HMS:
         return "EUROPE_COMPACT_HMS (DDMMYYYY HH:MM:SS)";
      case DateTimeFmt::ISO_TIMESTAMP:
         return "ISO_TIMESTAMP (YYYYMMDDHHMMSS)";
      }

   return "UNKNOWN";
}


} // namespace adecc


template<>
struct std::formatter<adecc::DateFmt, char> {
   std::formatter<std::string_view, char> m_aFormatter;

   constexpr auto parse(std::format_parse_context& aContext) {
      return m_aFormatter.parse(aContext);
      }

   template<typename theFormatContext>
   auto format(adecc::DateFmt const theValue, theFormatContext& aContext) const {
      return m_aFormatter.format(adecc::ToString(theValue), aContext);
      }
};

template<>
struct std::formatter<adecc::TimeFmt, char> {
   std::formatter<std::string_view, char> m_aFormatter;

   constexpr auto parse(std::format_parse_context& aContext) {
      return m_aFormatter.parse(aContext);
      }

   template<typename theFormatContext>
   auto format(adecc::TimeFmt const theValue, theFormatContext& aContext) const {
      return m_aFormatter.format(adecc::ToString(theValue), aContext);
      }
};


template<>
struct std::formatter<adecc::DateTimeFmt, char> {
   std::formatter<std::string_view, char> m_aFormatter;

   constexpr auto parse(std::format_parse_context& aContext) {
      return m_aFormatter.parse(aContext);
      }

   template<typename theFormatContext>
   auto format(adecc::DateTimeFmt const theValue, theFormatContext& aContext) const {
      return m_aFormatter.format(adecc::ToString(theValue), aContext);
      }
};