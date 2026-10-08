#pragma once

#include "database_definitions.h"
#include "database_exception.h"
#include "convert_core.h"
#include "convert_fixed.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

#ifndef ODBCVER
#define ODBCVER 0x0380
#endif

#include <sql.h>
#include <sqlext.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <expected>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace adecc::db::mssql {

   /**
   \brief Connection data for the Microsoft SQL Server backend.
   \details
      The public credential semantics intentionally match the existing SQL
      Server adapters. ODBC-specific settings remain private implementation
      details of this backend.
   */
   struct mssql_credentials {
      std::string strServer{};
      std::string strDatabase{};
      bool        boIntegrated{ true };
      std::string strUser{};
      std::string strPassword{};
      bool        boMARS{ true };
      int         iTimeout{ 30 };
      };


   namespace detail {

      enum class sql_scan_state {
         normal,
         single_quote,
         double_quote,
         bracket_identifier,
         line_comment,
         block_comment
         };


      struct parameter_occurrence {
         std::size_t uLogicalIndex{};
         std::size_t uPhysicalIndex{};
         };


      struct parameter_descriptor {
         SQLSMALLINT iSqlType{};
         SQLULEN     uPrecision{};
         SQLSMALLINT iScale{};
         SQLSMALLINT iNullable{};
         };


      struct parameter_plan {
         std::string                       strSql{};
         std::vector<std::string>          vecLogicalNames{};
         std::vector<parameter_occurrence> vecOccurrences{};
         };


      struct diagnostic_record {
         std::string strSqlState{};
         SQLINTEGER  iNativeError{};
         std::string strMessage{};
         };




      template <class ty>
      struct optional_or_self { using type = ty; };

      template <class ty>
      struct optional_or_self<std::optional<ty>> { using type = ty; };

      template <class ty>
      using optional_or_self_t = typename optional_or_self<ty>::type;
      struct column_descriptor {
         std::string strName{};
         std::string strNativeTypeName{};
         SQLSMALLINT iSqlType{};
         SQLULEN     uPrecision{};
         SQLSMALLINT iScale{};
         SQLSMALLINT iNullable{};
         };


      inline char ToLowerAscii(char const ch) {
         return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
         }


      inline bool EqualName(std::string_view const svLeft,
                            std::string_view const svRight) {
         if (svLeft.size() != svRight.size()) {
            return false;
            }

         return std::ranges::equal(
            svLeft,
            svRight,
            [](char const chLeft, char const chRight) {
               return ToLowerAscii(chLeft) == ToLowerAscii(chRight);
               }
            );
         }


      inline bool IsNameStart(char const ch) {
         auto const uCh = static_cast<unsigned char>(ch);
         return std::isalpha(uCh) != 0 || ch == '_';
         }


      inline bool IsNameChar(char const ch) {
         auto const uCh = static_cast<unsigned char>(ch);
         return std::isalnum(uCh) != 0 || ch == '_';
         }


      inline std::optional<std::size_t> FindLogicalParameter(
         std::vector<std::string> const& vecNames,
         std::string_view const svName
      ) {
         auto const it = std::ranges::find_if(
            vecNames,
            [svName](std::string const& strName) {
               return EqualName(strName, svName);
               }
            );

         if (it == vecNames.end()) {
            return std::nullopt;
            }

         return static_cast<std::size_t>(std::distance(vecNames.begin(), it));
         }


      inline bool ContainsParameter(parameter_plan const& aPlan,
                                    std::string_view const svName) {
         return FindLogicalParameter(aPlan.vecLogicalNames, svName).has_value();
         }


      /**
      \brief Translates the public adecc named-parameter syntax to ODBC markers.
      \details
         Every occurrence of \c :Name becomes one physical ODBC marker \c ?.
         A logical parameter is stored only once in \c vecLogicalNames, while
         \c vecOccurrences maps every physical marker back to that logical
         value. Consequently callers provide a repeated parameter only once.
      */
      inline std::expected<parameter_plan, error_ty> BuildParameterPlan(
         std::string const& strSql
      ) {
         parameter_plan aPlan;
         aPlan.strSql.reserve(strSql.size());

         sql_scan_state aState{ sql_scan_state::normal };

         for (std::size_t uPos{}; uPos < strSql.size();) {
            char const ch = strSql[uPos];

            switch (aState) {
            case sql_scan_state::normal:
               if (ch == '\'') {
                  aPlan.strSql += ch;
                  ++uPos;
                  aState = sql_scan_state::single_quote;
                  continue;
                  }

               if (ch == '"') {
                  aPlan.strSql += ch;
                  ++uPos;
                  aState = sql_scan_state::double_quote;
                  continue;
                  }

               if (ch == '[') {
                  aPlan.strSql += ch;
                  ++uPos;
                  aState = sql_scan_state::bracket_identifier;
                  continue;
                  }

               if (ch == '-' && uPos + 1 < strSql.size() && strSql[uPos + 1] == '-') {
                  aPlan.strSql += "--";
                  uPos += 2;
                  aState = sql_scan_state::line_comment;
                  continue;
                  }

               if (ch == '/' && uPos + 1 < strSql.size() && strSql[uPos + 1] == '*') {
                  aPlan.strSql += "/*";
                  uPos += 2;
                  aState = sql_scan_state::block_comment;
                  continue;
                  }

               if (ch == ':' && uPos + 1 < strSql.size() && IsNameStart(strSql[uPos + 1])) {
                  std::size_t uEnd{ uPos + 2 };

                  while (uEnd < strSql.size() && IsNameChar(strSql[uEnd])) {
                     ++uEnd;
                     }

                  std::string_view const svName{
                     strSql.data() + uPos + 1,
                     uEnd - uPos - 1
                     };

                  auto optLogical = FindLogicalParameter(aPlan.vecLogicalNames, svName);
                  std::size_t uLogical{};

                  if (optLogical) {
                     uLogical = *optLogical;
                     }
                  else {
                     uLogical = aPlan.vecLogicalNames.size();
                     aPlan.vecLogicalNames.emplace_back(svName);
                     }

                  aPlan.vecOccurrences.push_back(parameter_occurrence{
                     .uLogicalIndex = uLogical,
                     .uPhysicalIndex = aPlan.vecOccurrences.size()
                     });
                  aPlan.strSql += '?';
                  uPos = uEnd;
                  continue;
                  }

               aPlan.strSql += ch;
               ++uPos;
               break;

            case sql_scan_state::single_quote:
               aPlan.strSql += ch;
               ++uPos;

               if (ch == '\'' && uPos < strSql.size() && strSql[uPos] == '\'') {
                  aPlan.strSql += strSql[uPos++];
                  }
               else if (ch == '\'') {
                  aState = sql_scan_state::normal;
                  }
               break;

            case sql_scan_state::double_quote:
               aPlan.strSql += ch;
               ++uPos;

               if (ch == '"' && uPos < strSql.size() && strSql[uPos] == '"') {
                  aPlan.strSql += strSql[uPos++];
                  }
               else if (ch == '"') {
                  aState = sql_scan_state::normal;
                  }
               break;

            case sql_scan_state::bracket_identifier:
               aPlan.strSql += ch;
               ++uPos;

               if (ch == ']' && uPos < strSql.size() && strSql[uPos] == ']') {
                  aPlan.strSql += strSql[uPos++];
                  }
               else if (ch == ']') {
                  aState = sql_scan_state::normal;
                  }
               break;

            case sql_scan_state::line_comment:
               aPlan.strSql += ch;
               ++uPos;
               if (ch == '\n') {
                  aState = sql_scan_state::normal;
                  }
               break;

            case sql_scan_state::block_comment:
               if (ch == '*' && uPos + 1 < strSql.size() && strSql[uPos + 1] == '/') {
                  aPlan.strSql += "*/";
                  uPos += 2;
                  aState = sql_scan_state::normal;
                  }
               else {
                  aPlan.strSql += ch;
                  ++uPos;
                  }
               break;
               }
            }

         if (aState == sql_scan_state::single_quote ||
             aState == sql_scan_state::double_quote ||
             aState == sql_scan_state::bracket_identifier ||
             aState == sql_scan_state::block_comment) {
            return std::unexpected(error_ty{
               {},
               "error for set sql in SQL Server query",
               "unterminated SQL literal, identifier, or comment"
               });
            }

         return aPlan;
         }


      inline std::u16string Utf8ToUtf16(std::string_view const svValue) {
         std::u16string strResult;
         strResult.reserve(svValue.size());

         for (std::size_t uPos{}; uPos < svValue.size();) {
            unsigned char const uFirst = static_cast<unsigned char>(svValue[uPos]);
            char32_t uCodePoint{};
            std::size_t uCount{};

            if (uFirst < 0x80) {
               uCodePoint = uFirst;
               uCount = 1;
               }
            else if ((uFirst & 0xE0) == 0xC0) {
               uCodePoint = uFirst & 0x1F;
               uCount = 2;
               }
            else if ((uFirst & 0xF0) == 0xE0) {
               uCodePoint = uFirst & 0x0F;
               uCount = 3;
               }
            else if ((uFirst & 0xF8) == 0xF0) {
               uCodePoint = uFirst & 0x07;
               uCount = 4;
               }
            else {
               throw std::runtime_error("invalid UTF-8 leading byte");
               }

            if (uPos + uCount > svValue.size()) {
               throw std::runtime_error("truncated UTF-8 sequence");
               }

            for (std::size_t uIndex{ 1 }; uIndex < uCount; ++uIndex) {
               unsigned char const uNext = static_cast<unsigned char>(svValue[uPos + uIndex]);
               if ((uNext & 0xC0) != 0x80) {
                  throw std::runtime_error("invalid UTF-8 continuation byte");
                  }
               uCodePoint = (uCodePoint << 6) | (uNext & 0x3F);
               }

            if ((uCount == 2 && uCodePoint < 0x80) ||
                (uCount == 3 && uCodePoint < 0x800) ||
                (uCount == 4 && uCodePoint < 0x10000) ||
                uCodePoint > 0x10FFFF ||
                (uCodePoint >= 0xD800 && uCodePoint <= 0xDFFF)) {
               throw std::runtime_error("non-canonical or invalid UTF-8 code point");
               }

            if (uCodePoint <= 0xFFFF) {
               strResult.push_back(static_cast<char16_t>(uCodePoint));
               }
            else {
               uCodePoint -= 0x10000;
               strResult.push_back(static_cast<char16_t>(0xD800 + (uCodePoint >> 10)));
               strResult.push_back(static_cast<char16_t>(0xDC00 + (uCodePoint & 0x3FF)));
               }

            uPos += uCount;
            }

         return strResult;
         }


      inline std::string Utf16ToUtf8(std::u16string_view const svValue) {
         std::string strResult;
         strResult.reserve(svValue.size());

         for (std::size_t uPos{}; uPos < svValue.size(); ++uPos) {
            char32_t uCodePoint = svValue[uPos];

            if (uCodePoint >= 0xD800 && uCodePoint <= 0xDBFF) {
               if (uPos + 1 >= svValue.size()) {
                  throw std::runtime_error("truncated UTF-16 surrogate pair");
                  }
               char32_t const uLow = svValue[++uPos];
               if (uLow < 0xDC00 || uLow > 0xDFFF) {
                  throw std::runtime_error("invalid UTF-16 surrogate pair");
                  }
               uCodePoint = 0x10000 + ((uCodePoint - 0xD800) << 10) + (uLow - 0xDC00);
               }
            else if (uCodePoint >= 0xDC00 && uCodePoint <= 0xDFFF) {
               throw std::runtime_error("unpaired UTF-16 low surrogate");
               }

            if (uCodePoint <= 0x7F) {
               strResult.push_back(static_cast<char>(uCodePoint));
               }
            else if (uCodePoint <= 0x7FF) {
               strResult.push_back(static_cast<char>(0xC0 | (uCodePoint >> 6)));
               strResult.push_back(static_cast<char>(0x80 | (uCodePoint & 0x3F)));
               }
            else if (uCodePoint <= 0xFFFF) {
               strResult.push_back(static_cast<char>(0xE0 | (uCodePoint >> 12)));
               strResult.push_back(static_cast<char>(0x80 | ((uCodePoint >> 6) & 0x3F)));
               strResult.push_back(static_cast<char>(0x80 | (uCodePoint & 0x3F)));
               }
            else {
               strResult.push_back(static_cast<char>(0xF0 | (uCodePoint >> 18)));
               strResult.push_back(static_cast<char>(0x80 | ((uCodePoint >> 12) & 0x3F)));
               strResult.push_back(static_cast<char>(0x80 | ((uCodePoint >> 6) & 0x3F)));
               strResult.push_back(static_cast<char>(0x80 | (uCodePoint & 0x3F)));
               }
            }

         return strResult;
         }


      inline std::vector<SQLWCHAR> ToOdbcWide(std::string_view const svValue) {
         static_assert(sizeof(SQLWCHAR) == sizeof(char16_t));
         auto const strUtf16 = Utf8ToUtf16(svValue);
         std::vector<SQLWCHAR> vecValue;
         vecValue.reserve(strUtf16.size() + 1);
         for (char16_t const ch : strUtf16) {
            vecValue.push_back(static_cast<SQLWCHAR>(ch));
            }
         vecValue.push_back(SQLWCHAR{});
         return vecValue;
         }


      inline std::string FromOdbcWide(SQLWCHAR const* const pValue,
                                      std::size_t const uLength) {
         static_assert(sizeof(SQLWCHAR) == sizeof(char16_t));
         std::u16string strUtf16;
         strUtf16.reserve(uLength);
         for (std::size_t uIndex{}; uIndex < uLength; ++uIndex) {
            strUtf16.push_back(static_cast<char16_t>(pValue[uIndex]));
            }
         return Utf16ToUtf8(strUtf16);
         }


      inline std::vector<diagnostic_record> CollectDiagnostics(
         SQLSMALLINT const iHandleType,
         SQLHANDLE const hHandle
      ) {
         std::vector<diagnostic_record> vecDiagnostics;

         if (hHandle == SQL_NULL_HANDLE) {
            return vecDiagnostics;
            }

         for (SQLSMALLINT iRecord{ 1 };; ++iRecord) {
            std::array<SQLCHAR, 6> arrState{};
            SQLINTEGER iNativeError{};
            SQLSMALLINT iMessageLength{};
            std::vector<SQLCHAR> vecMessage(512);

            SQLRETURN iResult = SQLGetDiagRecA(
               iHandleType,
               hHandle,
               iRecord,
               arrState.data(),
               &iNativeError,
               vecMessage.data(),
               static_cast<SQLSMALLINT>(vecMessage.size()),
               &iMessageLength
               );

            if (iResult == SQL_NO_DATA) {
               break;
               }

            if (iResult == SQL_SUCCESS_WITH_INFO &&
                iMessageLength >= static_cast<SQLSMALLINT>(vecMessage.size())) {
               vecMessage.resize(static_cast<std::size_t>(iMessageLength) + 1);
               iResult = SQLGetDiagRecA(
                  iHandleType,
                  hHandle,
                  iRecord,
                  arrState.data(),
                  &iNativeError,
                  vecMessage.data(),
                  static_cast<SQLSMALLINT>(vecMessage.size()),
                  &iMessageLength
                  );
               }

            if (!SQL_SUCCEEDED(iResult)) {
               break;
               }

            vecDiagnostics.push_back(diagnostic_record{
               .strSqlState = reinterpret_cast<char const*>(arrState.data()),
               .iNativeError = iNativeError,
               .strMessage = std::string{
                  reinterpret_cast<char const*>(vecMessage.data()),
                  static_cast<std::size_t>(std::max<SQLSMALLINT>(0, iMessageLength))
                  }
               });
            }

         return vecDiagnostics;
         }


      inline std::string FormatDiagnostics(
         std::string_view const svOperation,
         SQLRETURN const iReturn,
         std::vector<diagnostic_record> const& vecDiagnostics
      ) {
         std::ostringstream os;
         os << "ODBC operation: " << svOperation << '\n'
            << "SQLRETURN: " << static_cast<long long>(iReturn) << '\n';

         for (std::size_t uIndex{}; uIndex < vecDiagnostics.size(); ++uIndex) {
            auto const& aDiag = vecDiagnostics[uIndex];
            os << "Diagnostic #" << (uIndex + 1) << '\n'
               << "  SQLSTATE: " << aDiag.strSqlState << '\n'
               << "  Native error: " << aDiag.iNativeError << '\n'
               << "  Message: " << aDiag.strMessage << '\n';
            }

         if (vecDiagnostics.empty()) {
            os << "No diagnostic record was provided by the ODBC driver.\n";
            }

         return os.str();
         }


      inline std::expected<bool, error_ty> CheckOdbc(
         SQLRETURN const iResult,
         SQLSMALLINT const iHandleType,
         SQLHANDLE const hHandle,
         std::string_view const svOperation,
         bool const boInfoIsError = false
      ) {
         if (iResult == SQL_SUCCESS) {
            return true;
            }

         if (iResult == SQL_SUCCESS_WITH_INFO) {
            auto const vecDiagnostics = CollectDiagnostics(iHandleType, hHandle);

            if (!boInfoIsError) {
               return true;
               }

            return std::unexpected(error_ty{
               {},
               std::format("ODBC warning while {}", svOperation),
               FormatDiagnostics(svOperation, iResult, vecDiagnostics)
               });
            }

         auto const vecDiagnostics = CollectDiagnostics(iHandleType, hHandle);
         return std::unexpected(error_ty{
            {},
            std::format("ODBC error while {}", svOperation),
            FormatDiagnostics(svOperation, iResult, vecDiagnostics)
            });
         }


      inline std::string EscapeConnectionValue(std::string const& strValue) {
         bool const boNeedBraces = strValue.find_first_of(";{}") != std::string::npos;
         if (!boNeedBraces) {
            return strValue;
            }

         std::string strResult{"{"};
         for (char const ch : strValue) {
            if (ch == '}') {
               strResult += "}}";
               }
            else {
               strResult += ch;
               }
            }
         strResult += '}';
         return strResult;
         }


      inline std::string AppendScopeIdentity(std::string const& strSql) {
         std::size_t uEnd = strSql.size();
         while (uEnd > 0 && std::isspace(static_cast<unsigned char>(strSql[uEnd - 1])) != 0) {
            --uEnd;
            }

         bool const boHadSemicolon = uEnd > 0 && strSql[uEnd - 1] == ';';
         if (boHadSemicolon) {
            --uEnd;
            }

         std::string strResult = strSql.substr(0, uEnd);
         strResult += "\n; SELECT CONVERT(bigint, SCOPE_IDENTITY()) AS __adecc_identity;";
         return strResult;
         }


      inline date_ty ToDate(SQL_DATE_STRUCT const& aValue) {
         return date_ty{
            std::chrono::year{ aValue.year },
            std::chrono::month{ aValue.month },
            std::chrono::day{ aValue.day }
            };
         }


      inline timestamp_ty ToTimestamp(SQL_TIMESTAMP_STRUCT const& aValue) {
         using namespace std::chrono;

         year_month_day const aDate{
            year{ aValue.year },
            month{ aValue.month },
            day{ aValue.day }
            };

         if (!aDate.ok()) {
            throw std::range_error("invalid date returned by SQL Server");
            }

         sys_days const aDays{ aDate };
         return time_point_cast<system_clock::duration>(
            aDays + hours{ aValue.hour } + minutes{ aValue.minute } +
            seconds{ aValue.second } + nanoseconds{ aValue.fraction }
            );
         }


      inline time_ty ToTime(SQL_TIME_STRUCT const& aValue) {
         using namespace std::chrono;
         auto const aSeconds = hours{ aValue.hour } + minutes{ aValue.minute } +
                               seconds{ aValue.second };
         return time_ty{ duration_cast<seconds>(aSeconds) };
         }


      inline SQL_DATE_STRUCT FromDate(date_ty const& aValue) {
         if (!aValue.ok()) {
            throw std::range_error("invalid adecc::date_ty value");
            }

         return SQL_DATE_STRUCT{
            static_cast<SQLSMALLINT>(int(aValue.year())),
            static_cast<SQLUSMALLINT>(unsigned(aValue.month())),
            static_cast<SQLUSMALLINT>(unsigned(aValue.day()))
            };
         }


      inline SQL_TIMESTAMP_STRUCT FromTimestamp(timestamp_ty const& aValue) {
         using namespace std::chrono;

         auto const aDays = floor<days>(aValue);
         year_month_day const aDate{ aDays };
         auto const aTime = aValue - aDays;
         auto const aHours = duration_cast<hours>(aTime);
         auto const aMinutes = duration_cast<minutes>(aTime - aHours);
         auto const aSeconds = duration_cast<seconds>(aTime - aHours - aMinutes);
         auto const aNanoseconds = duration_cast<nanoseconds>(
            aTime - aHours - aMinutes - aSeconds
            );

         return SQL_TIMESTAMP_STRUCT{
            static_cast<SQLSMALLINT>(int(aDate.year())),
            static_cast<SQLUSMALLINT>(unsigned(aDate.month())),
            static_cast<SQLUSMALLINT>(unsigned(aDate.day())),
            static_cast<SQLUSMALLINT>(aHours.count()),
            static_cast<SQLUSMALLINT>(aMinutes.count()),
            static_cast<SQLUSMALLINT>(aSeconds.count()),
            static_cast<SQLUINTEGER>(aNanoseconds.count())
            };
         }


      inline SQL_TIME_STRUCT FromTime(time_ty const& aValue) {
         using namespace std::chrono;
         return SQL_TIME_STRUCT{
            static_cast<SQLUSMALLINT>(aValue.hours().count()),
            static_cast<SQLUSMALLINT>(aValue.minutes().count()),
            static_cast<SQLUSMALLINT>(aValue.seconds().count())
            };
         }


      inline std::string NumericStructToString(SQL_NUMERIC_STRUCT const& aValue) {
         unsigned long long uMagnitude{};
         constexpr std::size_t uCopy = sizeof(uMagnitude) < SQL_MAX_NUMERIC_LEN
            ? sizeof(uMagnitude)
            : SQL_MAX_NUMERIC_LEN;
         std::memcpy(&uMagnitude, aValue.val, uCopy);

         std::string strDigits = std::to_string(uMagnitude);
         int const iScale = static_cast<int>(aValue.scale);

         if (iScale > 0) {
            if (strDigits.size() <= static_cast<std::size_t>(iScale)) {
               strDigits.insert(0, static_cast<std::size_t>(iScale) - strDigits.size() + 1, '0');
               }
            strDigits.insert(strDigits.size() - static_cast<std::size_t>(iScale), 1, '.');
            }

         if (aValue.sign == 0 && uMagnitude != 0) {
            strDigits.insert(strDigits.begin(), '-');
            }

         return strDigits;
         }

   } // namespace detail


   class odbc_database {
   private:
      mssql_credentials aCredentials{};
      SQLHENV hEnvironment{ SQL_NULL_HENV };
      SQLHDBC hConnection{ SQL_NULL_HDBC };
      bool boTransactionActive{ false };

   public:
      odbc_database() = default;


      explicit odbc_database(mssql_credentials const& aCred)
         : aCredentials{ aCred } {
         ConnectOrThrow_();
         }


      odbc_database(
         std::string const& strServer,
         std::string const& strDatabase,
         bool const boIntegrated = true,
         std::string const& strUser = {},
         std::string const& strPassword = {}
      )
         : aCredentials{
              .strServer = strServer,
              .strDatabase = strDatabase,
              .boIntegrated = boIntegrated,
              .strUser = strUser,
              .strPassword = strPassword
              } {
         ConnectOrThrow_();
         }


      odbc_database(odbc_database const& rhs)
         : aCredentials{ rhs.aCredentials } {
         if (rhs.Connected()) {
            ConnectOrThrow_();
            }
         }


      odbc_database& operator=(odbc_database const& rhs) {
         if (this != &rhs) {
            Close_();
            aCredentials = rhs.aCredentials;
            if (rhs.Connected()) {
               ConnectOrThrow_();
               }
            }
         return *this;
         }


      odbc_database(odbc_database&& rhs) noexcept
         : aCredentials{ std::move(rhs.aCredentials) }
         , hEnvironment{ std::exchange(rhs.hEnvironment, SQLHENV{}) }
         , hConnection{ std::exchange(rhs.hConnection, SQLHDBC{}) }
         , boTransactionActive{ std::exchange(rhs.boTransactionActive, false) } {
         }


      odbc_database& operator=(odbc_database&& rhs) noexcept {
         if (this != &rhs) {
            Close_();
            aCredentials = std::move(rhs.aCredentials);
            hEnvironment = std::exchange(rhs.hEnvironment, SQLHENV{});
            hConnection = std::exchange(rhs.hConnection, SQLHDBC{});
            boTransactionActive = std::exchange(rhs.boTransactionActive, false);
            }
         return *this;
         }


      ~odbc_database() {
         Close_();
         }


      mssql_credentials const& Credentials() const noexcept {
         return aCredentials;
         }


      std::string const& Server() const noexcept {
         return aCredentials.strServer;
         }


      std::string const& Database() const noexcept {
         return aCredentials.strDatabase;
         }


      bool Integrated() const noexcept {
         return aCredentials.boIntegrated;
         }


      std::string const& UserName() const noexcept {
         return aCredentials.strUser;
         }


      std::string const& Password() const noexcept {
         return aCredentials.strPassword;
         }


      std::string GetServer() const {
         return aCredentials.strServer;
         }


      std::string GetInformation() const {
         std::ostringstream os;
         os << "DriverID=MSSQL/ODBC\n"
            << "Driver=ODBC Driver 18 for SQL Server\n"
            << "Server=" << aCredentials.strServer << '\n'
            << "Database=" << aCredentials.strDatabase << '\n'
            << "Integrated=" << (aCredentials.boIntegrated ? "Yes" : "No") << '\n'
            << "User=" << aCredentials.strUser << '\n'
            << "Password=" << (aCredentials.boIntegrated
                  ? "<integrated>"
                  : (aCredentials.strPassword.empty() ? "" : "***")) << '\n'
            << "MARS=" << (aCredentials.boMARS ? "Yes" : "No") << '\n'
            << "Encrypt=No\n"
            << "LoginTimeout=" << aCredentials.iTimeout << '\n';
         return os.str();
         }


      framework_result_ty Connect() {
         Close_();

         SQLRETURN iResult = SQLAllocHandle(
            SQL_HANDLE_ENV,
            SQL_NULL_HANDLE,
            &hEnvironment
            );
         if (!SQL_SUCCEEDED(iResult)) {
            Close_();
            return std::unexpected(error_ty{
               {},
               "database not connected",
               "SQLAllocHandle(SQL_HANDLE_ENV) failed"
               });
            }

         iResult = SQLSetEnvAttr(
            hEnvironment,
            SQL_ATTR_ODBC_VERSION,
            reinterpret_cast<SQLPOINTER>(SQL_OV_ODBC3_80),
            0
            );
         if (auto aCheck = detail::CheckOdbc(
                iResult,
                SQL_HANDLE_ENV,
                hEnvironment,
                "SQLSetEnvAttr(SQL_ATTR_ODBC_VERSION)",
                true
             ); !aCheck) {
            Close_();
            return aCheck;
            }

         iResult = SQLAllocHandle(
            SQL_HANDLE_DBC,
            hEnvironment,
            &hConnection
            );
         if (!SQL_SUCCEEDED(iResult)) {
            auto const aDetails = detail::FormatDiagnostics(
               "SQLAllocHandle(SQL_HANDLE_DBC)",
               iResult,
               detail::CollectDiagnostics(SQL_HANDLE_ENV, hEnvironment)
               );
            Close_();
            return std::unexpected(error_ty{
               {},
               "database not connected",
               aDetails
               });
            }

         if (aCredentials.iTimeout > 0) {
            iResult = SQLSetConnectAttr(
               hConnection,
               SQL_LOGIN_TIMEOUT,
               reinterpret_cast<SQLPOINTER>(static_cast<std::intptr_t>(aCredentials.iTimeout)),
               0
               );
            if (auto aCheck = detail::CheckOdbc(
                   iResult,
                   SQL_HANDLE_DBC,
                   hConnection,
                   "SQLSetConnectAttr(SQL_LOGIN_TIMEOUT)",
                   true
                ); !aCheck) {
               Close_();
               return aCheck;
               }
            }

         std::string strConnection =
            "DRIVER={ODBC Driver 18 for SQL Server};";
         strConnection += "SERVER=" + detail::EscapeConnectionValue(aCredentials.strServer) + ";";
         strConnection += "DATABASE=" + detail::EscapeConnectionValue(aCredentials.strDatabase) + ";";
         strConnection += "MARS_Connection=" + std::string{ aCredentials.boMARS ? "Yes" : "No" } + ";";
         strConnection += "Encrypt=No;";

         if (aCredentials.boIntegrated) {
            strConnection += "Trusted_Connection=Yes;";
            }
         else {
            strConnection += "UID=" + detail::EscapeConnectionValue(aCredentials.strUser) + ";";
            strConnection += "PWD=" + detail::EscapeConnectionValue(aCredentials.strPassword) + ";";
            }

         auto vecConnection = detail::ToOdbcWide(strConnection);
         std::array<SQLWCHAR, 2048> arrCompleted{};
         SQLSMALLINT iCompletedLength{};
         iResult = SQLDriverConnectW(
            hConnection,
            nullptr,
            vecConnection.data(),
            SQL_NTS,
            arrCompleted.data(),
            static_cast<SQLSMALLINT>(arrCompleted.size()),
            &iCompletedLength,
            SQL_DRIVER_NOPROMPT
            );

         if (!SQL_SUCCEEDED(iResult)) {
            auto const aDetails = detail::FormatDiagnostics(
               "SQLDriverConnect",
               iResult,
               detail::CollectDiagnostics(SQL_HANDLE_DBC, hConnection)
               );
            Close_();
            return std::unexpected(error_ty{
               {},
               "database not connected",
               aDetails
               });
            }

         return true;
         }


      framework_result_ty Connect(mssql_credentials const& aCred) {
         aCredentials = aCred;
         return Connect();
         }


      framework_result_ty Connect(
         std::string const& strServer,
         std::string const& strDatabase,
         bool const boIntegrated = true,
         std::string const& strUser = {},
         std::string const& strPassword = {}
      ) {
         aCredentials.strServer = strServer;
         aCredentials.strDatabase = strDatabase;
         aCredentials.boIntegrated = boIntegrated;
         aCredentials.strUser = strUser;
         aCredentials.strPassword = strPassword;
         return Connect();
         }


      bool Connected() const {
         if (hConnection == SQL_NULL_HDBC) {
            return false;
            }

         SQLUINTEGER uDead{ SQL_CD_TRUE };
         SQLRETURN const iResult = SQLGetConnectAttr(
            hConnection,
            SQL_ATTR_CONNECTION_DEAD,
            &uDead,
            0,
            nullptr
            );

         return SQL_SUCCEEDED(iResult) && uDead == SQL_CD_FALSE;
         }


      framework_result_ty BeginTransaction() {
         if (!Connected()) {
            return false;
            }
         if (boTransactionActive) {
            return std::unexpected(error_ty{
               {},
               "Begin Transaction failed",
               "an ODBC transaction is already active"
               });
            }

         SQLRETURN const iResult = SQLSetConnectAttr(
            hConnection,
            SQL_ATTR_AUTOCOMMIT,
            reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_OFF),
            SQL_IS_UINTEGER
            );

         auto aCheck = detail::CheckOdbc(
            iResult,
            SQL_HANDLE_DBC,
            hConnection,
            "SQLSetConnectAttr(SQL_ATTR_AUTOCOMMIT_OFF)",
            true
            );
         if (!aCheck) {
            return aCheck;
            }

         boTransactionActive = true;
         return true;
         }


      framework_result_ty Commit() {
         if (!Connected() || !boTransactionActive) {
            return false;
            }

         SQLRETURN iResult = SQLEndTran(
            SQL_HANDLE_DBC,
            hConnection,
            SQL_COMMIT
            );
         if (auto aCheck = detail::CheckOdbc(
                iResult,
                SQL_HANDLE_DBC,
                hConnection,
                "SQLEndTran(SQL_COMMIT)",
                true
             ); !aCheck) {
            return aCheck;
            }

         iResult = SQLSetConnectAttr(
            hConnection,
            SQL_ATTR_AUTOCOMMIT,
            reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_ON),
            SQL_IS_UINTEGER
            );
         if (auto aCheck = detail::CheckOdbc(
                iResult,
                SQL_HANDLE_DBC,
                hConnection,
                "SQLSetConnectAttr(SQL_ATTR_AUTOCOMMIT_ON)",
                true
             ); !aCheck) {
            return aCheck;
            }

         boTransactionActive = false;
         return true;
         }


      framework_result_ty Rollback() {
         if (!Connected() || !boTransactionActive) {
            return false;
            }

         SQLRETURN iResult = SQLEndTran(
            SQL_HANDLE_DBC,
            hConnection,
            SQL_ROLLBACK
            );
         if (auto aCheck = detail::CheckOdbc(
                iResult,
                SQL_HANDLE_DBC,
                hConnection,
                "SQLEndTran(SQL_ROLLBACK)",
                true
             ); !aCheck) {
            return aCheck;
            }

         iResult = SQLSetConnectAttr(
            hConnection,
            SQL_ATTR_AUTOCOMMIT,
            reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_ON),
            SQL_IS_UINTEGER
            );
         if (auto aCheck = detail::CheckOdbc(
                iResult,
                SQL_HANDLE_DBC,
                hConnection,
                "SQLSetConnectAttr(SQL_ATTR_AUTOCOMMIT_ON)",
                true
             ); !aCheck) {
            return aCheck;
            }

         boTransactionActive = false;
         return true;
         }


      SQLHDBC NativeConnection() const noexcept {
         return hConnection;
         }

   private:
      void ConnectOrThrow_() {
         auto const aResult = Connect();
         if (!aResult) {
            auto const& [info, strMessage, strDetails] = aResult.error();
            (void)info;
            throw database_exception(
               strMessage,
               GetServer(),
               GetInformation(),
               strDetails
               );
            }
         if (!*aResult) {
            throw database_exception(
               "database not connected",
               GetServer(),
               GetInformation(),
               "Connect returned false"
               );
            }
         }


      void Close_() noexcept {
         if (hConnection != SQL_NULL_HDBC) {
            if (boTransactionActive) {
               SQLEndTran(SQL_HANDLE_DBC, hConnection, SQL_ROLLBACK);
               SQLSetConnectAttr(
                  hConnection,
                  SQL_ATTR_AUTOCOMMIT,
                  reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_ON),
                  SQL_IS_UINTEGER
                  );
               }
            SQLDisconnect(hConnection);
            SQLFreeHandle(SQL_HANDLE_DBC, hConnection);
            }

         if (hEnvironment != SQL_NULL_HENV) {
            SQLFreeHandle(SQL_HANDLE_ENV, hEnvironment);
            }

         hConnection = SQL_NULL_HDBC;
         hEnvironment = SQL_NULL_HENV;
         boTransactionActive = false;
         }
      };


   using mssql_database = odbc_database;


   static_assert(adecc::db::framework_database_type<odbc_database>);
   static_assert(adecc::db::framework_database_with_credentials<
      odbc_database,
      mssql_credentials
      >);


   template <adecc::db::framework_database_type db_ty>
   class query {
   public:
      using fw_query_type = SQLHSTMT;

      explicit query(db_ty const& aDb)
         : pDatabase{ &const_cast<db_ty&>(aDb) } {
         if (!pDatabase || !pDatabase->Connected()) {
            throw database_exception(
               "can't create framework query",
               aDb.GetServer(),
               aDb.GetInformation(),
               "No valid database connection"
               );
            }

         AllocateStatementOrThrow_();
         }


      query(query const&) = delete;
      query& operator=(query const&) = delete;


      query(query&& rhs) noexcept
         : pDatabase{ std::exchange(rhs.pDatabase, nullptr) }
         , hStatement{ std::exchange(rhs.hStatement, SQLHSTMT{}) }
         , strCurrentSql{ std::move(rhs.strCurrentSql) }
         , aParameterPlan{ std::move(rhs.aParameterPlan) }
         , strOutputSql{ std::move(rhs.strOutputSql) }
         , aOutputPlan{ std::move(rhs.aOutputPlan) }
         , vecOutputParameters{ std::move(rhs.vecOutputParameters) }
         , optIdentityIndex{ rhs.optIdentityIndex }
         , boExecuted{ rhs.boExecuted }
         , boHasCurrent{ rhs.boHasCurrent }
         , vecColumns{ std::move(rhs.vecColumns) }
         , vecBindStorage{ std::move(rhs.vecBindStorage) }
         , vecParamDescriptors{ std::move(rhs.vecParamDescriptors) } {
         rhs.optIdentityIndex.reset();
         rhs.boExecuted = false;
         rhs.boHasCurrent = false;
         }


      query& operator=(query&& rhs) noexcept {
         if (this != &rhs) {
            FreeStatement_();
            pDatabase = std::exchange(rhs.pDatabase, nullptr);
            hStatement = std::exchange(rhs.hStatement, SQLHSTMT{});
            strCurrentSql = std::move(rhs.strCurrentSql);
            aParameterPlan = std::move(rhs.aParameterPlan);
            strOutputSql = std::move(rhs.strOutputSql);
            aOutputPlan = std::move(rhs.aOutputPlan);
            vecOutputParameters = std::move(rhs.vecOutputParameters);
            optIdentityIndex = rhs.optIdentityIndex;
            boExecuted = rhs.boExecuted;
            boHasCurrent = rhs.boHasCurrent;
            vecColumns = std::move(rhs.vecColumns);
            vecBindStorage = std::move(rhs.vecBindStorage);
            vecParamDescriptors = std::move(rhs.vecParamDescriptors);
            rhs.optIdentityIndex.reset();
            rhs.boExecuted = false;
            rhs.boHasCurrent = false;
            }
         return *this;
         }


      ~query() {
         FreeStatement_();
         }


      framework_result_ty SetSql(std::string const& strSql) {
         strCurrentSql = strSql;
         auto aPlan = detail::BuildParameterPlan(strSql);
         if (!aPlan) {
            return std::unexpected(aPlan.error());
            }
         aParameterPlan = std::move(*aPlan);
         ResetExecutionState_();
         return true;
         }


      framework_result_ty Open(
         std::string const& strSql,
         adecc::db_params const& vecParams
      ) {
         if (auto aSet = SetSql(strSql); !aSet) {
            return aSet;
            }
         return Open(vecParams);
         }


      framework_result_ty Open(adecc::db_params const& vecParams) {
         return ExecutePrepared_(aParameterPlan, vecParams, true);
         }


      framework_output_ty ExecuteCommand(std::string const& strSql) {
         if (auto aSet = SetSql(strSql); !aSet) {
            return std::unexpected(aSet.error());
            }

         if (!aParameterPlan.vecOccurrences.empty()) {
            return std::unexpected(error_ty{
               {},
               "error for execute command",
               "ExecuteCommand does not accept SQL parameters"
               });
            }

         ResetStatement_();
         auto vecSql = detail::ToOdbcWide(aParameterPlan.strSql);
         SQLRETURN iResult = SQLExecDirectW(
            hStatement,
            vecSql.data(),
            SQL_NTS
            );
         if (auto aCheck = detail::CheckOdbc(
                iResult,
                SQL_HANDLE_STMT,
                hStatement,
                "SQLExecDirect",
                true
             ); !aCheck) {
            return std::unexpected(aCheck.error());
            }

         SQLLEN iRows{};
         iResult = SQLRowCount(hStatement, &iRows);
         if (auto aCheck = detail::CheckOdbc(
                iResult,
                SQL_HANDLE_STMT,
                hStatement,
                "SQLRowCount",
                true
             ); !aCheck) {
            return std::unexpected(aCheck.error());
            }

         return framework_output_result{
            static_cast<std::int64_t>(iRows),
            std::nullopt
            };
         }


      framework_result_ty PrepareOutput(
         std::string const& strSql,
         db_output_parameters const& vecOutputParams
      ) {
         strOutputSql = strSql;
         vecOutputParameters = vecOutputParams;
         optIdentityIndex.reset();

         for (std::size_t uIndex{}; uIndex < vecOutputParameters.size(); ++uIndex) {
            auto const& [strName, aRole] = vecOutputParameters[uIndex];
            if (aRole == db_output_param_role::identity) {
               if (optIdentityIndex) {
                  return std::unexpected(error_ty{
                     {},
                     "error for prepare SQL Server output query",
                     "more than one identity output parameter is defined"
                     });
                  }
               optIdentityIndex = uIndex;
               }
            }

         std::string const strPhysical = optIdentityIndex
            ? detail::AppendScopeIdentity(strOutputSql)
            : strOutputSql;

         auto aPlan = detail::BuildParameterPlan(strPhysical);
         if (!aPlan) {
            return std::unexpected(aPlan.error());
            }
         aOutputPlan = std::move(*aPlan);

         for (auto const& [strName, aRole] : vecOutputParameters) {
            switch (aRole) {
            case db_output_param_role::needed_value:
            case db_output_param_role::key:
               if (!detail::ContainsParameter(aOutputPlan, strName)) {
                  return std::unexpected(error_ty{
                     {},
                     "error for prepare SQL Server output query",
                     std::format("required output parameter '{}' is not used in SQL", strName)
                     });
                  }
               break;

            case db_output_param_role::identity:
               if (detail::ContainsParameter(aOutputPlan, strName)) {
                  return std::unexpected(error_ty{
                     {},
                     "error for prepare SQL Server output query",
                     std::format(
                        "identity output parameter '{}' must not be used as SQL input parameter",
                        strName
                        )
                     });
                  }
               break;

            case db_output_param_role::may_be_missing:
               break;
               }
            }

         return true;
         }


      template <class... Args>
         requires (adecc::db_result_type<std::remove_cvref_t<Args>> && ...)
      framework_output_ty ExecuteOutput(std::tuple<Args...> const& tupValues) {
         if (vecOutputParameters.size() != sizeof...(Args)) {
            return std::unexpected(error_ty{
               {},
               "error for execute SQL Server output query",
               std::format(
                  "output parameter count {} does not match tuple size {}",
                  vecOutputParameters.size(),
                  sizeof...(Args)
                  )
               });
            }

         adecc::db_params vecParams;
         vecParams.reserve(sizeof...(Args));
         if (auto aBuild = BuildOutputParams_(tupValues, vecParams); !aBuild) {
            return std::unexpected(aBuild.error());
            }

         auto aExecute = ExecutePrepared_(aOutputPlan, vecParams, false);
         if (!aExecute) {
            return std::unexpected(aExecute.error());
            }

         SQLLEN iRows{};
         SQLRETURN iResult = SQLRowCount(hStatement, &iRows);
         if (auto aCheck = detail::CheckOdbc(
                iResult,
                SQL_HANDLE_STMT,
                hStatement,
                "SQLRowCount after output execution",
                true
             ); !aCheck) {
            return std::unexpected(aCheck.error());
            }

         std::optional<db_value> optIdentity;

         if (optIdentityIndex) {
            bool boFoundIdentityResult{ false };

            for (;;) {
               SQLSMALLINT iColumns{};
               iResult = SQLNumResultCols(hStatement, &iColumns);
               if (auto aCheck = detail::CheckOdbc(
                      iResult,
                      SQL_HANDLE_STMT,
                      hStatement,
                      "SQLNumResultCols while reading identity",
                      true
                   ); !aCheck) {
                  return std::unexpected(aCheck.error());
                  }

               bool boIdentityResult{ false };
               if (iColumns == 1) {
                  std::array<SQLWCHAR, 128> arrColumnName{};
                  SQLSMALLINT iColumnNameLength{};
                  SQLSMALLINT iType{};
                  SQLULEN uPrecision{};
                  SQLSMALLINT iScale{};
                  SQLSMALLINT iNullable{};
                  iResult = SQLDescribeColW(
                     hStatement,
                     1,
                     arrColumnName.data(),
                     static_cast<SQLSMALLINT>(arrColumnName.size()),
                     &iColumnNameLength,
                     &iType,
                     &uPrecision,
                     &iScale,
                     &iNullable
                     );
                  if (auto aCheck = detail::CheckOdbc(
                         iResult,
                         SQL_HANDLE_STMT,
                         hStatement,
                         "SQLDescribeCol(identity result)",
                         true
                      ); !aCheck) {
                     return std::unexpected(aCheck.error());
                     }
                  auto const strColumnName = detail::FromOdbcWide(
                     arrColumnName.data(),
                     static_cast<std::size_t>(std::max<SQLSMALLINT>(0, iColumnNameLength))
                     );
                  boIdentityResult = detail::EqualName(strColumnName, "__adecc_identity");
                  }

               if (boIdentityResult) {
                  boFoundIdentityResult = true;
                  iResult = SQLFetch(hStatement);
                  if (iResult == SQL_NO_DATA) {
                     return std::unexpected(error_ty{
                        {},
                        "error for read SQL Server output identity",
                        "SCOPE_IDENTITY() returned no row"
                        });
                     }
                  if (auto aCheck = detail::CheckOdbc(
                         iResult,
                         SQL_HANDLE_STMT,
                         hStatement,
                         "SQLFetch while reading identity",
                         true
                      ); !aCheck) {
                     return std::unexpected(aCheck.error());
                     }

                  std::int64_t iIdentity{};
                  SQLLEN iIndicator{};
                  iResult = SQLGetData(
                     hStatement,
                     1,
                     SQL_C_SBIGINT,
                     &iIdentity,
                     sizeof(iIdentity),
                     &iIndicator
                     );
                  if (auto aCheck = detail::CheckOdbc(
                         iResult,
                         SQL_HANDLE_STMT,
                         hStatement,
                         "SQLGetData(SCOPE_IDENTITY)",
                         true
                      ); !aCheck) {
                     return std::unexpected(aCheck.error());
                     }

                  if (iIndicator == SQL_NULL_DATA) {
                     return std::unexpected(error_ty{
                        {},
                        "error for read SQL Server output identity",
                        "SCOPE_IDENTITY() returned NULL; no IDENTITY value was generated in this scope"
                        });
                     }

                  auto aIdentity = ConvertIdentity_<std::tuple<Args...>>(iIdentity);
                  if (!aIdentity) {
                     return std::unexpected(aIdentity.error());
                     }
                  optIdentity = std::move(*aIdentity);
                  break;
                  }

               iResult = SQLMoreResults(hStatement);
               if (iResult == SQL_NO_DATA) {
                  break;
                  }
               if (auto aCheck = detail::CheckOdbc(
                      iResult,
                      SQL_HANDLE_STMT,
                      hStatement,
                      "SQLMoreResults while reading identity",
                      true
                   ); !aCheck) {
                  return std::unexpected(aCheck.error());
                  }
               }

            if (!boFoundIdentityResult || !optIdentity) {
               return std::unexpected(error_ty{
                  {},
                  "error for read SQL Server output identity",
                  "no result set containing SCOPE_IDENTITY() was returned"
                  });
               }
            }

         return framework_output_result{
            static_cast<std::int64_t>(iRows),
            std::move(optIdentity)
            };
         }


      framework_result_ty First() {
         if (!boExecuted) {
            return false;
            }

         SQLRETURN const iResult = SQLFetch(hStatement);
         if (iResult == SQL_NO_DATA) {
            boHasCurrent = false;
            return false;
            }

         if (auto aCheck = detail::CheckOdbc(
                iResult,
                SQL_HANDLE_STMT,
                hStatement,
                "SQLFetch(first row)",
                true
             ); !aCheck) {
            boHasCurrent = false;
            return aCheck;
            }

         boHasCurrent = true;
         return true;
         }


      framework_result_ty Fetch() {
         if (!boExecuted || !boHasCurrent) {
            return false;
            }

         SQLRETURN const iResult = SQLFetch(hStatement);
         if (iResult == SQL_NO_DATA) {
            boHasCurrent = false;
            return false;
            }

         if (auto aCheck = detail::CheckOdbc(
                iResult,
                SQL_HANDLE_STMT,
                hStatement,
                "SQLFetch(next row)",
                true
             ); !aCheck) {
            boHasCurrent = false;
            return aCheck;
            }

         return true;
         }


      framework_result_ty Eof() const {
         return boExecuted && !boHasCurrent;
         }


      template <adecc::db_result_type ty>
      framework_get_ty<ty> GetField(std::string const& strField) const {
         if (!boExecuted || !boHasCurrent) {
            return std::unexpected(error_ty{
               {},
               std::format("error for get the field '{}'", strField),
               "query has no current row"
               });
            }

         auto const optColumn = FindColumn_(strField);
         if (!optColumn) {
            return std::unexpected(error_ty{
               {},
               std::format("error for get the field '{}'", strField),
               "field not found in SQL Server result set"
               });
            }

         try {
            return ReadField_<ty>(*optColumn);
            }
         catch (std::exception const& ex) {
            auto const& aColumn = vecColumns[*optColumn - 1];
            return std::unexpected(error_ty{
               {},
               std::format("error for get the field '{}'", strField),
               std::format(
                  "{}\nColumn={}\nNativeType={}\nODBCType={}\nPrecision={}\nScale={}\nNullable={}",
                  ex.what(),
                  aColumn.strName,
                  aColumn.strNativeTypeName,
                  aColumn.iSqlType,
                  aColumn.uPrecision,
                  aColumn.iScale,
                  aColumn.iNullable
                  )
               });
            }
         }


      std::vector<std::string> GetAttributes() const {
         std::vector<std::string> vecResult;
         vecResult.reserve(vecColumns.size());
         for (auto const& aColumn : vecColumns) {
            vecResult.push_back(aColumn.strName);
            }
         return vecResult;
         }


      SQLHSTMT NativeHandle() const noexcept {
         return hStatement;
         }

   private:
      struct bound_value {
         std::variant<
            std::monostate,
            SQLINTEGER,
            std::int64_t,
            SQLUINTEGER,
            std::uint64_t,
            double,
            SQLCHAR,
            std::string,
            std::vector<SQLWCHAR>,
            SQL_DATE_STRUCT,
            SQL_TIMESTAMP_STRUCT,
            SQL_TIME_STRUCT
         > aValue{};
         SQLLEN iIndicator{};
         };


      void AllocateStatementOrThrow_() {
         SQLRETURN const iResult = SQLAllocHandle(
            SQL_HANDLE_STMT,
            pDatabase->NativeConnection(),
            &hStatement
            );

         if (!SQL_SUCCEEDED(iResult)) {
            throw database_exception(
               "can't create framework query",
               pDatabase->GetServer(),
               pDatabase->GetInformation(),
               detail::FormatDiagnostics(
                  "SQLAllocHandle(SQL_HANDLE_STMT)",
                  iResult,
                  detail::CollectDiagnostics(
                     SQL_HANDLE_DBC,
                     pDatabase->NativeConnection()
                     )
                  )
               );
            }
         }


      void FreeStatement_() noexcept {
         if (hStatement != SQL_NULL_HSTMT) {
            SQLFreeHandle(SQL_HANDLE_STMT, hStatement);
            hStatement = SQL_NULL_HSTMT;
            }
         }


      void ResetStatement_() {
         if (hStatement == SQL_NULL_HSTMT) {
            AllocateStatementOrThrow_();
            return;
            }

         SQLFreeStmt(hStatement, SQL_CLOSE);
         SQLFreeStmt(hStatement, SQL_UNBIND);
         SQLFreeStmt(hStatement, SQL_RESET_PARAMS);
         vecBindStorage.clear();
         vecParamDescriptors.clear();
         vecColumns.clear();
         boExecuted = false;
         boHasCurrent = false;
         }


      void ResetExecutionState_() {
         if (hStatement != SQL_NULL_HSTMT) {
            SQLFreeStmt(hStatement, SQL_CLOSE);
            SQLFreeStmt(hStatement, SQL_UNBIND);
            SQLFreeStmt(hStatement, SQL_RESET_PARAMS);
            }
         vecBindStorage.clear();
         vecParamDescriptors.clear();
         vecColumns.clear();
         boExecuted = false;
         boHasCurrent = false;
         }


      static adecc::db_param2 const* FindDbParam_(
         adecc::db_params const& vecParams,
         std::string_view const svName
      ) {
         auto const it = std::ranges::find_if(
            vecParams,
            [svName](adecc::db_param2 const& aParam) {
               return detail::EqualName(std::get<0>(aParam), svName);
               }
            );
         return it == vecParams.end() ? nullptr : std::addressof(*it);
         }


      framework_result_ty ValidateParameters_(
         detail::parameter_plan const& aPlan,
         adecc::db_params const& vecParams
      ) const {
         for (auto const& strName : aPlan.vecLogicalNames) {
            if (!FindDbParam_(vecParams, strName)) {
               return std::unexpected(error_ty{
                  {},
                  "error for bind SQL Server parameter",
                  std::format("SQL parameter '{}' has no supplied value", strName)
                  });
               }
            }

         for (auto const& [strName, aValue, boRequired] : vecParams) {
            (void)aValue;
            if (boRequired && !detail::ContainsParameter(aPlan, strName)) {
               return std::unexpected(error_ty{
                  {},
                  "error for bind SQL Server parameter",
                  std::format("required parameter '{}' is not used in SQL", strName)
                  });
               }
            }

         return true;
         }


      static error_ty WithPlan_(error_ty aError,
                                detail::parameter_plan const& aPlan) {
         auto& strDetails = std::get<2>(aError);
         strDetails += "\nPhysical ODBC parameter map:\n";

         for (auto const& aOccurrence : aPlan.vecOccurrences) {
            strDetails += std::format(
               "  ?{} <- :{}\n",
               aOccurrence.uPhysicalIndex + 1,
               aPlan.vecLogicalNames[aOccurrence.uLogicalIndex]
               );
            }

         if (aPlan.vecOccurrences.empty()) {
            strDetails += "  <no parameters>\n";
            }

         return aError;
         }


      framework_result_ty ExecutePrepared_(
         detail::parameter_plan const& aPlan,
         adecc::db_params const& vecParams,
         bool const boLoadColumns
      ) {
         if (auto aValidation = ValidateParameters_(aPlan, vecParams); !aValidation) {
            return aValidation;
            }

         ResetStatement_();

         auto vecSql = detail::ToOdbcWide(aPlan.strSql);
         SQLRETURN iResult = SQLPrepareW(
            hStatement,
            vecSql.data(),
            SQL_NTS
            );
         if (auto aCheck = detail::CheckOdbc(
                iResult,
                SQL_HANDLE_STMT,
                hStatement,
                "SQLPrepare",
                true
             ); !aCheck) {
            return std::unexpected(WithPlan_(aCheck.error(), aPlan));
            }

         vecParamDescriptors.clear();
         vecParamDescriptors.reserve(aPlan.vecOccurrences.size());
         for (SQLUSMALLINT uPosition{ 1 };
              uPosition <= static_cast<SQLUSMALLINT>(aPlan.vecOccurrences.size());
              ++uPosition) {
            detail::parameter_descriptor aDescriptor{};
            iResult = SQLDescribeParam(
               hStatement,
               uPosition,
               &aDescriptor.iSqlType,
               &aDescriptor.uPrecision,
               &aDescriptor.iScale,
               &aDescriptor.iNullable
               );
            if (auto aCheck = detail::CheckOdbc(
                   iResult,
                   SQL_HANDLE_STMT,
                   hStatement,
                   std::format("SQLDescribeParam({})", uPosition),
                   true
                ); !aCheck) {
               return std::unexpected(WithPlan_(aCheck.error(), aPlan));
               }
            vecParamDescriptors.push_back(aDescriptor);
            }

         vecBindStorage.resize(aPlan.vecOccurrences.size());

         for (auto const& aOccurrence : aPlan.vecOccurrences) {
            auto const& strName = aPlan.vecLogicalNames[aOccurrence.uLogicalIndex];
            auto const* pParam = FindDbParam_(vecParams, strName);
            if (!pParam) {
               return std::unexpected(error_ty{
                  {},
                  "error for bind SQL Server parameter",
                  std::format("parameter '{}' disappeared during binding", strName)
                  });
               }

            auto aBind = BindParameter_(
               static_cast<SQLUSMALLINT>(aOccurrence.uPhysicalIndex + 1),
               std::get<1>(*pParam),
               vecBindStorage[aOccurrence.uPhysicalIndex],
               vecParamDescriptors[aOccurrence.uPhysicalIndex]
               );
            if (!aBind) {
               return std::unexpected(WithPlan_(aBind.error(), aPlan));
               }
            }

         iResult = SQLExecute(hStatement);
         if (auto aCheck = detail::CheckOdbc(
                iResult,
                SQL_HANDLE_STMT,
                hStatement,
                "SQLExecute",
                true
             ); !aCheck) {
            return std::unexpected(WithPlan_(aCheck.error(), aPlan));
            }

         boExecuted = true;
         boHasCurrent = false;

         if (boLoadColumns) {
            if (auto aColumns = LoadColumns_(); !aColumns) {
               return aColumns;
               }
            }

         return true;
         }


      framework_result_ty BindParameter_(
         SQLUSMALLINT const uPosition,
         db_param const& aParam,
         bound_value& aStorage,
         detail::parameter_descriptor const& aDescriptor
      ) {
         framework_result_ty aResult{ true };

         auto fnBind = [&]<class ty>(ty const& aValue) {
            if (!aResult) {
               return;
               }
            aResult = BindValue_(uPosition, aValue, aStorage, aDescriptor);
            };

         std::visit(fnBind, aParam);
         return aResult;
         }


      template <class ty>
      framework_result_ty BindValue_(
         SQLUSMALLINT const uPosition,
         std::optional<ty> const& optValue,
         bound_value& aStorage,
         detail::parameter_descriptor const& aDescriptor
      ) {
         if (!optValue) {
            aStorage.aValue = std::monostate{};
            aStorage.iIndicator = SQL_NULL_DATA;
            SQLRETURN const iResult = SQLBindParameter(
               hStatement,
               uPosition,
               SQL_PARAM_INPUT,
               SQL_C_CHAR,
               aDescriptor.iSqlType,
               std::max<SQLULEN>(1, aDescriptor.uPrecision),
               aDescriptor.iScale,
               nullptr,
               0,
               &aStorage.iIndicator
               );
            return detail::CheckOdbc(
               iResult,
               SQL_HANDLE_STMT,
               hStatement,
               std::format("SQLBindParameter({}) NULL", uPosition),
               true
               );
            }
         return BindValue_(uPosition, *optValue, aStorage, aDescriptor);
         }


      framework_result_ty BindValue_(SQLUSMALLINT const uPosition,
                                     std::string_view const svValue,
                                     bound_value& aStorage,
                                     detail::parameter_descriptor const& aDescriptor) {
         aStorage.aValue = std::string{ svValue };
         return BindString_(uPosition, aStorage, aDescriptor);
         }


      framework_result_ty BindValue_(SQLUSMALLINT const uPosition,
                                     char const* const szValue,
                                     bound_value& aStorage,
                                     detail::parameter_descriptor const& aDescriptor) {
         if (!szValue) {
            return BindValue_(uPosition, std::optional<std::string>{}, aStorage, aDescriptor);
            }
         aStorage.aValue = std::string{ szValue };
         return BindString_(uPosition, aStorage, aDescriptor);
         }


      framework_result_ty BindValue_(SQLUSMALLINT const uPosition,
                                     std::string const& strValue,
                                     bound_value& aStorage,
                                     detail::parameter_descriptor const& aDescriptor) {
         aStorage.aValue = strValue;
         return BindString_(uPosition, aStorage, aDescriptor);
         }


      framework_result_ty BindString_(SQLUSMALLINT const uPosition,
                                      bound_value& aStorage,
                                     detail::parameter_descriptor const& aDescriptor) {
         auto const strValue = std::get<std::string>(aStorage.aValue);
         aStorage.aValue = detail::ToOdbcWide(strValue);
         auto& vecValue = std::get<std::vector<SQLWCHAR>>(aStorage.aValue);
         std::size_t const uCharacters = vecValue.empty() ? 0 : vecValue.size() - 1;
         aStorage.iIndicator = static_cast<SQLLEN>(uCharacters * sizeof(SQLWCHAR));
         SQLRETURN const iResult = SQLBindParameter(
            hStatement,
            uPosition,
            SQL_PARAM_INPUT,
            SQL_C_WCHAR,
            aDescriptor.iSqlType,
            aDescriptor.uPrecision > 0
               ? aDescriptor.uPrecision
               : std::max<SQLULEN>(1, static_cast<SQLULEN>(uCharacters)),
            aDescriptor.iScale,
            vecValue.data(),
            static_cast<SQLLEN>(vecValue.size() * sizeof(SQLWCHAR)),
            &aStorage.iIndicator
            );
         return detail::CheckOdbc(
            iResult,
            SQL_HANDLE_STMT,
            hStatement,
            std::format("SQLBindParameter({}) string", uPosition),
            true
            );
         }


      template <class value_ty>
      framework_result_ty BindScalar_(
         SQLUSMALLINT const uPosition,
         value_ty const& aValue,
         bound_value& aStorage,
         SQLSMALLINT const iCType,
         SQLSMALLINT const iFallbackSqlType,
         detail::parameter_descriptor const& aDescriptor
      ) {
         aStorage.aValue = aValue;
         aStorage.iIndicator = 0;
         auto* pValue = std::addressof(std::get<value_ty>(aStorage.aValue));
         SQLRETURN const iResult = SQLBindParameter(
            hStatement,
            uPosition,
            SQL_PARAM_INPUT,
            iCType,
            aDescriptor.iSqlType != SQL_UNKNOWN_TYPE
               ? aDescriptor.iSqlType
               : iFallbackSqlType,
            aDescriptor.uPrecision,
            aDescriptor.iScale,
            pValue,
            sizeof(value_ty),
            &aStorage.iIndicator
            );
         return detail::CheckOdbc(
            iResult,
            SQL_HANDLE_STMT,
            hStatement,
            std::format("SQLBindParameter({}) scalar", uPosition),
            true
            );
         }


      framework_result_ty BindValue_(SQLUSMALLINT const uPosition,
                                     int const iValue,
                                     bound_value& aStorage,
                                     detail::parameter_descriptor const& aDescriptor) {
         return BindScalar_(uPosition, static_cast<SQLINTEGER>(iValue), aStorage,
                            SQL_C_SLONG, SQL_INTEGER, aDescriptor);
         }


      framework_result_ty BindValue_(SQLUSMALLINT const uPosition,
                                     long long const iValue,
                                     bound_value& aStorage,
                                     detail::parameter_descriptor const& aDescriptor) {
         return BindScalar_(uPosition, static_cast<std::int64_t>(iValue), aStorage,
                            SQL_C_SBIGINT, SQL_BIGINT, aDescriptor);
         }


      framework_result_ty BindValue_(SQLUSMALLINT const uPosition,
                                     unsigned int const uValue,
                                     bound_value& aStorage,
                                     detail::parameter_descriptor const& aDescriptor) {
         return BindScalar_(uPosition, static_cast<SQLUINTEGER>(uValue), aStorage,
                            SQL_C_ULONG, SQL_INTEGER, aDescriptor);
         }


      framework_result_ty BindValue_(SQLUSMALLINT const uPosition,
                                     unsigned long long const uValue,
                                     bound_value& aStorage,
                                     detail::parameter_descriptor const& aDescriptor) {
         return BindScalar_(uPosition, static_cast<std::uint64_t>(uValue), aStorage,
                            SQL_C_UBIGINT, SQL_NUMERIC, aDescriptor);
         }


      framework_result_ty BindValue_(SQLUSMALLINT const uPosition,
                                     double const flValue,
                                     bound_value& aStorage,
                                     detail::parameter_descriptor const& aDescriptor) {
         return BindScalar_(uPosition, flValue, aStorage, SQL_C_DOUBLE, SQL_DOUBLE, aDescriptor);
         }


      framework_result_ty BindValue_(SQLUSMALLINT const uPosition,
                                     bool const boValue,
                                     bound_value& aStorage,
                                     detail::parameter_descriptor const& aDescriptor) {
         return BindScalar_(uPosition, static_cast<SQLCHAR>(boValue ? 1 : 0), aStorage,
                            SQL_C_BIT, SQL_BIT, aDescriptor);
         }


      framework_result_ty BindValue_(SQLUSMALLINT const uPosition,
                                     money_ty const& aValue,
                                     bound_value& aStorage,
                                     detail::parameter_descriptor const& aDescriptor) {
         double const flValue = static_cast<double>(aValue.Get());
         return BindScalar_(
            uPosition,
            flValue,
            aStorage,
            SQL_C_DOUBLE,
            SQL_DECIMAL,
            aDescriptor
            );
         }


      framework_result_ty BindValue_(SQLUSMALLINT const uPosition,
                                     date_ty const& aValue,
                                     bound_value& aStorage,
                                     detail::parameter_descriptor const& aDescriptor) {
         aStorage.aValue = detail::FromDate(aValue);
         return BindScalar_(uPosition, std::get<SQL_DATE_STRUCT>(aStorage.aValue), aStorage,
                            SQL_C_TYPE_DATE, SQL_TYPE_DATE, aDescriptor);
         }


      framework_result_ty BindValue_(SQLUSMALLINT const uPosition,
                                     timestamp_ty const& aValue,
                                     bound_value& aStorage,
                                     detail::parameter_descriptor const& aDescriptor) {
         aStorage.aValue = detail::FromTimestamp(aValue);
         return BindScalar_(uPosition, std::get<SQL_TIMESTAMP_STRUCT>(aStorage.aValue), aStorage,
                            SQL_C_TYPE_TIMESTAMP, SQL_TYPE_TIMESTAMP, aDescriptor);
         }


      framework_result_ty BindValue_(SQLUSMALLINT const uPosition,
                                     time_ty const& aValue,
                                     bound_value& aStorage,
                                     detail::parameter_descriptor const& aDescriptor) {
         aStorage.aValue = detail::FromTime(aValue);
         return BindScalar_(uPosition, std::get<SQL_TIME_STRUCT>(aStorage.aValue), aStorage,
                            SQL_C_TYPE_TIME, SQL_TYPE_TIME, aDescriptor);
         }


      framework_result_ty LoadColumns_() {
         vecColumns.clear();

         SQLSMALLINT iColumns{};
         SQLRETURN iResult = SQLNumResultCols(hStatement, &iColumns);
         if (auto aCheck = detail::CheckOdbc(
                iResult,
                SQL_HANDLE_STMT,
                hStatement,
                "SQLNumResultCols",
                true
             ); !aCheck) {
            return aCheck;
            }

         vecColumns.reserve(static_cast<std::size_t>(iColumns));

         for (SQLUSMALLINT uColumn{ 1 }; uColumn <= static_cast<SQLUSMALLINT>(iColumns); ++uColumn) {
            std::array<SQLWCHAR, 512> arrName{};
            SQLSMALLINT iNameLength{};
            SQLSMALLINT iType{};
            SQLULEN uPrecision{};
            SQLSMALLINT iScale{};
            SQLSMALLINT iNullable{};

            iResult = SQLDescribeColW(
               hStatement,
               uColumn,
               arrName.data(),
               static_cast<SQLSMALLINT>(arrName.size()),
               &iNameLength,
               &iType,
               &uPrecision,
               &iScale,
               &iNullable
               );
            if (auto aCheck = detail::CheckOdbc(
                   iResult,
                   SQL_HANDLE_STMT,
                   hStatement,
                   std::format("SQLDescribeCol({})", uColumn),
                   true
                ); !aCheck) {
               return aCheck;
               }

            std::array<SQLWCHAR, 256> arrTypeName{};
            SQLSMALLINT iTypeNameLength{};
            iResult = SQLColAttributeW(
               hStatement,
               uColumn,
               SQL_DESC_TYPE_NAME,
               arrTypeName.data(),
               static_cast<SQLSMALLINT>(arrTypeName.size()),
               &iTypeNameLength,
               nullptr
               );
            if (auto aCheck = detail::CheckOdbc(
                   iResult,
                   SQL_HANDLE_STMT,
                   hStatement,
                   std::format("SQLColAttribute(SQL_DESC_TYPE_NAME,{})", uColumn),
                   true
                ); !aCheck) {
               return aCheck;
               }

            vecColumns.push_back(detail::column_descriptor{
               .strName = detail::FromOdbcWide(
                  arrName.data(),
                  static_cast<std::size_t>(std::max<SQLSMALLINT>(0, iNameLength))
                  ),
               .strNativeTypeName = detail::FromOdbcWide(
                  arrTypeName.data(),
                  static_cast<std::size_t>(std::max<SQLSMALLINT>(0, iTypeNameLength))
                  ),
               .iSqlType = iType,
               .uPrecision = uPrecision,
               .iScale = iScale,
               .iNullable = iNullable
               });
            }

         return true;
         }


      std::optional<SQLUSMALLINT> FindColumn_(std::string_view const svField) const {
         for (std::size_t uIndex{}; uIndex < vecColumns.size(); ++uIndex) {
            if (detail::EqualName(vecColumns[uIndex].strName, svField)) {
               return static_cast<SQLUSMALLINT>(uIndex + 1);
               }
            }
         return std::nullopt;
         }


      template <class ty>
      framework_get_ty<ty> ReadField_(SQLUSMALLINT const uColumn) const {
         using clean_ty = std::remove_cvref_t<ty>;

         if constexpr (std::same_as<clean_ty, std::string>) {
            return ReadString_(uColumn);
            }
         else if constexpr (std::same_as<clean_ty, double>) {
            return ReadScalar_<double, double>(uColumn, SQL_C_DOUBLE);
            }
         else if constexpr (std::same_as<clean_ty, int>) {
            return ReadScalar_<int, SQLINTEGER>(uColumn, SQL_C_SLONG);
            }
         else if constexpr (std::same_as<clean_ty, long long>) {
            return ReadScalar_<long long, std::int64_t>(uColumn, SQL_C_SBIGINT);
            }
         else if constexpr (std::same_as<clean_ty, bool>) {
            auto aValue = ReadScalar_<unsigned int, SQLCHAR>(uColumn, SQL_C_BIT);
            if (!aValue) {
               return std::unexpected(aValue.error());
               }
            if (!*aValue) {
               return std::optional<bool>{};
               }
            return std::optional<bool>{ **aValue != 0 };
            }
         else if constexpr (std::same_as<clean_ty, unsigned int>) {
            return ReadScalar_<unsigned int, SQLUINTEGER>(uColumn, SQL_C_ULONG);
            }
         else if constexpr (std::same_as<clean_ty, unsigned long long>) {
            return ReadScalar_<unsigned long long, std::uint64_t>(uColumn, SQL_C_UBIGINT);
            }
         else if constexpr (std::same_as<clean_ty, money_ty>) {
            return ReadMoney_(uColumn);
            }
         else if constexpr (std::same_as<clean_ty, date_ty>) {
            SQL_DATE_STRUCT aValue{};
            auto aRead = ReadNative_(uColumn, SQL_C_TYPE_DATE, aValue);
            if (!aRead) {
               return std::unexpected(aRead.error());
               }
            if (!*aRead) {
               return std::optional<date_ty>{};
               }
            return std::optional<date_ty>{ detail::ToDate(aValue) };
            }
         else if constexpr (std::same_as<clean_ty, timestamp_ty>) {
            SQL_TIMESTAMP_STRUCT aValue{};
            auto aRead = ReadNative_(uColumn, SQL_C_TYPE_TIMESTAMP, aValue);
            if (!aRead) {
               return std::unexpected(aRead.error());
               }
            if (!*aRead) {
               return std::optional<timestamp_ty>{};
               }
            return std::optional<timestamp_ty>{ detail::ToTimestamp(aValue) };
            }
         else if constexpr (std::same_as<clean_ty, time_ty>) {
            SQL_TIME_STRUCT aValue{};
            auto aRead = ReadNative_(uColumn, SQL_C_TYPE_TIME, aValue);
            if (!aRead) {
               return std::unexpected(aRead.error());
               }
            if (!*aRead) {
               return std::optional<time_ty>{};
               }
            return std::optional<time_ty>{ detail::ToTime(aValue) };
            }
         else {
            static_assert(std::same_as<clean_ty, void>, "unsupported SQL Server result type");
            }
         }


      template <class result_ty, class native_ty>
      framework_get_ty<result_ty> ReadScalar_(
         SQLUSMALLINT const uColumn,
         SQLSMALLINT const iCType
      ) const {
         native_ty aValue{};
         auto aRead = ReadNative_(uColumn, iCType, aValue);
         if (!aRead) {
            return std::unexpected(aRead.error());
            }
         if (!*aRead) {
            return std::optional<result_ty>{};
            }
         return std::optional<result_ty>{ static_cast<result_ty>(aValue) };
         }


      template <class native_ty>
      std::expected<bool, error_ty> ReadNative_(
         SQLUSMALLINT const uColumn,
         SQLSMALLINT const iCType,
         native_ty& aValue
      ) const {
         SQLLEN iIndicator{};
         SQLRETURN const iResult = SQLGetData(
            hStatement,
            uColumn,
            iCType,
            &aValue,
            sizeof(aValue),
            &iIndicator
            );

         if (iResult == SQL_NO_DATA) {
            return std::unexpected(error_ty{
               {},
               "error for read SQL Server field",
               "SQLGetData returned SQL_NO_DATA for an existing current row"
               });
            }

         if (auto aCheck = detail::CheckOdbc(
                iResult,
                SQL_HANDLE_STMT,
                hStatement,
                std::format("SQLGetData(column {})", uColumn),
                true
             ); !aCheck) {
            return std::unexpected(aCheck.error());
            }

         if (iIndicator == SQL_NULL_DATA) {
            return false;
            }

         return true;
         }


      framework_get_ty<std::string> ReadString_(SQLUSMALLINT const uColumn) const {
         std::u16string strUtf16;
         std::array<SQLWCHAR, 2048> arrBuffer{};

         for (;;) {
            arrBuffer.fill(SQLWCHAR{});
            SQLLEN iIndicator{};
            SQLRETURN const iResult = SQLGetData(
               hStatement,
               uColumn,
               SQL_C_WCHAR,
               arrBuffer.data(),
               static_cast<SQLLEN>(arrBuffer.size() * sizeof(SQLWCHAR)),
               &iIndicator
               );

            if (iResult == SQL_NO_DATA) {
               break;
               }

            if (iIndicator == SQL_NULL_DATA) {
               return std::optional<std::string>{};
               }

            if (iResult != SQL_SUCCESS && iResult != SQL_SUCCESS_WITH_INFO) {
               auto const vecDiagnostics = detail::CollectDiagnostics(
                  SQL_HANDLE_STMT,
                  hStatement
                  );
               return std::unexpected(error_ty{
                  {},
                  "error for read SQL Server string field",
                  detail::FormatDiagnostics("SQLGetData(string)", iResult, vecDiagnostics)
                  });
               }

            std::size_t uCharacters{};
            while (uCharacters < arrBuffer.size() && arrBuffer[uCharacters] != SQLWCHAR{}) {
               ++uCharacters;
               }
            for (std::size_t uIndex{}; uIndex < uCharacters; ++uIndex) {
               strUtf16.push_back(static_cast<char16_t>(arrBuffer[uIndex]));
               }

            if (iResult == SQL_SUCCESS) {
               break;
               }

            auto const vecDiagnostics = detail::CollectDiagnostics(
               SQL_HANDLE_STMT,
               hStatement
               );
            bool const boOnlyTruncation = !vecDiagnostics.empty() &&
               std::ranges::all_of(vecDiagnostics, [](detail::diagnostic_record const& aDiag) {
                  return aDiag.strSqlState == "01004";
                  });

            if (!boOnlyTruncation) {
               return std::unexpected(error_ty{
                  {},
                  "warning while read SQL Server string field",
                  detail::FormatDiagnostics("SQLGetData(string)", iResult, vecDiagnostics)
                  });
               }
            }

         try {
            return std::optional<std::string>{ detail::Utf16ToUtf8(strUtf16) };
            }
         catch (std::exception const& ex) {
            return std::unexpected(error_ty{
               {},
               "error for convert SQL Server Unicode string to UTF-8",
               ex.what()
               });
            }
         }


      framework_get_ty<money_ty> ReadMoney_(SQLUSMALLINT const uColumn) const {
         double flValue{};
         auto aRead = ReadNative_(uColumn, SQL_C_DOUBLE, flValue);
         if (!aRead) {
            return std::unexpected(aRead.error());
            }
         if (!*aRead) {
            return std::optional<money_ty>{};
            }

         try {
            return std::optional<money_ty>{ money_ty{ flValue } };
            }
         catch (std::exception const& ex) {
            return std::unexpected(error_ty{
               {},
               "error for convert SQL Server decimal/numeric to money_ty",
               ex.what()
               });
            }
         }


      template <class... Args>
      framework_result_ty BuildOutputParams_(
         std::tuple<Args...> const& tupValues,
         adecc::db_params& vecParams
      ) const {
         return BuildOutputParamsImpl_(
            tupValues,
            vecParams,
            std::index_sequence_for<Args...>{}
            );
         }


      template <class... Args, std::size_t... Is>
      framework_result_ty BuildOutputParamsImpl_(
         std::tuple<Args...> const& tupValues,
         adecc::db_params& vecParams,
         std::index_sequence<Is...>
      ) const {
         framework_result_ty aResult{ true };

         auto fnAppend = [&]<std::size_t I>(std::integral_constant<std::size_t, I>) {
            if (!aResult) {
               return;
               }

            auto const& [strName, aRole] = vecOutputParameters[I];
            switch (aRole) {
            case db_output_param_role::needed_value:
            case db_output_param_role::key:
               vecParams.emplace_back(strName, db_param{ std::get<I>(tupValues) }, true);
               break;

            case db_output_param_role::may_be_missing:
               vecParams.emplace_back(strName, db_param{ std::get<I>(tupValues) }, false);
               break;

            case db_output_param_role::identity:
               if (detail::ContainsParameter(aOutputPlan, strName)) {
                  aResult = std::unexpected(error_ty{
                     {},
                     "error for bind SQL Server output parameter",
                     std::format(
                        "identity output parameter '{}' must not be used as SQL input parameter",
                        strName
                        )
                     });
                  }
               break;
               }
            };

         (fnAppend(std::integral_constant<std::size_t, Is>{}), ...);
         return aResult;
         }


      template <class tup_ty>
      std::expected<std::optional<db_value>, error_ty> ConvertIdentity_(
         std::int64_t const iIdentity
      ) const {
         if (!optIdentityIndex) {
            return std::unexpected(error_ty{
               {},
               "error for read SQL Server output identity",
               "no identity output parameter is defined"
               });
            }

         return ConvertIdentityImpl_<tup_ty>(
            iIdentity,
            std::make_index_sequence<std::tuple_size_v<tup_ty>>{}
            );
         }


      template <class tup_ty, std::size_t... Is>
      std::expected<std::optional<db_value>, error_ty> ConvertIdentityImpl_(
         std::int64_t const iIdentity,
         std::index_sequence<Is...>
      ) const {
         std::expected<std::optional<db_value>, error_ty> aResult{
            std::optional<db_value>{}
            };

         auto fnConvert = [&]<std::size_t I>(std::integral_constant<std::size_t, I>) {
            if (!aResult || aResult->has_value() || *optIdentityIndex != I) {
               return;
               }

            using elem_ty = std::tuple_element_t<I, tup_ty>;
            using clean_ty = std::remove_cvref_t<elem_ty>;
            using value_ty = detail::optional_or_self_t<clean_ty>;

            if constexpr (std::integral<value_ty> && !std::same_as<value_ty, bool>) {
               if constexpr (std::is_signed_v<value_ty>) {
                  if (iIdentity < static_cast<std::int64_t>(std::numeric_limits<value_ty>::min()) ||
                      iIdentity > static_cast<std::int64_t>(std::numeric_limits<value_ty>::max())) {
                     aResult = std::unexpected(error_ty{
                        {},
                        "error for read SQL Server output identity",
                        "IDENTITY value is outside the target C++ integer range"
                        });
                     return;
                     }
                  }
               else {
                  if (iIdentity < 0 ||
                      static_cast<unsigned long long>(iIdentity) >
                         static_cast<unsigned long long>(std::numeric_limits<value_ty>::max())) {
                     aResult = std::unexpected(error_ty{
                        {},
                        "error for read SQL Server output identity",
                        "IDENTITY value is outside the target C++ unsigned integer range"
                        });
                     return;
                     }
                  }

               aResult = std::optional<db_value>{
                  db_value{ static_cast<value_ty>(iIdentity) }
                  };
               }
            else {
               aResult = std::unexpected(error_ty{
                  {},
                  "error for read SQL Server output identity",
                  "db_output_param_role::identity requires an integral adecc result type"
                  });
               }
            };

         (fnConvert(std::integral_constant<std::size_t, Is>{}), ...);
         return aResult;
         }

   private:
      db_ty*                       pDatabase{};
      SQLHSTMT                     hStatement{ SQL_NULL_HSTMT };

      std::string                  strCurrentSql{};
      detail::parameter_plan       aParameterPlan{};

      std::string                  strOutputSql{};
      detail::parameter_plan       aOutputPlan{};
      db_output_parameters         vecOutputParameters{};
      std::optional<std::size_t>   optIdentityIndex{};

      bool                         boExecuted{ false };
      bool                         boHasCurrent{ false };
      std::vector<detail::column_descriptor> vecColumns{};
      std::vector<bound_value>     vecBindStorage{};
      std::vector<detail::parameter_descriptor> vecParamDescriptors{};
      };


   template <class db_ty>
   using fw_query = query<db_ty>;


   static_assert(adecc::db::framework_query_type<fw_query, odbc_database>);
   static_assert(adecc::db::framework_command_query_type<fw_query, odbc_database>);
   static_assert(adecc::db::framework_output_query_type<
      fw_query,
      odbc_database,
      int,
      std::string
      >);

} // namespace adecc::db::mssql
