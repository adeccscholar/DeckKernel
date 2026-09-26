// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file database_definitions.h
\brief Concepts and common types defining the contract of physical database backends.

\details
Defines the compile-time interface expected from database and query backends, including credentials, typed
field access, command execution, prepared output operations, and result metadata. These concepts keep the
logical database layer independent of a specific database framework.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Database as a Relational Source and Sink".
- "Database as Source, Transformation, and Sink".
- "Why This Is Not a Classical ORM: SQL Remains Visible".
- "Values, Parameters, and the Defined Database Type Space".

\see ARCHITECTURE.md#database-as-a-relational-source-and-sink

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

#include "type_lists.h"
#include "value_types.h"

#include "piggyback_exceptions.h"

#include <string>
#include <vector>
#include <stdexcept>
#include <optional>
#include <concepts>
#include <expected>
#include <tuple>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace adecc::db {

   // ============================================================================
   // Concept for the framework query base class used by query<T>
   // ============================================================================

   struct db_type_error : std::runtime_error { using std::runtime_error::runtime_error; };

   enum class SingleRowError : uint32_t { NotFound, TooMany };

   enum class TraceMode : uint32_t { standard, informations, tracing };

   using error_ty = std::tuple<ExceptionInformation, std::string, std::string>;

   using framework_result_ty = std::expected<bool, error_ty>;

   template <typename ty>
   using framework_get_ty = std::expected<std::optional<ty>, error_ty>;


   /*!
   \brief Result of a physical output operation
   \details
      First element: number of affected rows.
      Second element: optional identity value.

      The identity value is a \c db_value because it must be a returnable
      database value. If the backend cannot provide an identity value
      or none was generated, the second element is \c std::nullopt.
   */
   using framework_output_result = std::tuple<
      std::int64_t,
      std::optional<db_value>
   >;


   /*!
   \brief Return type for physical output execution
   \details
      On success, technical execution metadata is returned.
      Domain output values are not assembled by the backend, but
      are derived from the original tuple in the logical layer.
   */
   using framework_output_ty = std::expected<framework_output_result, error_ty>;


   /*!
   \brief Base contract for a physical database without concrete credentials
   \details
      The database must support connection and transactions and must be copyable
      and movable. Errors are transported through \c framework_result_ty.
   */
   template <class db_ty>
   concept framework_database_type =
      std::default_initializable<db_ty> &&
      std::is_copy_constructible_v<db_ty> &&
      std::is_move_constructible_v<db_ty> &&
      requires (db_ty & db, db_ty const& cdb) {
         { db.Connect() }          -> std::same_as<framework_result_ty>;
         { db.BeginTransaction() } -> std::same_as<framework_result_ty>;
         { db.Commit() }           -> std::same_as<framework_result_ty>;
         { db.Rollback() }         -> std::same_as<framework_result_ty>;

         { cdb.Connected() }       -> std::same_as<bool>;
         { cdb.GetServer() }       -> std::same_as<std::string>;
         { cdb.GetInformation() }  -> std::same_as<std::string>;
   };


   /*!
   \brief Checks whether a credential type is accepted by a database
   \details
      A credential type is valid if the database can be constructed from it
      or provides a matching \c Connect overload.
   */
   template <class db_ty, class cred_ty>
   concept framework_credentials_for =
      std::constructible_from<db_ty, cred_ty> ||
      requires (db_ty & db, cred_ty const& cred) {
         { db.Connect(cred) } -> std::same_as<framework_result_ty>;
   };


   /*!
   \brief Checks whether a database supports all specified credential types
   \details
      Each credential type is checked individually against \c framework_credentials_for.
   */
   template <class db_ty, class... Cs>
   concept framework_database_with_credentials =
      framework_database_type<db_ty> &&
      (framework_credentials_for<db_ty, Cs> && ...);


   /*!
   \brief Checks typed field access for a concrete database value type
   \details
      The backend must provide field access for every defined value type
      by its name.
   */
   template <class Q, class ty>
   concept has_getfield_for =
      requires(Q const& q, std::string const& strField) {
         { q.template GetField<ty>(strField) } -> std::same_as<framework_get_ty<ty>>;
   };


   template <template<class> class query_ty, class db_ty, class List>
   struct check_getfield_all : std::false_type {};


   /*!
   \brief Checks field access for every type in a type list
   \details
      The specialization expects a type list of the form \c L<Ts...>.
   */
   template <
      template<class> class query_ty,
      class db_ty,
      template<class...> class L,
      class... Ts
   >
   struct check_getfield_all<query_ty, db_ty, L<Ts...>>
      : std::bool_constant<(has_getfield_for<query_ty<db_ty>, Ts> && ...)> {
   };


   /*!
   \brief Checks whether a query can prepare an output statement
   \details
      Preparation receives the SQL statement and the complete
      positionsgebundene Output-Parameterbeschreibung.

      At this stage, the backend may set SQL, analyze placeholders, and
      perform backend-specific preparation. The actual tuple values
      are passed only to \c ExecuteOutput.
   */
   template <class Q>
   concept has_prepare_output =
      requires(
   Q & aQry,
      std::string const& strSql,
      db_output_parameters const& vecOutputParameters
      ) {
         {
            aQry.PrepareOutput(
               strSql,
               vecOutputParameters
            )
         } -> std::same_as<framework_result_ty>;
   };


   /*!
   \brief Checks physical output execution for a concrete tuple type combination
   \details
      The method receives only the complete tuple of the current
      range element. The SQL statement and output-parameter description
      must already have been prepared through \c PrepareOutput.

      Tuple values are passed as \c const&. The backend must neither
      modify nor consume them.
   */
   template <class Q, class... Args>
   concept has_execute_output_for =
      (db_result_type<std::remove_cvref_t<Args>> && ...) &&
      requires(
   Q & aQry,
      std::tuple<std::remove_cvref_t<Args>...> const& tupValues
      ) {
         {
            aQry.template ExecuteOutput<std::remove_cvref_t<Args>...>(
               tupValues
            )
         } -> std::same_as<framework_output_ty>;
   };


   /**
   \brief Checks whether a query can execute simple SQL commands
   \details
      This method is intended for DDL and simple DML without domain output.
      It is independent of OutputSink and OutputRange.
   */
   template <class Q>
   concept has_execute_command = requires(Q & aQry, std::string const& strSql) {
         { aQry.ExecuteCommand(strSql) } -> std::same_as<framework_output_ty>;
      };

   /**
   \brief Concept for a framework query template specialized for a database type
   \details
      For a concrete \c DB, \c framework_query_type must provide the following interface:
      \li Constructor \c query_ty(db_ty const&)
      \li \c using fw_query_type = ...
      \li \c SetSql(std::string const&) -> framework_result_ty
      \li \c Open(std::string const&, db_params const&) -> framework_result_ty
      \li \c Open(db_params const&) -> framework_result_ty
      \li \c First() -> framework_result_ty
      \li \c Fetch() -> framework_result_ty
      \li \c Eof() const -> framework_result_ty
      \li \c GetField<T>(std::string const&) const -> framework_get_ty<T>
      \li \c GetAttributes() const -> std::vector<std::string>

      \note
      Output-sink and output-range capability is deliberately not part of this
      general Concept. For concrete tuple types, it is checked through
      \c framework_output_query_type.
   */
   template <template<class> class query_ty, class db_ty>
   concept framework_query_type =
      framework_database_type<db_ty> &&
      check_getfield_all<query_ty, db_ty, defined_values_types>::value&&
      requires(
   db_ty const& db,
      std::string const& strSql,
      db_params const& vecParams
      ) {
      typename query_ty<db_ty>;
      { query_ty<db_ty>{ db } };
      typename query_ty<db_ty>::fw_query_type;

      { std::declval<query_ty<db_ty>&>().SetSql(strSql) }           -> std::same_as<framework_result_ty>;
      { std::declval<query_ty<db_ty>&>().Open(strSql, vecParams) }  -> std::same_as<framework_result_ty>;
      { std::declval<query_ty<db_ty>&>().Open(vecParams) }          -> std::same_as<framework_result_ty>;
      { std::declval<query_ty<db_ty>&>().First() }                  -> std::same_as<framework_result_ty>;
      { std::declval<query_ty<db_ty>&>().Fetch() }                  -> std::same_as<framework_result_ty>;
      { std::declval<query_ty<db_ty> const&>().Eof() }              -> std::same_as<framework_result_ty>;
      { std::declval<query_ty<db_ty> const&>().GetAttributes() }    -> std::same_as<std::vector<std::string>>;
   };

   /**
   \brief Checks whether a concrete query can execute SQL commands
   \details
      This concept is independent of OutputSink and OutputRange.
   */
   template <class Q>
   concept framework_command_query_for = has_execute_command<Q>;


   /**
   \brief Framework query including command capability
   \details
      This Concept combines the general query contract with the ability
      to execute simple SQL commands without domain output.
   */
   template <template<class> class query_ty, class db_ty>
   concept framework_command_query_type = framework_query_type<query_ty, db_ty> &&
                                          framework_command_query_for<query_ty<db_ty>>;


   /**
   \brief Checks whether a concrete query supports output processing
   \details
      This Concept describes the additional backend capability for prepared
      Output-Sinks und transformierende Output-Ranges.

      The query must:
      \li Provides \c PrepareOutput for SQL and output-parameter descriptions
      \li Provides \c ExecuteOutput for the concrete \c std::tuple<Args...>

      The types \c Args... must be valid database result types according to
      \c db_result_type and therefore satisfy the constraints of \c db_value.
   */
   template <class Q, class... Args>
   concept framework_output_query_for = has_prepare_output<Q> && 
                                        has_execute_output_for<Q, Args...>;


   /**
   \brief Framework query including output capability for concrete types
   \details
      This Concept combines the general query contract with the concrete
      Output execution for \c std::tuple<Args...>.
   */
   template <template<class> class query_ty, class db_ty, class... Args>
   concept framework_output_query_type = framework_query_type<query_ty, db_ty> &&
                                         framework_output_query_for<query_ty<db_ty>, Args...>;

} // namespace adecc::db
