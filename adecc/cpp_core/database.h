// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file database.h
\brief Typed relational database facade exposing queries, transactions, sources, and sinks as C++ abstractions.

\details
Builds the logical database layer on top of backend concepts and typed values. Queries expose rows as tuples
and ranges, output operations act as typed sinks, and transaction scopes use RAII. SQL remains explicit while
framework-specific database mechanics stay behind the backend boundary.

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

#include "tuple_check.h"
#include "database_definitions.h"
#include "database_exception.h"

#include "generator.h"

#include <string>
#include <sstream>
#include <vector>
#include <stdexcept>
#include <tuple>
#include <mutex>
#include <chrono>
#include <optional>
#include <concepts>
#include <ranges>
#include <variant>
#include <format>
#include <cstdint>
#include <utility>
#include <type_traits>

namespace adecc {
   namespace db {


      // ------------------------------------------------------------
      // 2) query<fw_type, db_ty>: lazy and unbuffered; derives from fw_type<db_ty>
      // ------------------------------------------------------------

      template <template<class> class fw_type, framework_database_type db_ty>
         requires framework_query_type<fw_type, db_ty>
      class logical_query : public fw_type<db_ty> {
      public:
         using base_ty = fw_type<db_ty>;
         using fw_query_type = typename base_ty::fw_query_type;

         /**
            \brief Row proxy providing \c Get<T>() for the current row
         */
         class RowView {
         public:
            explicit RowView(logical_query const* p) : aQuery{ p } {}

            template <typename ty> requires db_result_type<ty>
            std::optional<ty> Get(std::string const& strField, bool needed = true) const {
               return aQuery->template Get<ty>(strField, needed);
            }

            /*! \brief Builds a tuple from the current row using column names */
            template <typename... Args> requires (db_result_type<Args> && ...)
               std::tuple<Args...> AsTuple(std::vector<std::string> const& vecNames) const {
               return aQuery->template MakeRowTuple<Args...>(vecNames);
            }

            /** \brief Builds a tuple from the current row using GetAttributes() */
            template <typename... Args> requires (db_result_type<Args> && ...)
               std::tuple<Args...> AsTuple() const {
               return aQuery->template MakeRowTuple<Args...>(aQuery->GetAttributes());
            }

         private:
            logical_query const* aQuery{};
         };

         /**
            \brief Lazy iterator driving Fetch()/Eof() and yielding \c RowView
         */
         class iterator {
         public:
            using iterator_concept = std::input_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = RowView;

            iterator() noexcept = default;

            // Do not advance in the constructor.
            explicit iterator(logical_query* p) : aQuery{ p } {
               if (p) [[likely]] {
                  if (auto is_eof = p->base_ty::Eof(); !is_eof) [[unlikely]] {
                     auto const& [info, msg, details] = is_eof.error();
                     throw StandardError<query_exception>(info.renew("Exception throw"), in_place_exception,
                        msg,
                        aQuery->theDatabase.GetServer(),
                        aQuery->theDatabase.GetInformation(),
                        details,
                        aQuery->GetSQL(),
                        aQuery->GetParamsAsText());
                  }
                  else bEof = *is_eof; // steht Open() auf 1. Zeile? -> Eof=false
               }
               else {
                  bEof = true;
               }
            }

            // The iterator also serves as its own range
            iterator begin() {
               if (!aQuery) {
                  bEof = true;
                  return *this;
               }

               if (auto fst = aQuery->base_ty::First(); !fst) [[unlikely]] {
                  auto const& [info, msg, details] = fst.error();
                  throw StandardError<query_exception>(info.renew("Exception throw"), in_place_exception,
                     msg,
                     aQuery->theDatabase.GetServer(),
                     aQuery->theDatabase.GetInformation(),
                     details,
                     aQuery->GetSQL(),
                     aQuery->GetParamsAsText());
               }
               else {
                  bEof = !(*fst);

                  if (!bEof) {
                     if (auto eof = aQuery->base_ty::Eof(); !eof) [[unlikely]] {
                        auto const& [info, msg, details] = eof.error();
                        throw StandardError<query_exception>(info.renew("Exception throw"), in_place_exception,
                           msg,
                           aQuery->theDatabase.GetServer(),
                           aQuery->theDatabase.GetInformation(),
                           details,
                           aQuery->GetSQL(),
                           aQuery->GetParamsAsText());
                     }
                     else {
                        bEof = *eof;
                     }
                  }

                  return *this;
               }
            }

            std::default_sentinel_t end() const noexcept { return {}; }

            value_type operator*() const { return RowView{ aQuery }; }

            iterator& operator++() {
               if (!aQuery || bEof) return *this;

               // EXAKT EIN Schritt nach vorn       ((( !!! )))
               if (auto has = aQuery->base_ty::Fetch(); !has) [[unlikely]] {
                  auto const& [info, msg, details] = has.error();
                  throw StandardError<query_exception>(info.renew("Exception throw"), in_place_exception,
                     msg,
                     aQuery->theDatabase.GetServer(),
                     aQuery->theDatabase.GetInformation(),
                     details,
                     aQuery->GetSQL(),
                     aQuery->GetParamsAsText());
               }
               else {
                  if (auto eof = aQuery->base_ty::Eof(); !eof) [[unlikely]] {
                     auto const& [info, msg, details] = eof.error();
                     throw StandardError<query_exception>(info.renew("Exception throw"), in_place_exception,
                        msg,
                        aQuery->theDatabase.GetServer(),
                        aQuery->theDatabase.GetInformation(),
                        details,
                        aQuery->GetSQL(),
                        aQuery->GetParamsAsText());
                  }
                  else {
                     bEof = (!(*has)) || (*eof);
                  }
               }
               return *this;
            }

            void operator++(int) { ++(*this); }

            friend bool operator==(iterator const& it, std::default_sentinel_t) noexcept {
               return !it.aQuery || it.bEof;
            }

         private:
            logical_query* aQuery{};
            bool bEof{ false };
         };


