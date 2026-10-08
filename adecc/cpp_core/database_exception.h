// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file database_exception.h
\brief Database-specific exception types with persistent diagnostic context.

\details
Extends standard C++ exceptions with server information, query text, parameters, and backend details while
preserving a stable what() result. Diagnostic std::string content is UTF-8 by contract. This does not change
the encoding semantics of ordinary application std::string values; conversion happens when values enter the
diagnostic boundary. The design follows the library principle that exceptions remain standard C++ objects
but may carry richer contextual information.

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

#include "piggyback_exceptions.h"

#include "value_types.h"
#include "value_types_visitor.h"

#include <string>
#include <stdexcept>
#include <string_view>
#include <format>
#include <regex>

namespace adecc {

namespace db {
   class database_exception;   // Forward declarations.
   class query_exception;      // Forward declarations.
} // namespace db

template<>
struct error_name<db::database_exception> {
   static constexpr std::string_view value = "database_exception";
};

template<>
struct error_name<db::query_exception> {
   static constexpr std::string_view value = "query_exception";
};


namespace db {

/// \brief Exception for database errors
/// \details Stores server information, supplemental information, and a detailed message.
/// \note \what() is O(1) because the final message is built in the constructor.
class database_exception : public std::runtime_error {
private:
   std::string strServer      = {};
   std::string strInformation = {};
   std::string strMessage     = {};
   mutable std::string finalMessage_; ///< Stores the final message returned by \what().

   /// \brief Builds the final error message
   /// \details Uses \std::format and the base text from \std::runtime_error::what()
   void buildFinal_() const  {
   // error_name<database_exception>::value is specialized below
   finalMessage_ = std::format("{}\nserver: {}\n\nconnection information:\n{}\n\ndetailed message:\n{}\n",
         std::runtime_error::what(),
         strServer,
         strInformation,
         strMessage);
   }

public:
   using this_type = database_exception;

   database_exception() = delete;

   /// \brief Constructor accepting all fields
   /// \param msg_ Base error message for \std::runtime_error
   /// \param server_ Server identifier, for example Host:Port.
   /// \param information_ Context or supplemental information.
   /// \param errorinfo_ Detailed database error description
   database_exception(std::string const& msg_,
                      std::string const& server_,
                      std::string const& information_,
                      std::string const& errorinfo_)
      : std::runtime_error { msg_ },
        strServer { server_ },
        strInformation { std::move( clear_pwd(information_) ) },
        strMessage{ std::move( clear_pwd(errorinfo_) ) } {
      buildFinal_();
   }

   database_exception(database_exception const&) = default;
   database_exception(database_exception&&) noexcept = default;
   database_exception& operator=(database_exception const&) = default;
   database_exception& operator=(database_exception&&) noexcept = default;
   ~database_exception() override = default;

   /// \brief Returns the server identifier.
   std::string const& Server() const { return strServer; }

   /// \brief Returns supplemental information.
   std::string const& Information() const { return strInformation; }

   /// \brief Detailed database message
   std::string const& Message() const { return strMessage; }

   /// \brief Returns the prepared error message.
   /// \returns Pointer to the internal null-terminated storage
   char const* what() const noexcept override {
      return finalMessage_.c_str();
      }

   std::string clear_pwd(std::string const& strText) {
      static std::regex pwd_pattern(R"((?:Pwd|pwd|Password|password)=[^\s]+)");
      return std::regex_replace(strText, pwd_pattern, "Pwd=***");
      }
};




/// \brief Exception for failed database queries
/// \details Derives from \database_exception and adds query and parameter text.
/// \note \what() 
///       is O(1); the final text is built in the constructor.
class query_exception : public database_exception {
private:
   std::string strQuery      = {};
   std::string strParameter  = {};
   mutable std::string finalMessage_;  ///< Final message owned by the derived class.

   /// \brief Builds the final error message for the derived class
   void buildFinal_() const {
      // Append query and parameter text to the base message
      finalMessage_ = std::format("{}\n\nquery:\n{}\n\nparameters:\n{}",
         database_exception::what(), strQuery, strParameter);
   }

public:
   using this_type  = query_exception;
   using base_type  = database_exception;

   query_exception() = delete;

   /// \brief Complete constructor
   /// \param msg_ Base text for \std::runtime_error
   /// \param server_ Server identifier, for example Host:Port.
   /// \param information_ Context or supplemental information.
   /// \param errorinfo_ Detailed database error
   /// \param query_ SQL or query text.
   /// \param parameter_ Rendered parameter list, for example "p1=..., p2=...".
   query_exception(std::string const& msg_,
                   std::string const& server_,
                   std::string const& information_,
                   std::string const& errorinfo_,
                   std::string const& query_,
                   std::string const& parameter_)
      : database_exception{ msg_, server_, information_, errorinfo_ },
        strQuery{ query_ }, strParameter{ parameter_ } {
      buildFinal_();
   }

   /// \brief Convenience constructor accepting \std::string_view
   query_exception(std::string_view svMsg,
                   std::string_view svServer,
                   std::string_view svInformation,
                   std::string_view svErrorInfo,
                   std::string_view svQuery,
                   std::string_view svParameter)
      : database_exception{ std::string{ svMsg },
                            std::string{ svServer },
                            std::string{ svInformation },
                            std::string{ svErrorInfo } },
        strQuery{ svQuery },
        strParameter{ svParameter } {
      buildFinal_();
   }

   query_exception(query_exception const&)                = default;
   query_exception(query_exception&&) noexcept            = default;
   query_exception& operator=(query_exception const&)     = default;
   query_exception& operator=(query_exception&&) noexcept = default;
   ~query_exception() override                            = default;

   /// \brief Abfragetext
   std::string const& Query() const { return strQuery; }

   /// \brief Parameterdarstellung
   std::string const& Parameter() const { return strParameter; }

   /// \brief Precomputed message for the derived class
   char const* what() const noexcept override {
      return finalMessage_.c_str();
   }
};


} // namespace db

} // namespace adecc

