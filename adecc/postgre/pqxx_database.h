#pragma once

#include "database_definitions.h"
#include "database_exception.h"
#include "convert_core.h"
#include "convert_fixed.h"

#include <pqxx/pqxx>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace adecc::db::postgres {

   struct postgres_credentials {
      std::string   strHost{};
      std::uint16_t uPort{ 5432 };
      std::string   strDatabase{};
      std::string   strUser{};
      std::string   strPassword{};
      bool          boIntegrated{ false };
      std::string   strSslMode{ "prefer" };
      std::string   strGssEncMode{ "disable" };
      std::string   strKrbSrvName{ "postgres" };
      std::string   strGssLib{};
      std::string   strApplicationName{};
      int           iConnectTimeout{ 30 };
      };


   namespace detail {

      struct parameter_plan {
         std::string              strSql{};
         std::vector<std::string> vecNames{};
         };


      enum class sql_scan_state {
         normal,
         single_quote,
         double_quote,
         line_comment,
         block_comment
         };


      inline char ToLowerAscii(char const ch) {
         auto const uCh = static_cast<unsigned char>(ch);
         return static_cast<char>(std::tolower(uCh));
         }


      inline bool IsNameStart(char const ch) {
         auto const uCh = static_cast<unsigned char>(ch);
         return std::isalpha(uCh) != 0 || ch == '_';
         }


      inline bool IsNameChar(char const ch) {
         auto const uCh = static_cast<unsigned char>(ch);
         return std::isalnum(uCh) != 0 || ch == '_';
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


      inline std::optional<std::size_t> FindParameter(
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

         return static_cast<std::size_t>(
            std::distance(vecNames.begin(), it)
            );
         }


      inline bool ContainsParameter(parameter_plan const& aPlan,
                                    std::string_view const svName) {
         return FindParameter(aPlan.vecNames, svName).has_value();
         }


      inline bool IsDollarQuoteStart(std::string const& strSql,
                                     std::size_t const uPos);


      /**
      \brief Builds the temporary PostgreSQL positional parameter plan.
      \details
         The public adecc SQL syntax keeps named placeholders in the form
         \c :Name. libpqxx 8 uses PostgreSQL positional placeholders in the
         form \c $1, \c $2 and so on. This function translates only that
         boundary.

         Repeated names reuse the same PostgreSQL position. Name matching is
         ASCII case-insensitive in order to preserve the existing database
         framework semantics.

         Single-quoted strings, double-quoted identifiers, line comments,
         block comments and PostgreSQL casts using \c :: are left unchanged.

         PostgreSQL positional placeholders and dollar-quoted strings are
         rejected deliberately. They are outside the adecc SQL contract and
         would make this temporary translator unnecessarily complex.
      \param strSql SQL text in adecc syntax.
      \returns Translated SQL and parameter names in PostgreSQL position order.
      \note This is deliberately a small transition layer for the current
            libpqxx parameter API and not a general PostgreSQL parser.
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

               if (ch == '-' &&
                   uPos + 1 < strSql.size() &&
                   strSql[uPos + 1] == '-') {
                  aPlan.strSql += "--";
                  uPos += 2;
                  aState = sql_scan_state::line_comment;
                  continue;
                  }

               if (ch == '/' &&
                   uPos + 1 < strSql.size() &&
                   strSql[uPos + 1] == '*') {
                  aPlan.strSql += "/*";
                  uPos += 2;
                  aState = sql_scan_state::block_comment;
                  continue;
                  }

               if (ch == ':' &&
                   uPos + 1 < strSql.size() &&
                   strSql[uPos + 1] == ':') {
                  aPlan.strSql += "::";
                  uPos += 2;
                  continue;
                  }

               if (ch == '$' &&
                   uPos + 1 < strSql.size() &&
                   std::isdigit(static_cast<unsigned char>(strSql[uPos + 1])) != 0) {
                  return std::unexpected(error_ty{
                     {},
                     "error for set sql in PostgreSQL query",
                     "PostgreSQL positional parameters are not part of the adecc SQL contract; use :Name"
                     });
                  }

               if (ch == '$' && IsDollarQuoteStart(strSql, uPos)) {
                  return std::unexpected(error_ty{
                     {},
                     "error for set sql in PostgreSQL query",
                     "PostgreSQL dollar-quoted strings are not supported by the temporary named-parameter translator"
                     });
                  }

               if (ch == ':' &&
                   uPos + 1 < strSql.size() &&
                   IsNameStart(strSql[uPos + 1])) {
                  std::size_t uEnd{ uPos + 2 };

                  while (uEnd < strSql.size() && IsNameChar(strSql[uEnd])) {
                     ++uEnd;
                     }

                  std::string_view const svName{
                     strSql.data() + uPos + 1,
                     uEnd - uPos - 1
                     };

                  auto const optIndex = FindParameter(aPlan.vecNames, svName);
                  std::size_t uIndex{};

                  if (optIndex) {
                     uIndex = *optIndex;
                     }
                  else {
                     uIndex = aPlan.vecNames.size();
                     aPlan.vecNames.emplace_back(svName);
                     }

                  aPlan.strSql += std::format("${}", uIndex + 1);
                  uPos = uEnd;
                  continue;
                  }

               aPlan.strSql += ch;
               ++uPos;
               break;

            case sql_scan_state::single_quote:
               aPlan.strSql += ch;
               ++uPos;

               if (ch == '\'') {
                  if (uPos < strSql.size() && strSql[uPos] == '\'') {
                     aPlan.strSql += strSql[uPos++];
                     }
                  else {
                     aState = sql_scan_state::normal;
                     }
                  }
               break;

            case sql_scan_state::double_quote:
               aPlan.strSql += ch;
               ++uPos;

               if (ch == '"') {
                  if (uPos < strSql.size() && strSql[uPos] == '"') {
                     aPlan.strSql += strSql[uPos++];
                     }
                  else {
                     aState = sql_scan_state::normal;
                     }
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
               if (ch == '*' &&
                   uPos + 1 < strSql.size() &&
                   strSql[uPos + 1] == '/') {
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

         return aPlan;
         }


      inline bool IsSafeSqlIdentifier(std::string_view const svName) {
         if (svName.empty() || !IsNameStart(svName.front())) {
            return false;
            }

         return std::ranges::all_of(
            svName.substr(1),
            [](char const ch) {
               return IsNameChar(ch);
               }
            );
         }


      inline bool IsDollarQuoteStart(std::string const& strSql,
                                     std::size_t const uPos) {
         if (uPos >= strSql.size() || strSql[uPos] != '$') {
            return false;
            }

         if (uPos + 1 >= strSql.size()) {
            return false;
            }

         if (strSql[uPos + 1] == '$') {
            return true;
            }

         if (!IsNameStart(strSql[uPos + 1])) {
            return false;
            }

         std::size_t uEnd{ uPos + 2 };

         while (uEnd < strSql.size() && IsNameChar(strSql[uEnd])) {
            ++uEnd;
            }

         return uEnd < strSql.size() && strSql[uEnd] == '$';
         }


      inline std::string AppendReturning(std::string const& strSql,
                                         std::string const& strIdentityName) {
         std::size_t uInsertPos = strSql.size();

         while (uInsertPos > 0 &&
                std::isspace(static_cast<unsigned char>(strSql[uInsertPos - 1])) != 0) {
            --uInsertPos;
            }

         bool const boHasSemicolon = uInsertPos > 0 && strSql[uInsertPos - 1] == ';';

         if (boHasSemicolon) {
            --uInsertPos;
            }

         std::string strResult;
         strResult.reserve(strSql.size() + strIdentityName.size() + 16);
         strResult.append(strSql, 0, uInsertPos);
         strResult += " RETURNING ";
         strResult += strIdentityName;

         if (boHasSemicolon) {
            strResult += ';';
            }

         strResult.append(strSql, boHasSemicolon ? uInsertPos + 1 : uInsertPos, std::string::npos);
         return strResult;
         }


      inline std::string NormalizePostgresTimestamp(std::string_view const svValue) {
         if (svValue.size() < 19 ||
             (svValue[10] != ' ' && svValue[10] != 'T')) {
            throw std::runtime_error("unsupported PostgreSQL timestamp representation");
            }

         std::string strValue{ svValue.substr(0, 19) };
         strValue[10] = 'T';

         std::string_view const svRest = svValue.substr(19);

         if (!svRest.empty()) {
            if (svRest.front() != '.' ||
                !std::ranges::all_of(
                   svRest.substr(1),
                   [](char const ch) {
                      return std::isdigit(static_cast<unsigned char>(ch)) != 0;
                      }
                   )) {
               throw std::runtime_error(
                  "timestamp with time zone is not supported by adecc::timestamp_ty"
                  );
               }
            }

         return strValue;
         }


      inline std::string NormalizePostgresTime(std::string_view const svValue) {
         if (svValue.size() < 8) {
            throw std::runtime_error("unsupported PostgreSQL time representation");
            }

         std::string strValue{ svValue.substr(0, 8) };
         std::string_view const svRest = svValue.substr(8);

         if (!svRest.empty()) {
            if (svRest.front() != '.' ||
                !std::ranges::all_of(
                   svRest.substr(1),
                   [](char const ch) {
                      return std::isdigit(static_cast<unsigned char>(ch)) != 0;
                      }
                   )) {
               throw std::runtime_error(
                  "time with time zone is not supported by adecc::time_ty"
                  );
               }
            }

         return strValue;
         }

   } // namespace detail


   class postgres_database {
   private:
      postgres_credentials        aCredentials{};
      std::unique_ptr<pqxx::connection> upConnection{};
      std::unique_ptr<pqxx::work>       upTransaction{};

   public:
      postgres_database() = default;


      explicit postgres_database(postgres_credentials const& aCred)
         : aCredentials{ aCred } {
         ConnectOrThrow_();
         }


      postgres_database(
         std::string strHost,
         std::string strDatabase,
         std::string strUser,
         std::string strPassword,
         std::uint16_t const uPort = 5432
      )
         : aCredentials{
              .strHost = std::move(strHost),
              .uPort = uPort,
              .strDatabase = std::move(strDatabase),
              .strUser = std::move(strUser),
              .strPassword = std::move(strPassword)
              } {
         ConnectOrThrow_();
         }


      postgres_database(postgres_database const& rhs)
         : aCredentials{ rhs.aCredentials } {
         if (rhs.Connected()) {
            ConnectOrThrow_();
            }
         }


      postgres_database& operator=(postgres_database const& rhs) {
         if (this != &rhs) {
            Close_();
            aCredentials = rhs.aCredentials;

            if (rhs.Connected()) {
               ConnectOrThrow_();
               }
            }

         return *this;
         }


      postgres_database(postgres_database&& rhs) noexcept
         : aCredentials{ std::move(rhs.aCredentials) }
         , upConnection{ std::move(rhs.upConnection) }
         , upTransaction{ std::move(rhs.upTransaction) } {
         }


      postgres_database& operator=(postgres_database&& rhs) noexcept {
         if (this != &rhs) {
            Close_();
            aCredentials = std::move(rhs.aCredentials);
            upConnection = std::move(rhs.upConnection);
            upTransaction = std::move(rhs.upTransaction);
            }

         return *this;
         }


      ~postgres_database() {
         Close_();
         }


      postgres_credentials const& Credentials() const noexcept {
         return aCredentials;
         }


      std::string GetServer() const {
         std::string const strHost = aCredentials.strHost.empty()
            ? std::string{ "<default>" }
            : aCredentials.strHost;

         return std::format("{}:{}", strHost, aCredentials.uPort);
         }


      std::string GetInformation() const {
         std::ostringstream os;

         os << "DriverID=PostgreSQL/libpqxx\n"
            << "Host=" << aCredentials.strHost << "\n"
            << "Port=" << aCredentials.uPort << "\n"
            << "Database=" << aCredentials.strDatabase << "\n"
            << "User=" << aCredentials.strUser << "\n"
            << "Integrated=" << (aCredentials.boIntegrated ? "Yes" : "No") << "\n"
            << "Password="
            << (aCredentials.boIntegrated
                  ? "<integrated>"
                  : (aCredentials.strPassword.empty() ? "" : "***"))
            << "\n"
            << "SslMode=" << aCredentials.strSslMode << "\n"
            << "GssEncMode=" << aCredentials.strGssEncMode << "\n"
            << "KrbSrvName=" << aCredentials.strKrbSrvName << "\n"
            << "GssLib=" << aCredentials.strGssLib << "\n"
            << "ApplicationName=" << aCredentials.strApplicationName << "\n"
            << "ConnectTimeout=" << aCredentials.iConnectTimeout << "\n";

         return os.str();
         }


      framework_result_ty Connect() {
         try {
            Close_();

            std::vector<std::pair<std::string, std::string>> vecConnectionParams;
            vecConnectionParams.reserve(12);

            auto fnAdd = [&vecConnectionParams](std::string strName,
                                                 std::string const& strValue) {
               if (!strValue.empty()) {
                  vecConnectionParams.emplace_back(std::move(strName), strValue);
                  }
               };

            fnAdd("host", aCredentials.strHost);
            vecConnectionParams.emplace_back("port", std::to_string(aCredentials.uPort));
            fnAdd("dbname", aCredentials.strDatabase);
            fnAdd("user", aCredentials.strUser);

            if (!aCredentials.boIntegrated) {
               fnAdd("password", aCredentials.strPassword);
               }

            fnAdd("sslmode", aCredentials.strSslMode);
            fnAdd("gssencmode", aCredentials.strGssEncMode);
            fnAdd("krbsrvname", aCredentials.strKrbSrvName);
            fnAdd("gsslib", aCredentials.strGssLib);
            fnAdd("application_name", aCredentials.strApplicationName);

            if (aCredentials.iConnectTimeout > 0) {
               vecConnectionParams.emplace_back(
                  "connect_timeout",
                  std::to_string(aCredentials.iConnectTimeout)
                  );
               }

            upConnection = std::make_unique<pqxx::connection>(vecConnectionParams);
            return upConnection && upConnection->is_open();
            }
         catch (std::exception const& ex) {
            Close_();

            return std::unexpected(error_ty{
               {},
               "database not connected",
               ex.what()
               });
            }
         }


      framework_result_ty Connect(postgres_credentials const& aCred) {
         aCredentials = aCred;
         return Connect();
         }


      bool Connected() const {
         return upConnection && upConnection->is_open();
         }


      framework_result_ty BeginTransaction() {
         if (!Connected()) {
            return false;
            }

         if (upTransaction) {
            return std::unexpected(error_ty{
               {},
               "Begin Transaction failed",
               "a PostgreSQL transaction is already active"
               });
            }

         try {
            upTransaction = std::make_unique<pqxx::work>(*upConnection);
            return true;
            }
         catch (std::exception const& ex) {
            upTransaction.reset();

            return std::unexpected(error_ty{
               {},
               "Begin Transaction failed",
               ex.what()
               });
            }
         }


      framework_result_ty Commit() {
         if (!Connected()) {
            return false;
            }

         if (!upTransaction) {
            return false;
            }

         try {
            upTransaction->commit();
            upTransaction.reset();
            return true;
            }
         catch (std::exception const& ex) {
            upTransaction.reset();

            return std::unexpected(error_ty{
               {},
               "Commit Transaction failed",
               ex.what()
               });
            }
         }


      framework_result_ty Rollback() {
         if (!Connected()) {
            return false;
            }

         if (!upTransaction) {
            return false;
            }

         try {
            upTransaction->abort();
            upTransaction.reset();
            return true;
            }
         catch (std::exception const& ex) {
            upTransaction.reset();

            return std::unexpected(error_ty{
               {},
               "Rollback Transaction failed",
               ex.what()
               });
            }
         }


      pqxx::connection& NativeConnection() {
         if (!Connected()) {
            throw std::runtime_error("PostgreSQL connection is not open");
            }

         return *upConnection;
         }


      pqxx::connection const& NativeConnection() const {
         if (!Connected()) {
            throw std::runtime_error("PostgreSQL connection is not open");
            }

         return *upConnection;
         }


      pqxx::result ExecuteNative(std::string_view const svSql,
                                 pqxx::params const& aParams) {
         if (!Connected()) {
            throw std::runtime_error("PostgreSQL connection is not open");
            }

         if (upTransaction) {
            return upTransaction->exec(svSql, aParams);
            }

         pqxx::nontransaction aTransaction{ *upConnection };
         return aTransaction.exec(svSql, aParams);
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
         try {
            if (upTransaction) {
               upTransaction->abort();
               }
            }
         catch (...) {
            }

         upTransaction.reset();
         upConnection.reset();
         }
      };


   static_assert(adecc::db::framework_database_type<postgres_database>);
   static_assert(adecc::db::framework_database_with_credentials<
      postgres_database,
      postgres_credentials
      >);


   template <adecc::db::framework_database_type db_ty>
   class query {
   public:
      using fw_query_type = pqxx::result*;

      explicit query(db_ty const& aDb)
         : pDatabase{ &const_cast<db_ty&>(aDb) } {
         if (!pDatabase->Connected()) {
            throw StandardError<database_exception>(
               {},
               in_place_exception,
               "can't create framework query",
               aDb.GetServer(),
               aDb.GetInformation(),
               "No valid PostgreSQL database connection"
               );
            }
         }


      framework_result_ty SetSql(std::string const& strSql) {
         strCurrentSql = strSql;

         auto aResult = detail::BuildParameterPlan(strCurrentSql);

         if (!aResult) {
            return std::unexpected(aResult.error());
            }

         aParameterPlan = std::move(*aResult);
         ResetResult_();
         return true;
         }


      framework_result_ty Open(std::string const& strSql,
                               adecc::db_params const& vecParams) {
         if (auto aSetResult = SetSql(strSql); !aSetResult) [[unlikely]] {
            auto const& aError = aSetResult.error();
            auto const& info = std::get<0>(aError);
            auto const& strDetails = std::get<2>(aError);

            return std::unexpected(error_ty{
               info.renew("Call Function"),
               "error for open the PostgreSQL query",
               strDetails
               });
            }

         return Open(vecParams);
         }


      framework_result_ty Open(adecc::db_params const& vecParams) {
         try {
            auto aParams = MakePgParams_(aParameterPlan, vecParams);

            if (!aParams) [[unlikely]] {
               return std::unexpected(aParams.error());
               }

            theResult = pDatabase->ExecuteNative(
               aParameterPlan.strSql,
               *aParams
               );

            boExecuted = true;
            boHasCurrent = false;
            uCurrentRow = 0;
            return true;
            }
         catch (std::exception const& ex) {
            ResetResult_();

            return std::unexpected(error_ty{
               {},
               "error for open the PostgreSQL query",
               ex.what()
               });
            }
         }


      /**
      \brief Executes a statement without framework parameters.
      \details
         This method is intended for DDL and simple DML which do not use
         \c db_params and do not return an application identity value.
      \param strSql SQL statement in adecc SQL syntax.
      \returns Number of affected rows and no identity value.
      */
      framework_output_ty ExecuteCommand(std::string const& strSql) {
         if (auto aSetResult = SetSql(strSql); !aSetResult) [[unlikely]] {
            auto const& aError = aSetResult.error();
            auto const& info = std::get<0>(aError);
            auto const& strDetails = std::get<2>(aError);

            return std::unexpected(error_ty{
               info.renew("Call Function"),
               "error for execute PostgreSQL command",
               strDetails
               });
            }

         if (!aParameterPlan.vecNames.empty()) {
            return std::unexpected(error_ty{
               {},
               "error for execute PostgreSQL command",
               "ExecuteCommand does not accept SQL parameters"
               });
            }

         try {
            pqxx::params aParams{ pDatabase->NativeConnection() };
            theResult = pDatabase->ExecuteNative(aParameterPlan.strSql, aParams);
            boExecuted = true;
            boHasCurrent = false;
            uCurrentRow = 0;

            return framework_output_result{
               static_cast<std::int64_t>(theResult.affected_rows()),
               std::nullopt
               };
            }
         catch (std::exception const& ex) {
            ResetResult_();

            return std::unexpected(error_ty{
               {},
               "error for execute PostgreSQL command",
               ex.what()
               });
            }
         }


      framework_result_ty PrepareOutput(
         std::string const& strSql,
         db_output_parameters const& vecOutputParams
      ) {
         strOutputSql = strSql;
         vecOutputParameters = vecOutputParams;
         optIdentityIndex = std::nullopt;
         strIdentityName.clear();

         if (auto aSetResult = SetSql(strOutputSql); !aSetResult) [[unlikely]] {
            auto const& aError = aSetResult.error();
            auto const& info = std::get<0>(aError);
            auto const& strDetails = std::get<2>(aError);

            return std::unexpected(error_ty{
               info.renew("Call Function"),
               "error for prepare PostgreSQL output query",
               strDetails
               });
            }

         aOutputPlan = aParameterPlan;

         for (std::size_t uIndex{}; uIndex < vecOutputParameters.size(); ++uIndex) {
            auto const& [strName, aRole] = vecOutputParameters[uIndex];

            switch (aRole) {
            case db_output_param_role::needed_value:
            case db_output_param_role::key:
               if (!detail::ContainsParameter(aOutputPlan, strName)) {
                  return std::unexpected(error_ty{
                     {},
                     "error for prepare PostgreSQL output query",
                     std::format(
                        "required output parameter '{}' is not used in SQL",
                        strName
                        )
                     });
                  }
               break;

            case db_output_param_role::may_be_missing:
               break;

            case db_output_param_role::identity:
               if (optIdentityIndex) {
                  return std::unexpected(error_ty{
                     {},
                     "error for prepare PostgreSQL output query",
                     "more than one identity output parameter is defined"
                     });
                  }

               if (detail::ContainsParameter(aOutputPlan, strName)) {
                  return std::unexpected(error_ty{
                     {},
                     "error for prepare PostgreSQL output query",
                     std::format(
                        "identity output parameter '{}' must not be used as SQL input parameter",
                        strName
                        )
                     });
                  }

               if (!detail::IsSafeSqlIdentifier(strName)) {
                  return std::unexpected(error_ty{
                     {},
                     "error for prepare PostgreSQL output query",
                     std::format(
                        "identity column '{}' is not a supported simple SQL identifier",
                        strName
                        )
                     });
                  }

               optIdentityIndex = uIndex;
               strIdentityName = strName;
               break;
               }
            }

         if (optIdentityIndex) {
            aOutputPlan.strSql = detail::AppendReturning(
               aOutputPlan.strSql,
               strIdentityName
               );
            }

         return true;
         }


      template <class... Args>
         requires (adecc::db_result_type<std::remove_cvref_t<Args>> && ...)
      framework_output_ty ExecuteOutput(
         std::tuple<Args...> const& tupValues
      ) {
         if (vecOutputParameters.size() != sizeof...(Args)) {
            return std::unexpected(error_ty{
               {},
               "error for execute PostgreSQL output query",
               std::format(
                  "output parameter count {} does not match tuple size {}",
                  vecOutputParameters.size(),
                  sizeof...(Args)
                  )
               });
            }

         adecc::db_params vecParams;
         vecParams.reserve(sizeof...(Args));

         if (auto aBuildResult = BuildOutputParams_(tupValues, vecParams);
             !aBuildResult) [[unlikely]] {
            auto const& aError = aBuildResult.error();
            auto const& info = std::get<0>(aError);
            auto const& strDetails = std::get<2>(aError);

            return std::unexpected(error_ty{
               info.renew("Call Function"),
               "error for execute PostgreSQL output query",
               strDetails
               });
            }

         auto aParams = MakePgParams_(aOutputPlan, vecParams);

         if (!aParams) [[unlikely]] {
            auto const& aError = aParams.error();
            auto const& info = std::get<0>(aError);
            auto const& strDetails = std::get<2>(aError);

            return std::unexpected(error_ty{
               info.renew("Call Function"),
               "error for execute PostgreSQL output query",
               strDetails
               });
            }

         try {
            theResult = pDatabase->ExecuteNative(
               aOutputPlan.strSql,
               *aParams
               );

            boExecuted = true;
            boHasCurrent = false;
            uCurrentRow = 0;

            std::optional<db_value> optIdentity{};

            if (optIdentityIndex) {
               if (theResult.size() != 1 || theResult.columns() != 1) {
                  return std::unexpected(error_ty{
                     {},
                     "error for read PostgreSQL output identity",
                     std::format(
                        "RETURNING expected exactly one row and one column, got {} row(s) and {} column(s)",
                        theResult.size(),
                        theResult.columns()
                        )
                     });
                  }

               auto aIdentity = ReadIdentity_<std::tuple<Args...>>(
                  theResult[0][0]
                  );

               if (!aIdentity) [[unlikely]] {
                  auto const& aError = aIdentity.error();
                  auto const& info = std::get<0>(aError);
                  auto const& strDetails = std::get<2>(aError);

                  return std::unexpected(error_ty{
                     info.renew("Call Function"),
                     "error for read PostgreSQL output identity",
                     strDetails
                     });
                  }

               optIdentity = std::move(*aIdentity);
               }

            return framework_output_result{
               static_cast<std::int64_t>(theResult.affected_rows()),
               std::move(optIdentity)
               };
            }
         catch (std::exception const& ex) {
            ResetResult_();

            return std::unexpected(error_ty{
               {},
               "error for execute PostgreSQL output query",
               ex.what()
               });
            }
         }


      framework_result_ty First() {
         if (!boExecuted || theResult.empty()) {
            boHasCurrent = false;
            return false;
            }

         uCurrentRow = 0;
         boHasCurrent = true;
         return true;
         }


      framework_result_ty Fetch() {
         if (!boExecuted || !boHasCurrent) {
            return false;
            }

         if (uCurrentRow + 1 < theResult.size()) {
            ++uCurrentRow;
            return true;
            }

         boHasCurrent = false;
         return false;
         }


      framework_result_ty Eof() const {
         return !boExecuted || !boHasCurrent;
         }


      template <class ty>
         requires adecc::is_in_type_list_v<ty, adecc::defined_values_types>
      framework_get_ty<ty> GetField(std::string const& strField) const {
         if (!boExecuted || !boHasCurrent || uCurrentRow >= theResult.size()) {
            return std::unexpected(error_ty{
               {},
               std::format("error for get the field '{}'", strField),
               "query does not have a current PostgreSQL row"
               });
            }

         auto const optIndex = FindColumn_(strField);

         if (!optIndex) {
            return std::unexpected(error_ty{
               {},
               std::format("error for get the field '{}'", strField),
               "field not found"
               });
            }

         try {
            auto const aField = theResult[uCurrentRow][*optIndex];

            if (aField.is_null()) {
               return std::nullopt;
               }

            return std::optional<ty>{ ConvertField_<ty>(aField) };
            }
         catch (std::exception const& ex) {
            return std::unexpected(error_ty{
               {},
               std::format("error for get the field '{}'", strField),
               ex.what()
               });
            }
         }


      std::vector<std::string> GetAttributes() const {
         std::vector<std::string> vecNames;

         if (!boExecuted) {
            return vecNames;
            }

         vecNames.reserve(static_cast<std::size_t>(theResult.columns()));

         for (pqxx::row_size_type uIndex{}; uIndex < theResult.columns(); ++uIndex) {
            vecNames.emplace_back(theResult.column_name(uIndex));
            }

         return vecNames;
         }


      fw_query_type NativeHandle() noexcept {
         return &theResult;
         }


      fw_query_type NativeHandle() const noexcept {
         return const_cast<pqxx::result*>(&theResult);
         }

   private:
      std::expected<pqxx::params, error_ty> MakePgParams_(
         detail::parameter_plan const& aPlan,
         adecc::db_params const& vecParams
      ) const {
         if (auto aValidation = ValidateParameters_(aPlan, vecParams);
             !aValidation) [[unlikely]] {
            return std::unexpected(aValidation.error());
            }

         try {
            pqxx::params aPgParams{ pDatabase->NativeConnection() };
            aPgParams.reserve(aPlan.vecNames.size());

            for (auto const& strName : aPlan.vecNames) {
               auto const* pParam = FindDbParam_(vecParams, strName);

               if (!pParam) {
                  return std::unexpected(error_ty{
                     {},
                     "error while binding PostgreSQL parameters",
                     std::format("Parameter '{}' not found", strName)
                     });
                  }

               std::visit(
                  DbParamVariantWriter{ &aPgParams },
                  std::get<1>(*pParam)
                  );
               }

            return aPgParams;
            }
         catch (std::exception const& ex) {
            return std::unexpected(error_ty{
               {},
               "error while binding PostgreSQL parameters",
               ex.what()
               });
            }
         }


      framework_result_ty ValidateParameters_(
         detail::parameter_plan const& aPlan,
         adecc::db_params const& vecParams
      ) const {
         for (std::size_t uLeft{}; uLeft < vecParams.size(); ++uLeft) {
            auto const& strLeft = std::get<0>(vecParams[uLeft]);

            for (std::size_t uRight{ uLeft + 1 }; uRight < vecParams.size(); ++uRight) {
               auto const& strRight = std::get<0>(vecParams[uRight]);

               if (detail::EqualName(strLeft, strRight)) {
                  return std::unexpected(error_ty{
                     {},
                     "error while binding PostgreSQL parameters",
                     std::format(
                        "Parameter '{}' is defined more than once",
                        strLeft
                        )
                     });
                  }
               }
            }

         for (auto const& aParam : vecParams) {
            auto const& strName = std::get<0>(aParam);
            bool const boRequired = std::get<2>(aParam);

            if (boRequired && !detail::ContainsParameter(aPlan, strName)) {
               return std::unexpected(error_ty{
                  {},
                  "error while binding PostgreSQL parameters",
                  std::format("Parameter '{}' not found in SQL", strName)
                  });
               }
            }

         for (auto const& strName : aPlan.vecNames) {
            if (!FindDbParam_(vecParams, strName)) {
               return std::unexpected(error_ty{
                  {},
                  "error while binding PostgreSQL parameters",
                  std::format("SQL parameter '{}' has no value", strName)
                  });
               }
            }

         return true;
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


      struct DbParamVariantWriter {
         pqxx::params* pParams{};

         template <class ty>
         void operator()(std::optional<ty> const& optValue) const {
            if (!optValue) {
               pParams->append();
               return;
               }

            (*this)(*optValue);
            }


         void operator()(std::string_view const svValue) const {
            pParams->append(std::string{ svValue });
            }


         void operator()(char const* const szValue) const {
            if (szValue) {
               pParams->append(std::string{ szValue });
               }
            else {
               pParams->append();
               }
            }


         void operator()(std::string const& strValue) const {
            pParams->append(strValue);
            }


         void operator()(double const flValue) const {
            pParams->append(flValue);
            }


         void operator()(money_ty const& aValue) const {
            pParams->append(adecc::ConvertTo<std::string>(aValue));
            }


         void operator()(int const iValue) const {
            pParams->append(iValue);
            }


         void operator()(long long const iValue) const {
            pParams->append(iValue);
            }


         void operator()(bool const boValue) const {
            pParams->append(boValue);
            }


         void operator()(unsigned int const uValue) const {
            pParams->append(uValue);
            }


         void operator()(unsigned long long const uValue) const {
            pParams->append(uValue);
            }


         void operator()(date_ty const& aValue) const {
            pParams->append(adecc::ConvertTo<std::string>(aValue));
            }


         void operator()(timestamp_ty const& aValue) const {
            pParams->append(adecc::ConvertTo<std::string>(aValue));
            }


         void operator()(time_ty const& aValue) const {
            pParams->append(adecc::ConvertTo<std::string>(aValue));
            }
         };


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

         auto fnAppend = [&]<std::size_t I>(
            std::integral_constant<std::size_t, I>
         ) {
            if (!aResult) {
               return;
               }

            auto const& [strName, aRole] = vecOutputParameters[I];

            switch (aRole) {
            case db_output_param_role::needed_value:
            case db_output_param_role::key:
               vecParams.emplace_back(
                  strName,
                  adecc::db_param{ std::get<I>(tupValues) },
                  true
                  );
               break;

            case db_output_param_role::may_be_missing:
               vecParams.emplace_back(
                  strName,
                  adecc::db_param{ std::get<I>(tupValues) },
                  false
                  );
               break;

            case db_output_param_role::identity:
               if (detail::ContainsParameter(aOutputPlan, strName)) {
                  aResult = std::unexpected(error_ty{
                     {},
                     "error for bind PostgreSQL output parameter",
                     std::format(
                        "identity output parameter '{}' must not be used as SQL input parameter",
                        strName
                        )
                     });
                  }
               break;
               }
            };

         (
            fnAppend(std::integral_constant<std::size_t, Is>{}),
            ...
            );

         return aResult;
         }


      std::optional<pqxx::row_size_type> FindColumn_(
         std::string_view const svField
      ) const {
         for (pqxx::row_size_type uIndex{}; uIndex < theResult.columns(); ++uIndex) {
            if (detail::EqualName(theResult.column_name(uIndex), svField)) {
               return uIndex;
               }
            }

         return std::nullopt;
         }


      template <class ty>
      static ty ConvertField_(pqxx::field_ref const& aField) {
         using clean_ty = std::remove_cvref_t<ty>;

         if constexpr (std::same_as<clean_ty, std::string>) {
            return std::string{ aField.view() };
            }
         else if constexpr (
            std::same_as<clean_ty, double> ||
            std::same_as<clean_ty, int> ||
            std::same_as<clean_ty, long long> ||
            std::same_as<clean_ty, bool> ||
            std::same_as<clean_ty, unsigned int> ||
            std::same_as<clean_ty, unsigned long long>
            ) {
            return aField.template as<clean_ty>();
            }
         else if constexpr (std::same_as<clean_ty, money_ty>) {
            return adecc::ConvertTo<clean_ty>(std::string{ aField.view() });
            }
         else if constexpr (std::same_as<clean_ty, date_ty>) {
            return adecc::ConvertTo<clean_ty>(std::string{ aField.view() });
            }
         else if constexpr (std::same_as<clean_ty, timestamp_ty>) {
            return adecc::ConvertTo<clean_ty>(
               detail::NormalizePostgresTimestamp(aField.view())
               );
            }
         else if constexpr (std::same_as<clean_ty, time_ty>) {
            return adecc::ConvertTo<clean_ty>(
               detail::NormalizePostgresTime(aField.view())
               );
            }
         else {
            static_assert(
               std::same_as<clean_ty, void>,
               "unsupported PostgreSQL result type"
               );
            }
         }


      template <class tup_ty>
      std::expected<std::optional<db_value>, error_ty> ReadIdentity_(
         pqxx::field_ref const& aField
      ) const {
         if (!optIdentityIndex) {
            return std::unexpected(error_ty{
               {},
               "error for read PostgreSQL output identity",
               "backend returned identity value, but no identity output parameter is defined"
               });
            }

         if (aField.is_null()) {
            return std::unexpected(error_ty{
               {},
               "error for read PostgreSQL output identity",
               "PostgreSQL RETURNING produced NULL for the identity column"
               });
            }

         return ReadIdentityImpl_<tup_ty>(
            aField,
            std::make_index_sequence<std::tuple_size_v<tup_ty>>{}
            );
         }


      template <class tup_ty, std::size_t... Is>
      std::expected<std::optional<db_value>, error_ty> ReadIdentityImpl_(
         pqxx::field_ref const& aField,
         std::index_sequence<Is...>
      ) const {
         std::expected<std::optional<db_value>, error_ty> aResult{
            std::optional<db_value>{}
            };

         auto fnRead = [&]<std::size_t I>(
            std::integral_constant<std::size_t, I>
         ) {
            if (!aResult || aResult->has_value()) {
               return;
               }

            if (*optIdentityIndex != I) {
               return;
               }

            aResult = ReadIdentityAt_<tup_ty, I>(aField);
            };

         (
            fnRead(std::integral_constant<std::size_t, Is>{}),
            ...
            );

         return aResult;
         }


      template <class tup_ty, std::size_t I>
      std::expected<std::optional<db_value>, error_ty> ReadIdentityAt_(
         pqxx::field_ref const& aField
      ) const {
         using elem_ty = std::tuple_element_t<I, tup_ty>;
         using clean_elem_ty = std::remove_cvref_t<elem_ty>;

         try {
            if constexpr (adecc::is_optional_v<clean_elem_ty>) {
               using value_ty = adecc::optional_value_type_t<clean_elem_ty>;
               value_ty const aValue = ConvertField_<value_ty>(aField);
               return std::optional<db_value>{ db_value{ aValue } };
               }
            else {
               clean_elem_ty const aValue = ConvertField_<clean_elem_ty>(aField);
               return std::optional<db_value>{ db_value{ aValue } };
               }
            }
         catch (std::exception const& ex) {
            return std::unexpected(error_ty{
               {},
               "error for read PostgreSQL output identity",
               ex.what()
               });
            }
         }


      void ResetResult_() noexcept {
         theResult.clear();
         boExecuted = false;
         boHasCurrent = false;
         uCurrentRow = 0;
         }

   private:
      db_ty*                     pDatabase{};
      pqxx::result               theResult{};
      std::size_t                uCurrentRow{};
      bool                       boExecuted{ false };
      bool                       boHasCurrent{ false };

      std::string                strCurrentSql{};
      detail::parameter_plan     aParameterPlan{};

      std::string                strOutputSql{};
      detail::parameter_plan     aOutputPlan{};
      db_output_parameters       vecOutputParameters{};
      std::optional<std::size_t> optIdentityIndex{};
      std::string                strIdentityName{};
      };


   template <class db_ty>
   using fw_query = query<db_ty>;


   static_assert(adecc::db::framework_query_type<fw_query, postgres_database>);
   static_assert(adecc::db::framework_command_query_type<fw_query, postgres_database>);
   static_assert(adecc::db::framework_output_query_type<
      fw_query,
      postgres_database,
      int,
      std::string
      >);

} // namespace adecc::db::postgres