         struct rows_range {
            logical_query* q;
            iterator begin() { return iterator{ q }; }
            std::default_sentinel_t end() const noexcept { return {}; }
         };


      public:
         explicit logical_query(db_ty const& aDb) : base_ty{ aDb }, theDatabase{ aDb } {
         }


         /**
         \brief Sets SQL in the framework object and stores the query text locally
         */
         logical_query& SetSQL(std::string aQuery) {
            strQuery = std::move(aQuery);

            if (auto val = this->base_ty::SetSql(strQuery); !val) {
               auto const& [info, msg, details] = val.error();
               throw StandardError<query_exception>(
                  info.renew("Exception throw"),
                  in_place_exception,
                  msg,
                  theDatabase.GetServer(),
                  theDatabase.GetInformation(),
                  details,
                  GetSQL(),
                  GetParamsAsText()
               );
            }

            return *this;
         }

         std::string const& GetSQL(ExceptionInformation const& call_info = { "Called from",  src_loc::current(),
                            std::chrono::system_clock::now() }) const {
            // SQL must already be set.
            if (strQuery.empty()) {
               throw StandardError<query_exception>({ "Exception throw",  src_loc::current(),
                                             std::chrono::system_clock::now() },
                  in_place_exception,
                  "Query is empty.",
                  theDatabase.GetServer(),
                  theDatabase.GetInformation(),
                  "",
                  "",
                  GetParamsAsText());
            }
            return strQuery;
         }

         /**
         \brief Binds one parameter (name, db_param, flag)
         */
         logical_query& Set(db_param2 const& aOne) {
            vecParams.emplace_back(aOne);
            return *this;
         }

         /**
         \brief Binds multiple parameters
         */
         logical_query& Set(db_params const& aList) {
            for (auto const& p : aList) {
               vecParams.emplace_back(p);
            }
            return *this;
         }


         db_params const& GetParams() const {
            return vecParams;
         }

         /**
         \brief Starts lazy execution: calls \c Open(sql, params), while the iterator drives Fetch
         */
         iterator Execute() {

            // error_ty   std::tuple<ExceptionInformation, std::string, std::string>;
            if (auto val = OpenIfNeeded(); !val) {
               auto const& [info, msg, details] = val.error();
               throw StandardError<query_exception>(info.renew("Exception throw"), in_place_exception,
                  msg,
                  theDatabase.GetServer(),
                  theDatabase.GetInformation(),
                  details,
                  GetSQL(),
                  GetParamsAsText());
            }
            return iterator{ this };
         }

         iterator Execute(db_params const& aList) {
            // Open directly with the parameters passed here
            vecParams = aList;

            if (auto val = this->base_ty::Open(GetSQL(), vecParams); !val) {
               auto const& [info, msg, details] = val.error();
               throw StandardError<query_exception>(info.renew("Exception throw"), in_place_exception,
                  msg,
                  theDatabase.GetServer(),
                  theDatabase.GetInformation(),
                  details,
                  GetSQL(),
                  GetParamsAsText());
            }

            // State marker for other paths that inspect Eof/Fetch:
            bOpened = true;

            return iterator{ this };
         }

         /**
         \brief Spaltenliste vom Framework (in fester Reihenfolge)
         */
         std::vector<std::string> GetAttributes() const {
            return this->base_ty::GetAttributes();
         }

         /*!
         \brief Typed access to a cell in the current row (lazy, unbuffered)
         */
         template <typename ty> requires db_result_type<ty>
         std::conditional_t<is_optional_v<ty>, ty, std::optional<ty>> Get(std::string const& strField, bool needed = true) const {

            using value_ty = std::conditional_t<is_optional_v<ty>, optional_value_type_t<ty>, ty>;

            auto aOpt = this->base_ty::template GetField<value_ty>(strField);

            if (!aOpt.has_value()) {
               auto const& [info, msg, details] = aOpt.error();
               throw StandardError<query_exception>(info.renew("Exception throw"), in_place_exception,
                  msg,
                  theDatabase.GetServer(),
                  theDatabase.GetInformation(),
                  details,
                  GetSQL(),
                  GetParamsAsText());
            }
            else [[likely]] {
               if constexpr (is_optional_v<ty>) {
                  return aOpt.value(); // ok: optional<base_T> konvertiert zu ty (= optional<base_T>)
               }
               else {
                  if (!(aOpt.value().has_value()) && needed) {
                     throw StandardError<query_exception>({}, in_place_exception,
                        std::format("error for get the field '{}'", strField),
                        theDatabase.GetServer(), theDatabase.GetInformation(),
                        "NULL encountered for non-optional result field",
                        GetSQL(),
                        GetParamsAsText());
                  }
                  return aOpt.value();
               }
            }
         }




         // … in logical_query:
         rows_range ExecuteRange(db_params const& params) {
            (void)Execute(params); // öffnet und positioniert
            return rows_range{ this };
         }

         /*!
         \brief Lazy typed output as Generator<std::tuple<Args...>>
         */
         template <typename... Args> requires (db_result_type<Args> && ...)
            Generator<std::tuple<Args...>> Get() const {
            auto it = const_cast<logical_query*>(this)->Execute().begin();
            auto aAttrs = GetAttributes();
            for (; it != it.end(); ++it) {
               auto aTup = MakeRowTuple<Args...>(aAttrs);
               co_yield aTup;
            }
            co_return;
         }


         template <class... Args> requires (db_result_type<Args> && ...)
            Generator<std::tuple<Args...>> Execute(db_params const& vecParams) const {
            auto it = const_cast<logical_query*>(this)->Execute(vecParams).begin();
            auto vecAttrs = GetAttributes();
            for (; it != it.end(); ++it) {
               co_yield MakeRowTuple<Args...>(vecAttrs);
            }
            co_return;
         }


         template <class... Args> requires (db_result_type<Args> && ...)
            std::expected<std::tuple<Args...>, SingleRowError> ExecuteOne() {
            return ExecuteOneImpl_<true, Args...>(/*useStored*/ true, db_params{});
         }

