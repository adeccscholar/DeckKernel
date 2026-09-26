// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file convert_c_time_types.h
\brief Conversions between legacy C time structures and the std::chrono-based core time types.

\details
Bridges std::tm and related C time representations with the date, time, and timestamp forms used by the
C++ core. Legacy time APIs remain at a controlled conversion boundary so that domain code can stay based
on standard C++ chrono types.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Conversion as a Central Architectural Element".
- "Safe Conversions Instead of Accidental Casts".
- "Time Types as a Strategic Boundary".
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

#include "convert_core.h"

#include <chrono>
#include <ctime>

namespace adecc {

enum class Tz : uint32_t { UTC, Local };

namespace details {

// \brief Thread-safe wrappers for gmtime/localtime; the result is copied
// \note May require platform-specific adaptation

inline std::tm GmTimeCopy(std::time_t const tt) {
   std::tm out{};
#if defined(_WIN32)
   if(::gmtime_s(&out, &tt) != 0) throw std::runtime_error("gmtime_s failed");
#elif defined(__unix__)
   if(::gmtime_r(&tt, &out) == nullptr) throw std::runtime_error("gmtime_r failed");
#else
   if(auto* p = std::gmtime(&tt)) out = *p; else throw std::runtime_error("gmtime failed");
#endif
   return out;
}

inline std::tm LocalTimeCopy(std::time_t const tt) {
   std::tm out{};
#if defined(_WIN32)
   if(::localtime_s(&out, &tt) != 0) throw std::runtime_error("localtime_s failed");
#elif defined(__unix__)
   if(::localtime_r(&tt, &out) == nullptr) throw std::runtime_error("localtime_r failed");
#else
   if(auto* p = std::localtime(&tt)) out = *p; else throw std::runtime_error("localtime failed");
#endif
   return out;
}

// \brief Builds a UTC sys_time from std::tm, interpreting its fields as UTC
inline std::chrono::sys_time<std::chrono::seconds> SysTimeFromTmUTC(std::tm const& tmv) {
   using namespace std::chrono;
   // tm_year: years since 1900; tm_mon: 0..11
   std::chrono::year const y{ tmv.tm_year + 1900 };
   std::chrono::month const m{ static_cast<unsigned>(tmv.tm_mon + 1) };
   std::chrono::day const d{ static_cast<unsigned>(tmv.tm_mday) };
   if(!y.ok() || !m.ok() || !std::chrono::year_month_day{y/m/d}.ok())
      throw std::runtime_error("invalid tm date");
   sys_days const sd{ y/m/d };
   auto const h = std::chrono::hours{ tmv.tm_hour };
   auto const min = std::chrono::minutes{ tmv.tm_min };
   auto const s = std::chrono::seconds{ tmv.tm_sec };
   return sys_time<std::chrono::seconds>{ sd.time_since_epoch() + h + min + s };
}

// \brief Builds std::tm from Y-M-D and H:M:S; UTC/local time is irrelevant for the raw fields
inline std::tm TmFromYmdHms(int yy, int mm, int dd, int hh, int mi, int ss) {
   std::tm tmv{};
   tmv.tm_year = yy - 1900;
   tmv.tm_mon  = mm - 1;
   tmv.tm_mday = dd;
   tmv.tm_hour = hh;
   tmv.tm_min  = mi;
   tmv.tm_sec  = ss;
   tmv.tm_isdst = -1;
   return tmv;
}
   
} // end of namespace details


// --------------------------------------------------------------
// system_clock::time_point <-> std::time_t
// --------------------------------------------------------------

template<>
struct Convert<std::chrono::system_clock::time_point, std::time_t> {
   static std::time_t apply(std::chrono::system_clock::time_point const& tp) {
      return std::chrono::system_clock::to_time_t(tp);
   }
};

template<>
struct Convert<std::time_t, std::chrono::system_clock::time_point> {
   static std::chrono::system_clock::time_point apply(std::time_t const tt) {
      return std::chrono::system_clock::from_time_t(tt);
   }
};

// --------------------------------------------------------------
// system_clock::time_point <-> std::tm  (UTC / Local via Tz)
// --------------------------------------------------------------

template<>
struct Convert<std::chrono::system_clock::time_point, std::tm> {
   static std::tm apply(std::chrono::system_clock::time_point const& tp) {
      auto const tt = Convert<std::chrono::system_clock::time_point, std::time_t>::apply(tp);
      return details::GmTimeCopy(tt); // Default: UTC
   }
   static std::tm apply(std::chrono::system_clock::time_point const& tp, Tz const tz) {
      auto const tt = Convert<std::chrono::system_clock::time_point, std::time_t>::apply(tp);
      return (tz == Tz::UTC) ? details::GmTimeCopy(tt) : details::LocalTimeCopy(tt);
   }
};

template<>
struct Convert<std::tm, std::chrono::system_clock::time_point> {
   // Default: interpretiere tm als UTC
   static std::chrono::system_clock::time_point apply(std::tm const& tmv) {
      return std::chrono::time_point<std::chrono::system_clock>(
         details::SysTimeFromTmUTC(tmv));
   }
   static std::chrono::system_clock::time_point apply(std::tm const& tmv, Tz const tz) {
      if(tz == Tz::UTC) {
         return apply(tmv);
      } else {
         std::tm tmp = tmv; // mktime darf tm ändern (Normalisierung)
         std::time_t const tt = std::mktime(&tmp); // interpretiert als Lokalzeit
         if(tt == std::time_t(-1)) throw std::runtime_error("mktime failed");
         return std::chrono::system_clock::from_time_t(tt);
      }
   }
};

// --------------------------------------------------------------
// --------------------------------------------------------------

template<>
struct Convert<std::time_t, std::tm> {
   static std::tm apply(std::time_t const tt) {
      return details::GmTimeCopy(tt); // Default: UTC
   }
   static std::tm apply(std::time_t const tt, Tz const tz) {
      return (tz == Tz::UTC) ? details::GmTimeCopy(tt) : details::LocalTimeCopy(tt);
   }
};

template<>
struct Convert<std::tm, std::time_t> {
   // Default: interpret tm as UTC without non-portable timegm:
   static std::time_t apply(std::tm const& tmv) {
      auto const st = details::SysTimeFromTmUTC(tmv);
      auto const secs = std::chrono::time_point_cast<std::chrono::seconds>(st).time_since_epoch().count();
      return static_cast<std::time_t>(secs);
   }
   static std::time_t apply(std::tm const& tmv, Tz const tz) {
      if(tz == Tz::UTC) return apply(tmv);
      std::tm tmp = tmv;
      std::time_t const tt = std::mktime(&tmp); // lokal
      if(tt == std::time_t(-1)) throw std::runtime_error("mktime failed");
      return tt;
   }
};

// --------------------------------------------------------------
// year_month_day <-> std::tm
// --------------------------------------------------------------

template<>
struct Convert<std::chrono::year_month_day, std::tm> {
   static std::tm apply(std::chrono::year_month_day const& ymd) {
      int const yy = int(ymd.year());
      unsigned const mm = unsigned(ymd.month());
      unsigned const dd = unsigned(ymd.day());
      if(!ymd.ok()) throw std::runtime_error("invalid year_month_day");
      return details::TmFromYmdHms(yy, int(mm), int(dd), 0, 0, 0);
   }
};

template<>
struct Convert<std::tm, std::chrono::year_month_day> {
   static std::chrono::year_month_day apply(std::tm const& tmv) {
      std::chrono::year const y{ tmv.tm_year + 1900 };
      std::chrono::month const m{ static_cast<unsigned>(tmv.tm_mon + 1) };
      std::chrono::day const d{ static_cast<unsigned>(tmv.tm_mday) };
      std::chrono::year_month_day const ymd{ y/m/d };
      if(!ymd.ok()) throw std::runtime_error("invalid tm date");
      return ymd;
   }
};

// --------------------------------------------------------------
// hh_mm_ss<seconds> <-> std::tm  (nur Zeitanteil)
// --------------------------------------------------------------

template<>
struct Convert<std::chrono::hh_mm_ss<std::chrono::seconds>, std::tm> {
   static std::tm apply(std::chrono::hh_mm_ss<std::chrono::seconds> const& hms) {
      return details::TmFromYmdHms(1900, 1, 1,
                                  int(hms.hours().count()),
                                  int(hms.minutes().count()),
                                  int(hms.seconds().count()));
   }
};

template<>
struct Convert<std::tm, std::chrono::hh_mm_ss<std::chrono::seconds>> {
   static std::chrono::hh_mm_ss<std::chrono::seconds> apply(std::tm const& tmv) {
      using namespace std::chrono;
      return hh_mm_ss<seconds>{ hours{tmv.tm_hour} + minutes{tmv.tm_min} + seconds{tmv.tm_sec} };
   }
};

// --------------------------------------------------------------
// --------------------------------------------------------------

template<>
struct Convert<std::time_t, std::string> {
   // Default: ISO "YYYY-MM-DD HH:MM:SS" in UTC
   static std::string apply(std::time_t const tt) {
      auto const tmv = Convert<std::time_t, std::tm>::apply(tt, Tz::UTC);
      std::ostringstream os;
      os << std::put_time(&tmv, "%Y-%m-%d %H:%M:%S");
      return os.str();
   }
   static std::string apply(std::time_t const tt, std::string_view const fmt, Tz const tz = Tz::UTC) {
      auto const tmv = Convert<std::time_t, std::tm>::apply(tt, tz);
      std::ostringstream os;
      os << std::put_time(&tmv, std::string{fmt}.c_str());
      return os.str();
   }
};

template<>
struct Convert<std::string, std::time_t> {
   // Default: parse ISO "YYYY-MM-DD HH:MM:SS" als UTC
   static std::time_t apply(std::string const& s) {
      std::tm tmv{};
      std::istringstream is{s};
      is >> std::get_time(&tmv, "%Y-%m-%d %H:%M:%S");
      if(!is) throw std::runtime_error("parse time_t failed (ISO)");
      return Convert<std::tm, std::time_t>::apply(tmv, Tz::UTC);
   }
   static std::time_t apply(std::string const& s, std::string_view const fmt, Tz const tz = Tz::UTC) {
      std::tm tmv{};
      std::istringstream is{s};
      is >> std::get_time(&tmv, std::string{fmt}.c_str());
      if(!is) throw std::runtime_error("parse time_t failed (fmt)");
      return Convert<std::tm, std::time_t>::apply(tmv, tz);
   }
};

template<>
struct Convert<std::tm, std::string> {
   // Default: ISO "YYYY-MM-DD HH:MM:SS"
   static std::string apply(std::tm const& tmv) {
      std::ostringstream os;
      os << std::put_time(&tmv, "%Y-%m-%d %H:%M:%S");
      return os.str();
   }
   static std::string apply(std::tm const& tmv, std::string_view const fmt) {
      std::ostringstream os;
      os << std::put_time(&tmv, std::string{fmt}.c_str());
      return os.str();
   }
};

template<>
struct Convert<std::string, std::tm> {
   // Default: ISO "YYYY-MM-DD HH:MM:SS"
   static std::tm apply(std::string const& s) {
      std::tm tmv{};
      std::istringstream is{s};
      is >> std::get_time(&tmv, "%Y-%m-%d %H:%M:%S");
      if(!is) throw std::runtime_error("parse tm failed (ISO)");
      return tmv;
   }
   static std::tm apply(std::string const& s, std::string_view const fmt) {
      std::tm tmv{};
      std::istringstream is{s};
      is >> std::get_time(&tmv, std::string{fmt}.c_str());
      if(!is) throw std::runtime_error("parse tm failed (fmt)");
      return tmv;
   }
};

// --------------------------------------------------------------
// String helpers for the existing chrono converters:
//  time_point <-> string (formatiert, UTC default), Delegation via tm
// --------------------------------------------------------------

template<>
struct Convert<std::chrono::system_clock::time_point, std::string> {
   static std::string apply(std::chrono::system_clock::time_point const& tp) {
      auto const tt = Convert<std::chrono::system_clock::time_point, std::time_t>::apply(tp);
      return Convert<std::time_t, std::string>::apply(tt); // ISO UTC
   }
   static std::string apply(std::chrono::system_clock::time_point const& tp, std::string_view const fmt, Tz const tz = Tz::UTC) {
      auto const tt = Convert<std::chrono::system_clock::time_point, std::time_t>::apply(tp);
      return Convert<std::time_t, std::string>::apply(tt, fmt, tz);
   }
};

template<>
struct Convert<std::string, std::chrono::system_clock::time_point> {
   static std::chrono::system_clock::time_point apply(std::string const& s) {
      auto const tt = Convert<std::string, std::time_t>::apply(s); // ISO UTC
      return Convert<std::time_t, std::chrono::system_clock::time_point>::apply(tt);
   }
   static std::chrono::system_clock::time_point apply(std::string const& s, std::string_view const fmt, Tz const tz = Tz::UTC) {
      auto const tt = Convert<std::string, std::time_t>::apply(s, fmt, tz);
      return Convert<std::time_t, std::chrono::system_clock::time_point>::apply(tt);
   }
};




} // namespace adecc