         // 2) Single-row variant: pass an explicit parameter list so the query can be reused
         template <class... Args> requires (db_result_type<Args> && ...)
            std::expected<std::tuple<Args...>, SingleRowError> ExecuteOne(db_params const& params) {
            return ExecuteOneImpl_<true, Args...>(/*useStored*/ false, params);
         }

         static std::string ParamsAsText(db_params const& vecTheParams) {
            std::ostringstream os;

            for (auto const& [strName, aValue, boRequired] : vecTheParams) {
               auto const [strValue, strType] = std::visit(adecc::value_types_visit{}, aValue);

               std::println(os,
                  "{} | {} | {} | Required = {}",
                  strName,
                  strType,
                  strValue,
                  boRequired);
            }

            return os.str();
         }

         std::string GetParamsAsText() const {
            return ParamsAsText(vecParams);
         }

         /*!
         \brief Prepares an output operation
         \details
            The SQL statement and the position-bound output parameter description are
            forwarded to the physical backend.

            This method supports OutputSink and OutputRange. The actual values are supplied
            later, one tuple at a time, through \c ExecuteOutput.

         \tparam Args Types of the later tuple value
         \param strSql SQL statement
         \param vecOutputParameters Output parameter description.
         \returns Reference to this query
         \throw query_exception if physical preparation fails
         */
         template <class... Args>
            requires framework_output_query_type<fw_type, db_ty, Args...>
         logical_query& PrepareOutput(std::string strSql, db_output_parameters vecOutputParameters) {
            strQuery = std::move(strSql);
            vecOutputParams = std::move(vecOutputParameters);

            if (auto val = this->base_ty::PrepareOutput(strQuery, vecOutputParams); !val) {
               auto const& [info, msg, details] = val.error();

               throw StandardError<query_exception>(
                  info.renew("Exception throw"),
                  in_place_exception,
                  msg,
                  theDatabase.GetServer(),
                  theDatabase.GetInformation(),
                  details,
                  GetSQL(),
                  GetParamsAsText()
               );
            }

            bOutputPrepared = true;
            return *this;
         }


         /*!
         \brief Executes the prepared output statement for one tuple
         \details
            The tuple is neither modified nor consumed. The backend returns only technical
            metadata: the affected-row count and an optional generated identity value.

         \tparam Args Types of the tuple value.
         \param tupValues Values of the current range element
         \returns Result containing affected-row count and an optional identity value
         \throw query_exception if physical execution fails
         */
         template <class... Args>
            requires framework_output_query_type<fw_type, db_ty, Args...>
         framework_output_result ExecuteOutput(std::tuple<Args...> const& tupValues) {
            if (!bOutputPrepared) {
               throw StandardError<query_exception>(
                  ExceptionInformation{},
                  in_place_exception,
                  "output query is not prepared",
                  theDatabase.GetServer(),
                  theDatabase.GetInformation(),
                  "PrepareOutput must be called before ExecuteOutput",
                  GetSQL(),
                  GetParamsAsText()
               );
            }

            RememberOutputParams_(tupValues);

            if (auto val = this->base_ty::template ExecuteOutput<Args...>(tupValues); !val) {
               auto const& [info, msg, details] = val.error();

               throw StandardError<query_exception>(
                  info.renew("Exception throw"),
                  in_place_exception,
                  msg,
                  theDatabase.GetServer(),
                  theDatabase.GetInformation(),
                  details,
                  GetSQL(),
                  GetParamsAsText()
               );
            }
            else {
               return *val;
            }
         }


         /*!
         \brief Returns the prepared output-parameter description
         */
         db_output_parameters const& GetOutputParameters() const noexcept {
            return vecOutputParams;
         }


      private:
         template <class... Args>
         void RememberOutputParams_(std::tuple<Args...> const& tupValues) {
            vecParams.clear();
            vecParams.reserve(sizeof...(Args));

            RememberOutputParamsImpl_(
               tupValues,
               std::index_sequence_for<Args...>{}
            );
         }

         template <class... Args, std::size_t... Is>
         void RememberOutputParamsImpl_(std::tuple<Args...> const& tupValues,
            std::index_sequence<Is...>) {
            auto fnAddParam = [&]<std::size_t I>(std::integral_constant<std::size_t, I>) {
               auto const& [strName, aRole] = vecOutputParams[I];

               if (aRole == db_output_param_role::identity) {
                  return;
               }

               vecParams.emplace_back(
                  strName,
                  db_param{ std::get<I>(tupValues) },
                  true
               );
            };

            (fnAddParam(std::integral_constant<std::size_t, Is>{}), ...);
         }

         framework_result_ty OpenIfNeeded() {
            if (bOpened) {
               return true;
            }

            if (auto val = this->base_ty::Open(GetSQL(), vecParams); !val) {
               return val;
            }

            bOpened = true;
            return true;
         }

         template <bool WithException, class... Args>
         std::conditional_t<WithException, std::tuple<Args...>,
            std::expected<std::tuple<Args...>, SingleRowError>>
            ExecuteOneImpl_(bool useStored, db_params const& overrideParams) {

            // Use explicit parameters or the internally stored parameters
            db_params const& ps = useStored ? vecParams : overrideParams;
            if (!useStored) {
               vecParams = overrideParams;
            }

            // Always open freshly here and position on the first row
            if (auto val = this->base_ty::Open(GetSQL(), ps); !val) {
               auto const& [info, msg, details] = val.error();
               throw StandardError<query_exception>(info.renew("exception called"),
                  in_place_exception,
                  msg,
                  theDatabase.GetServer(),
                  theDatabase.GetInformation(),
                  details,
                  GetSQL(),
                  GetParamsAsText()); // ps

               // "ExecuteOne: Open(sql, params) failed in framework base"
            }

            // Spaltenreihenfolge holen (vom Framework)
            auto attrs = this->base_ty::GetAttributes();

            // Zero rows?
            if (!this->base_ty::First() || this->base_ty::Eof()) {
               if constexpr (WithException == true) {
                  throw StandardError<query_exception>(ExceptionInformation{},
                     in_place_exception,
                     "reading a single dataset."s,
                     theDatabase.GetServer(),
                     theDatabase.GetInformation(),
                     "dataset with this parameter not found."s,
                     GetSQL(),
                     GetParamsAsText()
                  );
               }
               else {
                  return std::unexpected(SingleRowError::NotFound); // not found
               }
            }

            // Project the first row as a tuple
            auto tup = MakeRowTuple<Args...>(attrs);

            // Is another row available?
            if (this->base_ty::Fetch() && !this->base_ty::Eof()) {
               if constexpr (WithException == true) {
                  throw StandardError<query_exception>(ExceptionInformation{},
                     in_place_exception,
                     "reading a single dataset."s,
                     theDatabase.GetServer(),
                     theDatabase.GetInformation(),
                     "dataset with this parameter not unique."s,
                     GetSQL(),
                     GetParamsAsText()
                  );
               }
               else {
                  return std::unexpected(SingleRowError::TooMany);  // too many
               }
            }

            return tup;
         }

         template <typename... Args, std::size_t... Is>
         std::tuple<Args...> MakeRowTupleImpl(std::vector<std::string> const& vecNames, std::index_sequence<Is...>) const {
            if (vecNames.size() != sizeof...(Args)) {
               throw std::runtime_error{ "column count mismatch vs. result Args" };
            }
            return std::tuple<Args...>{ GetField<Args>(vecNames[Is])... };
         }

         template <typename... Args>
         std::tuple<Args...> MakeRowTuple(std::vector<std::string> const& vecNames) const {
            return MakeRowTupleImpl<Args...>(vecNames, std::make_index_sequence<sizeof...(Args)>{});
         }

         template <typename ty>
         ty GetField(std::string const& strField) const {
            if constexpr (is_optional_v<ty>) {
               return Get<ty>(strField, false);
            }
            else {
               auto aOpt = Get<ty>(strField, true);
               if (!aOpt) {
                  throw StandardError<query_exception>(ExceptionInformation{ },
                     in_place_exception,
                     "database::GetField",
                     theDatabase.GetServer(),
                     theDatabase.GetInformation(),
                     "NULL encountered for non-optional result field",
                     GetSQL(),
                     GetParamsAsText());
               }
               return *aOpt;
            }
         }


      private:
         db_ty const& theDatabase;
         std::string       strQuery{};
         db_params         vecParams{};
         bool              bOpened{ false };

         db_output_parameters vecOutputParams{};
         bool                 bOutputPrepared{ false };
      };


      template <class range_ty, class... Args>
      concept tuple_input_range_for = std::ranges::input_range<range_ty> &&
         std::same_as<std::remove_cvref_t<std::ranges::range_value_t<range_ty>>,
         std::tuple<Args...>
         >;

      template <framework_database_type db_ty, template<class> class qry_ty>
         requires framework_query_type<qry_ty, db_ty>
      class logical_database : public db_ty {
      public:
         using base_type = db_ty;
         using query_type = logical_query<qry_ty, db_ty>;

         // ------------------------------------------------------------------------
         // RAII transaction helper
         // ------------------------------------------------------------------------
         /**
         \brief RAII transaction scope for \c logical_database
         \details
            \par Semantik
            \li Constructor: starts a transaction (\c BeginTransaction)
            \li \c Commit(false): commits and closes the transaction
            \li Destructor: rolls back any transaction that is still active
            \note Move-only to prevent duplicate completion
         */
         class TransactionScope {
         public:
            /**
            \brief Starts a new transaction
            \throw database_exception if \c BeginTransaction fails
            */
            explicit TransactionScope(logical_database& aDb) : pDb{ &aDb }, bActive{ true } {
               if (auto ta = pDb->BeginTransaction(); !ta) {
                  pDb = nullptr;
                  bActive = false;

                  auto const& [info, msg, details] = ta.error();
                  throw database_exception(msg, aDb.GetServer(), aDb.GetInformation(), details);
                  }
               else {
                  if (!(*ta)) {
                     pDb = nullptr;
                     bActive = false;

                     throw database_exception("Begin Transaction failed",
                        aDb.GetServer(),
                        aDb.GetInformation(),
                        "retval is false");
                     }
                  }
               }

            /**
            \brief Adopts an already active transaction.
            \details Does not call BeginTransaction in the constructor.
            \param aDb Database instance.
            \param adoptedActive Indicates whether a transaction is already active.
            \note Useful for nested or externally controlled transactions.
            */
            TransactionScope(logical_database& aDb, std::adopt_lock_t, bool adoptedActive) noexcept
               : pDb{ &aDb }, bActive{ adoptedActive } {
               }

            TransactionScope(TransactionScope&& aOther) noexcept
               : pDb{ std::exchange(aOther.pDb, nullptr) }
               , bActive{ std::exchange(aOther.bActive, false) }
               , bCommitted{ std::exchange(aOther.bCommitted, false) } {
               }

            TransactionScope& operator=(TransactionScope&& aOther) noexcept {
               if (this != &aOther) {
                  tryRollback_();

                  pDb = std::exchange(aOther.pDb, nullptr);
                  bActive = std::exchange(aOther.bActive, false);
                  bCommitted = std::exchange(aOther.bCommitted, false);
                  }

               return *this;
               }

            TransactionScope(TransactionScope const&) = delete;
            TransactionScope& operator=(TransactionScope const&) = delete;

            /**
            \brief Commits the active transaction.
            \details
               If \c boContinue is true, a new transaction is started immediately after a successful commit.
               If \c boContinue is false, the scope becomes inactive.
            \param boContinue Whether to start a new transaction after the commit.
            \throw database_exception if Commit or the subsequent BeginTransaction fails.
            */
            void Commit(bool const boContinue = true) {
               if (!pDb || !bActive) {
                  return;
                  }

               if (auto ta = pDb->Commit(); !ta) [[unlikely]] {
                  auto const& [info, msg, details] = ta.error();
                  throw database_exception(msg, pDb->GetServer(), pDb->GetInformation(), details);
                  }
               else {
                  if (!(*ta)) [[unlikely]] {
                     throw database_exception("Commit Transaction failed",
                        pDb->GetServer(),
                        pDb->GetInformation(),
                        "retval is false");
                     }
                  }

               bCommitted = true;
               bActive = false;

               if (boContinue) {
                  if (auto ta = pDb->BeginTransaction(); !ta) [[unlikely]] {
                     auto const& [info, msg, details] = ta.error();
                     throw database_exception(msg, pDb->GetServer(), pDb->GetInformation(), details);
                     }
                  else {
                     if (!(*ta)) [[unlikely]] {
                        throw database_exception("Begin Transaction failed",
                           pDb->GetServer(),
                           pDb->GetInformation(),
                           "retval is false");
                        }
                     }

                  bCommitted = false;
                  bActive = true;
                  }
               }

            void CommitAndContinue() {
               Commit(true);
               }

            void CommitAndClose() {
               Commit(false);
               }

            /**
            \brief Performs an explicit rollback.
            \details The scope is inactive afterwards.
            */
            void Rollback() noexcept {
               tryRollback_();
               }

            ~TransactionScope() {
               tryRollback_();
               }

         private:
            void tryRollback_() noexcept {
               if (pDb && bActive && !bCommitted) {
                  (void)pDb->Rollback();
                  bActive = false;
                  bCommitted = false;
                  }
               }

         private:
            logical_database* pDb{};
            bool bActive{ false };
            bool bCommitted{ false };
            };
         // -- Konstruktion ---------------------------------------------------------
         logical_database() = default;

         template <class... Args> requires std::constructible_from<db_ty, Args...>
         explicit logical_database(Args&&... aArgs) : db_ty{ std::forward<Args>(aArgs)... } {}

         // -- Parameter builder ----------------------------------------------------
         static db_param2 Param(std::string aName, db_param aValue, bool bRequired = true) {
            return db_param2{ std::move(aName), std::move(aValue), bRequired };
            }

         static db_param2 Param(std::string aName, char const* const szValue, bool bRequired = true) {
            return db_param2{ std::move(aName), db_param{ szValue }, bRequired };
            }

         static db_param2 Param(std::string aName, wchar_t const* const szValue, bool bRequired = true) {
            return db_param2{ std::move(aName), db_param{ szValue }, bRequired };
            }

         template <class value_ty>
            requires (!adecc::is_optional_v<std::remove_cvref_t<value_ty>> &&
                      !std::same_as<std::remove_cvref_t<value_ty>, db_param>&&
                      adecc::db_param_atom<std::remove_cvref_t<value_ty>>)
         static db_param2 Param(std::string aName, value_ty&& aValue, bool bRequired = true) {
            return db_param2{ std::move(aName), db_param{ std::forward<value_ty>(aValue) }, bRequired };
         }

         template <class value_ty>
            requires adecc::is_optional_v<std::remove_cvref_t<value_ty>>
         static db_param2 Param(std::string aName, value_ty&& optValue, bool bRequired = true) {
            using clean_ty = std::remove_cvref_t<value_ty>;
            using inner_ty = adecc::optional_value_type_t<clean_ty>;

            static_assert(adecc::db_param_atom<inner_ty>,
               "optional value type is not a valid db_param atom");

            return db_param2{ std::move(aName), db_param{ std::forward<value_ty>(optValue) }, bRequired };
         }

         template <class value_ty>
            requires adecc::db_param_atom<std::remove_cvref_t<value_ty>>
         static db_param2 ParamNull(std::string aName, bool bRequired = true) {
            using clean_ty = std::remove_cvref_t<value_ty>;

            return db_param2{ std::move(aName), db_param{ std::optional<clean_ty>{} }, bRequired };
         }

         template <class... Pairs>
            requires (std::same_as<std::remove_cvref_t<Pairs>, db_param2> && ...)
         static db_params Params(Pairs&&... aPairs) {
            db_params vec{};
            vec.reserve(sizeof...(aPairs));
            (vec.emplace_back(std::forward<Pairs>(aPairs)), ...);
            return vec;
            }

         // -- Query-Erzeugung ------------------------------------------------------
         //[[nodiscard]] std::unique_ptr<query_type> CreateQuery() const {
         //   }

         [[nodiscard]] query_type MakeQuery() const {
            return query_type{ static_cast<db_ty const&>(*this) };
            }

         // -- Execute (lazy, getypt) -----------------------------------------------
         template <class... Args> requires (db_result_type<Args> && ...)
            Generator<std::tuple<Args...>> Execute(std::string const& strSql) const {
            return ExecuteImpl_<Args...>(
               std::string{ strSql },
               db_params{}
               );
            }

         template <class... Args> requires (db_result_type<Args> && ...)
            Generator<std::tuple<Args...>> Execute(std::string const& strSql, db_params const& vecParams) const {
            return ExecuteImpl_<Args...>(
               std::string{ strSql },
               db_params{ vecParams }
               );
            }

         template <class... Args> requires (db_result_type<Args> && ...)
            Generator<std::tuple<Args...>> ExecuteImpl_(std::string strSql, db_params vecParams) const {
            EnsureConnected_();

            auto aQry = MakeQuery_();        // Query by value im Coroutine-Frame
            aQry.SetSQL(std::move(strSql));

            auto it = aQry.Execute(vecParams).begin();
            auto vecAttrs = aQry.GetAttributes();

            for (; it != it.end(); ++it) {
               co_yield MakeRowTuple_<Args...>(aQry, vecAttrs);
            }

            co_return;
         }

         template <class... Args, class... Pairs>
            requires ((db_result_type<Args> && ...) && (std::same_as<std::remove_cvref_t<Pairs>, db_param2> && ...))
         Generator<std::tuple<Args...>> Execute(std::string const& strSql, Pairs&&... aPairs) const {
            return ExecuteImpl_<Args...>(
               std::string{ strSql },
               Params(std::forward<Pairs>(aPairs)...)
            );
         }


         template <class... Args> requires (db_result_type<Args> && ...)
            std::expected<std::tuple<Args...>, SingleRowError> ExecuteOne(std::string const& strSql) const {
            return ExecuteOneImpl_<false, Args...>(strSql, db_params{});
         }

         // --- ExecuteOne with a complete parameter list ---------------------------
         template <class... Args> requires (db_result_type<Args> && ...)
            std::expected<std::tuple<Args...>, SingleRowError> ExecuteOne(std::string const& strSql, db_params const& vecParams) const {
            return ExecuteOneImpl_<false, Args...>(strSql, vecParams);
         }


         template <class... Args> requires (db_result_type<Args> && ...)
            std::tuple<Args...> ExecuteOne1(std::string const& strSql, db_params const& vecParams) const {
            auto t = ExecuteOneImpl_<true, Args...>(strSql, vecParams);
            return t;
         }

         /*!
         \brief Executes an SQL statement without parameters and without a result set
         \details
            This method is intended for DDL and simple DML, for example
            \c create \c table, \c drop \c table, or \c truncate.

         \param strSql SQL statement
         \returns Number of affected rows; no domain result value is returned
         \throw database_exception if the database connection cannot be established
         \throw query_exception if execution fails
         */
         framework_output_result ExecuteCommand(std::string const& strSql) const
            requires framework_command_query_type<qry_ty, db_ty> {
            EnsureConnected_();

            auto aQry = MakeQuery_();

            if (auto val = aQry.ExecuteCommand(std::string{ strSql }); !val) {
               auto const& [info, msg, details] = val.error();
               db_ty const& aDb = static_cast<db_ty const&>(*this);

               throw StandardError<query_exception>(info.renew("Exception throw"),
                  in_place_exception,
                  msg,
                  aDb.GetServer(),
                  aDb.GetInformation(),
                  details,
                  strSql,
                  "");
            }
            else {
               return *val;
            }
         }

         // --- Transaction convenience API -------------------------------------------------
         /**
         \brief Starts a transaction and returns the RAII guard
         \details
            Typische Nutzung:
            \code
               auto tx = db.Transaction();
               for (auto&& row : db.Execute<int>("select ...")) { ... }
               tx.Commit();
            \endcode
         */
         [[nodiscard]] TransactionScope Transaction() {
            return TransactionScope{ *this };
         }

         /**
         \brief Adopts an already active transaction context
         \details Does not begin a transaction; provides RAII protection for commit or automatic rollback.
         */
         [[nodiscard]] TransactionScope AdoptTransaction(bool active) noexcept {
            return TransactionScope{ *this, std::adopt_lock, active };
         }


         /*!
         \brief Output sink for a prepared database operation
         \details
            The sink consumes an input range of \c std::tuple<Args...> values.
            The prepared physical output statement is executed once per element.

            The sink does not produce domain values.
         */
         template <class... Args>
            requires framework_output_query_type<qry_ty, db_ty, Args...>
         class OutputSink {
         public:
            /*!
            \brief Output iterator for standard algorithms
            \details
               Each assignment of a \c std::tuple<Args...> executes the prepared database operation.

            \note
               The iterator does not own the sink. The sink must remain valid while the iterator is used.
            */
            class iterator {
            public:
               using iterator_concept = std::output_iterator_tag;
               using difference_type = std::ptrdiff_t;

               iterator() noexcept = default;

               explicit iterator(OutputSink* pSink) noexcept
                  : pSink{ pSink } {
               }

               iterator& operator*() noexcept {
                  return *this;
               }

               iterator& operator++() noexcept {
                  return *this;
               }

               iterator operator++(int) noexcept {
                  return *this;
               }

               iterator& operator=(std::tuple<Args...> const& tupValues) {
                  if (pSink == nullptr) {
                     throw std::runtime_error{
                        "OutputSink iterator is not bound to a sink"
                     };
                  }

                  pSink->Write_(tupValues);

                  return *this;
               }

            private:
               OutputSink* pSink{};
            };

            OutputSink(query_type aQuery, std::string strSql, db_output_parameters vecOutputParameters)
               : aQuery{ std::move(aQuery) } {
               this->aQuery.template PrepareOutput<Args...>(std::move(strSql), std::move(vecOutputParameters));
            }

            /*!
            \brief Transfers all elements of the input range to the database
            \details
               The prepared output statement is executed for every tuple.
               Backend return values are not interpreted as domain values.
            */
            template <class range_ty>
               requires tuple_input_range_for<range_ty, Args...>
            void operator()(range_ty&& rngInput) {
               for (auto&& tupValues : rngInput) {
                  using current_ty = std::remove_cvref_t<decltype(tupValues)>;

                  if constexpr (std::same_as<current_ty, std::tuple<Args...>>) {
                     Write_(tupValues);
                     }
                  else {
                     std::tuple<Args...> tupCurrent{
                        std::forward<decltype(tupValues)>(tupValues)
                        };
                     Write_(tupCurrent);
                     }
               }
            }

            /*!
            \brief Executes the prepared output statement for a single tuple
            \details
               This method allows the sink to be used with exactly one tuple value.

            \param tupValues Values of the row to transfer
            \returns Reference to this sink
            */
            OutputSink& operator=(std::tuple<Args...> const& tupValues) {
               Write_(tupValues);

               return *this;
            }

            /*!
            \brief Executes the prepared output statement for a range
            \details
               This method assigns a complete input range to the sink.

            \tparam range_ty Input-range type
            \param rngInput Input range
            \returns Reference to this sink
            */
            template <class range_ty>
               requires tuple_input_range_for<range_ty, Args...>
            OutputSink& operator=(range_ty const& rngInput) {
               (*this)(rngInput);

               return *this;
            }

            /*!
            \brief Returns an output iterator for standard algorithms
            \details
               The iterator can be used as the destination of \c std::ranges::copy.
               Each copied tuple is written through the prepared database operation.

            \returns Output iterator targeting this sink
            */
            iterator OutputIterator() noexcept {
               return iterator{ this };
            }

         private:
            void Write_(std::tuple<Args...> const& tupValues) {
               [[maybe_unused]] auto tupResult = aQuery.template ExecuteOutput<Args...>(tupValues);
            }

         private:
            query_type aQuery;
         };

         /*!
         \brief Transforming output-range adapter.
         \details
            The adapter consumes an input range of \c std::tuple<Args...> values.
            Each output tuple initially equals its input tuple. If an identity position is
            described and the backend returns an identity value, that position is replaced.
         */
         template <class... Args>
            requires framework_output_query_type<qry_ty, db_ty, Args...>
         class OutputRange {
         public:
            OutputRange(query_type aQuery, std::string strSql, db_output_parameters vecOutputParameters)
               : aQuery{ std::move(aQuery) },
               vecOutputParameters{ std::move(vecOutputParameters) } {
               ValidateOutputParameters_();
               this->aQuery.template PrepareOutput<Args...>(std::move(strSql), this->vecOutputParameters);
            }

            /*!
            \brief Transforms the input range
            \details
               The input range must remain valid while the generator is evaluated.
            */
            template <class range_ty>
               requires tuple_input_range_for<range_ty, Args...>
            Generator<std::tuple<Args...>> operator()(range_ty&& rngInput) {
               for (auto&& tupInput : rngInput) {
                  std::tuple<Args...> tupOutput{ std::forward<decltype(tupInput)>(tupInput) };
                  auto tupResult = aQuery.template ExecuteOutput<Args...>(tupOutput);

                  ApplyIdentity_(tupOutput, std::get<1>(tupResult));

                  co_yield tupOutput;
               }

               co_return;
            }

         private:
            void ValidateOutputParameters_() const {
               if (vecOutputParameters.size() != sizeof...(Args)) {
                  throw std::runtime_error{
                     std::format("output parameter count {} does not match tuple size {}",
                                 vecOutputParameters.size(), sizeof...(Args))
                  };
               }

               std::size_t uIdentityCount{ 0 };

               for (auto const& [strName, aRole] : vecOutputParameters) {
                  if (aRole == db_output_param_role::identity) {
                     ++uIdentityCount;
                  }
               }

               if (uIdentityCount > 1) {
                  throw std::runtime_error{ "output range supports at most one identity parameter" };
               }
            }

            static std::optional<std::size_t> FindIdentityIndex_(db_output_parameters const& vecOutputParameters) {
               for (std::size_t uIndex{}; uIndex < vecOutputParameters.size(); ++uIndex) {
                  auto const& [strName, aRole] = vecOutputParameters[uIndex];

                  if (aRole == db_output_param_role::identity) {
                     return uIndex;
                  }
               }

               return std::nullopt;
            }

            template <std::size_t I, class Tup>
            static bool TrySetIdentityAt_(Tup& tupOutput, std::size_t const uIdentityIndex, db_value const& aIdentity) {
               if (uIdentityIndex != I) {
                  return false;
               }

               using elem_ty = std::tuple_element_t<I, Tup>;
               using clean_elem_ty = std::remove_cvref_t<elem_ty>;

               if constexpr (is_optional_v<clean_elem_ty>) {
                  using value_ty = optional_value_type_t<clean_elem_ty>;

                  if (auto const* pValue = std::get_if<value_ty>(&aIdentity)) {
                     std::get<I>(tupOutput) = *pValue;
                     return true;
                  }
               }
               else {
                  if (auto const* pValue = std::get_if<clean_elem_ty>(&aIdentity)) {
                     std::get<I>(tupOutput) = *pValue;
                     return true;
                  }
               }

               throw std::runtime_error{ std::format("identity value type does not match tuple position {}", I) };
            }

            template <class Tup, std::size_t... Is>
            static bool ApplyIdentityImpl_(Tup& tupOutput, std::size_t const uIdentityIndex,
               db_value const& aIdentity, std::index_sequence<Is...>) {
               return (TrySetIdentityAt_<Is>(tupOutput, uIdentityIndex, aIdentity) || ...);
            }

            void ApplyIdentity_(std::tuple<Args...>& tupOutput, std::optional<db_value> const& optIdentity) const {
               if (!optIdentity) {
                  return;
               }

               auto optIdentityIndex = FindIdentityIndex_(vecOutputParameters);

               if (!optIdentityIndex) {
                  throw std::runtime_error{ "backend returned identity value, but no identity parameter is defined" };
               }

               bool const bApplied = ApplyIdentityImpl_(tupOutput,
                  *optIdentityIndex,
                  *optIdentity,
                  std::make_index_sequence<sizeof...(Args)> {});

               if (!bApplied) {
                  throw std::runtime_error{
                     std::format("identity parameter index {} is outside the output tuple", *optIdentityIndex)
                  };
               }
            }

         private:
            query_type            aQuery;
            db_output_parameters  vecOutputParameters;
         };

         /*!
         \brief Creates a prepared output sink
         \details
            The query is prepared and encapsulated by the sink. The sink can then transfer
            a range of \c std::tuple<Args...> values to the database.
         */
         template <class... Args>
            requires framework_output_query_type<qry_ty, db_ty, Args...>
         [[nodiscard]] OutputSink<Args...> MakeOutputSink(std::string const& strSql,
            db_output_parameters vecOutputParameters) const {
            EnsureConnected_();

            return OutputSink<Args...> { MakeQuery_(), std::string{ strSql }, std::move(vecOutputParameters) };
         }

         /*!
         \brief Creates a prepared transforming output-range adapter
         \details
            The adapter executes one prepared database operation per input tuple and returns
            the original tuple. Only the position marked as
            \c db_output_param_role::identity may be replaced.
         */
         template <class... Args>
            requires framework_output_query_type<qry_ty, db_ty, Args...>
         [[nodiscard]] OutputRange<Args...> MakeOutputRange(std::string const& strSql,
            db_output_parameters vecOutputParameters) const {
            EnsureConnected_();

            return OutputRange<Args...> { MakeQuery_(), std::string{ strSql }, std::move(vecOutputParameters) };
         }

      private:
         // -- interne Hilfen -------------------------------------------------------
         void EnsureConnected_(ExceptionInformation const& call_info = { "Called from",  src_loc::current(),
                                                                      std::chrono::system_clock::now() }) const {
            db_ty const& aDb = static_cast<db_ty const&>(*this);
            if (!aDb.Connected()) {
               auto& self = const_cast<db_ty&>(aDb);

               if (auto ret = self.Connect(); !ret) {
                  auto const& [info, msg, details] = ret.error();
                  info.trace(call_info);
                  throw StandardError<database_exception>(info.renew("Exception throw"),
                     in_place_exception,
                     msg, aDb.GetServer(),
                     aDb.GetInformation(),
                     details);
               }
            }
         }


         template <typename Elem_ty>
         static Elem_ty GetTupleElem_(query_type const& aQry, std::string const& strName) {
            using clean_elem = remove_cvref_t<Elem_ty>;

            if constexpr (is_optional_v<clean_elem>) {
               // Elem is optional: pass the optional directly; do not dereference it
               return aQry.template Get<clean_elem>(strName, /*needed*/ false);
            }
            else {
               // Elem is not optional: Get returns optional<Elem>, which must contain a value when needed=true
               auto opt = aQry.template Get<clean_elem>(strName, /*needed*/ true);
               return *opt; // hier OK, weil needed=true vorher NULL abgefangen hat
            }
         }

         template <tuple_like Tup, std::size_t... Is>
         static Tup MakeRowTupleImpl_(query_type const& aQry,
            std::vector<std::string> const& vecNames,
            std::index_sequence<Is...>) {
            if (vecNames.size() != std::tuple_size_v<Tup>) {
               throw std::runtime_error{ "column count mismatch vs. result Tup" };
            }

            return Tup{
                    GetTupleElem_<std::tuple_element_t<Is, Tup>>(aQry, vecNames[Is])...
            };
         }

         template <class... Args>
         static std::tuple<Args...> MakeRowTuple_(query_type const& aQry,
            std::vector<std::string> const& vecNames) {
            using row_tuple_t = std::tuple<Args...>;
            return MakeRowTupleImpl_<row_tuple_t>(
               aQry,
               vecNames,
               std::make_index_sequence<std::tuple_size_v<row_tuple_t>>{}
            );
         }


         // Single-row execution returns NotFound or TooManyRows when no exception is requested.
         template <bool WithException, class... Args>
         std::conditional_t<WithException, std::tuple<Args...>,
            std::expected<std::tuple<Args...>, SingleRowError>>
            ExecuteOneImpl_(std::string const& strSql, db_params const& vecParams) const {

            EnsureConnected_();

            // Keep the query in the local frame; do not copy it out
            auto aQry = MakeQuery_();
            aQry.SetSQL(std::string{ strSql });
            if (!vecParams.empty()) aQry.Set(vecParams);

            // Open and retain the column order
            auto rngIt = aQry.Execute(vecParams).begin(); // Iterator range
            auto rngEnd = rngIt.end();
            auto attrs = aQry.GetAttributes();

            // Zero rows?
            if (rngIt == rngEnd) {
               if constexpr (WithException == true) {
                  db_ty const& aDb = static_cast<db_ty const&>(*this);
                  throw StandardError<query_exception>(ExceptionInformation{},
                     in_place_exception,
                     "reading a single dataset."s,
                     aDb.GetServer(),
                     aDb.GetInformation(),
                     "dataset with this parameter not found."s,
                     aQry.GetSQL(),
                     aQry.GetParamsAsText()
                  );
               }
               else {
                  return std::unexpected(SingleRowError::NotFound); // false => not found
               }
            }

            // Evaluate the first row
            auto tup = MakeRowTuple_<Args...>(aQry, attrs);

            // Is a second row available?
            ++rngIt;
            if (rngIt != rngEnd) {
               if constexpr (WithException == true) {
                  db_ty const& aDb = static_cast<db_ty const&>(*this);
                  throw StandardError<query_exception>(ExceptionInformation{},
                     in_place_exception,
                     "reading a single dataset."s,
                     aDb.GetServer(),
                     aDb.GetInformation(),
                     "dataset isn't unique."s,
                     aQry.GetSQL(),
                     aQry.GetParamsAsText()
                  );
               }
               else {
                  return std::unexpected(SingleRowError::TooMany);  // true => too many
               }
            }

            return tup; // genau eine Zeile
         }


         [[nodiscard]] query_type MakeQuery_() const {
            return query_type{ static_cast<db_ty const&>(*this) };
         }


      };


      template <typename ty>
      concept logical_database_type =
         requires(ty theDb, ty const theConstDb, std::string const& strSql, adecc::db_params const& vecParams) {
         typename std::remove_cvref_t<ty>::base_type;
         typename std::remove_cvref_t<ty>::query_type;
         typename std::remove_cvref_t<ty>::TransactionScope;

            requires framework_database_type<typename std::remove_cvref_t<ty>::base_type>;

         { theConstDb.MakeQuery() } -> std::same_as<typename std::remove_cvref_t<ty>::query_type>;
         { theDb.Transaction() } -> std::same_as<typename std::remove_cvref_t<ty>::TransactionScope>;
         { theDb.AdoptTransaction(true) } -> std::same_as<typename std::remove_cvref_t<ty>::TransactionScope>;

         { theDb.template Execute<int>(strSql, vecParams) };
         { theDb.template ExecuteOne<int>(strSql, vecParams) };

         { std::remove_cvref_t<ty>::Param(std::string{}, adecc::db_param{ int{} }, true) } -> std::same_as<adecc::db_param2>;
         { std::remove_cvref_t<ty>::Param(std::string{}, std::optional<int>{}, true) } -> std::same_as<adecc::db_param2>;
         { std::remove_cvref_t<ty>::template ParamNull<int>(std::string{}, true) } -> std::same_as<adecc::db_param2>;

      }&& std::derived_from<std::remove_cvref_t<ty>, typename std::remove_cvref_t<ty>::base_type>;


   } // namespace db
} // namespace adecc
