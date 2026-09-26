// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file test_framework.h
\brief Lightweight typed test harness and result model for cpp_core integration tests.

\details
Defines test cases, tuple-like result records, timing, expected-success and expected-error execution,
diagnostics, and reporting helpers. The harness intentionally stays within standard C++ so that the same
architectural tests can be reused across backends and toolchains.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Tests as Architectural Proof".
- "Migration as an Architecture Test".
- "The Cost Model of Abstraction".
- "The Core Belongs in C++".

\see ../ARCHITECTURE.md#tests-as-architectural-proof

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

#include "value_types.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <exception>
#include <format>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <print>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace adecc::test {

   enum class test_case_kind {
      initialize,
      expected_success,
      expected_error,
      finish
   };

   inline std::string_view ToText(test_case_kind const aKind) {
      switch (aKind) {
      case test_case_kind::initialize:
         return "Initialize";
      case test_case_kind::expected_success:
         return "Expected";
      case test_case_kind::expected_error:
         return "Error expected";
      case test_case_kind::finish:
         return "Finish";
      default:
         return "Unknown";
      }
   }

   struct test_result {
      test_case_kind aType{};
      std::string    strName{};
      std::chrono::system_clock::time_point aStartedAt{};
      long long      iDurationMs{};
      bool           boPassed{};
      std::string    strStatusText{};
      std::string    strExceptionText{};
   };

   template <std::size_t I>
   decltype(auto) get(test_result& aResult) noexcept {
      if constexpr (I == 0) {
         return (aResult.aType);
      }
      else if constexpr (I == 1) {
         return (aResult.strName);
      }
      else if constexpr (I == 2) {
         return (aResult.aStartedAt);
      }
      else if constexpr (I == 3) {
         return (aResult.iDurationMs);
      }
      else if constexpr (I == 4) {
         return (aResult.boPassed);
      }
      else if constexpr (I == 5) {
         return (aResult.strStatusText);
      }
      else if constexpr (I == 6) {
         return (aResult.strExceptionText);
      }
      else {
         static_assert(I < 7, "test_result has only seven tuple-like fields");
      }
   }

   template <std::size_t I>
   decltype(auto) get(test_result const& aResult) noexcept {
      if constexpr (I == 0) {
         return (aResult.aType);
      }
      else if constexpr (I == 1) {
         return (aResult.strName);
      }
      else if constexpr (I == 2) {
         return (aResult.aStartedAt);
      }
      else if constexpr (I == 3) {
         return (aResult.iDurationMs);
      }
      else if constexpr (I == 4) {
         return (aResult.boPassed);
      }
      else if constexpr (I == 5) {
         return (aResult.strStatusText);
      }
      else if constexpr (I == 6) {
         return (aResult.strExceptionText);
      }
      else {
         static_assert(I < 7, "test_result has only seven tuple-like fields");
      }
   }

   template <std::size_t I>
   decltype(auto) get(test_result&& aResult) noexcept {
      if constexpr (I == 0) {
         return std::move(aResult.aType);
      }
      else if constexpr (I == 1) {
         return std::move(aResult.strName);
      }
      else if constexpr (I == 2) {
         return std::move(aResult.aStartedAt);
      }
      else if constexpr (I == 3) {
         return std::move(aResult.iDurationMs);
      }
      else if constexpr (I == 4) {
         return std::move(aResult.boPassed);
      }
      else if constexpr (I == 5) {
         return std::move(aResult.strStatusText);
      }
      else if constexpr (I == 6) {
         return std::move(aResult.strExceptionText);
      }
      else {
         static_assert(I < 7, "test_result has only seven tuple-like fields");
      }
   }

   class test_assertion_error : public std::runtime_error {
   public:
      explicit test_assertion_error(std::string const& strMessage)
         : std::runtime_error{ strMessage } {
      }
   };

   inline std::string EscapeResultText(std::string_view const svText) {
      std::string strResult;
      strResult.reserve(svText.size());

      for (char const ch : svText) {
         switch (ch) {
         case '\\':
            strResult += "\\\\";
            break;
         case '\t':
            strResult += "\\t";
            break;
         case '\r':
            strResult += "\\r";
            break;
         case '\n':
            strResult += "\\n";
            break;
         default:
            strResult += ch;
            break;
         }
      }

      return strResult;
   }

   inline std::string FormatTimestamp(std::chrono::system_clock::time_point const aTimePoint) {
      auto const aTime = std::chrono::system_clock::to_time_t(aTimePoint);
      auto const iMillis = std::chrono::duration_cast<std::chrono::milliseconds>(
         aTimePoint.time_since_epoch()).count() % 1000;

      std::tm aTimeParts{};

#ifdef _WIN32
      localtime_s(&aTimeParts, &aTime);
#else
      localtime_r(&aTime, &aTimeParts);
#endif

      std::ostringstream strmTimestamp;
      strmTimestamp << std::put_time(&aTimeParts, "%Y-%m-%d %H:%M:%S");
      strmTimestamp << std::format(".{:03}", iMillis);
      return strmTimestamp.str();
   }

   inline int PassedCount(std::vector<test_result> const& vecResults) {
      return static_cast<int>(std::ranges::count_if(vecResults, [](test_result const& aResult) {
         return aResult.boPassed;
         }));
   }

   inline int FailedCount(std::vector<test_result> const& vecResults) {
      return static_cast<int>(std::ranges::count_if(vecResults, [](test_result const& aResult) {
         return !aResult.boPassed;
         }));
   }

   inline void Summary(std::ostream& out, std::string_view const svTitle,
      std::vector<test_result> const& vecResults) {
      std::println(out, "");
      std::println(out, "{}", svTitle);
      std::println(out, "Test run resolved.");
      std::println(out, "Passed tests: {}", PassedCount(vecResults));
      std::println(out, "Failed tests: {}", FailedCount(vecResults));
      std::println(out, "");
   }

   class test_run {
   public:
      explicit test_run(std::string strTitle = "Test run")
         : strTitle_{ std::move(strTitle) } {
      }

      test_run(test_run const&) = delete;
      test_run& operator=(test_run const&) = delete;

      test_run(test_run&&) noexcept = default;
      test_run& operator=(test_run&&) noexcept = default;

      ~test_run() = default;

      [[nodiscard]] std::string const& Title() const noexcept {
         return strTitle_;
      }

      void SetTitle(std::string strTitle) {
         strTitle_ = std::move(strTitle);
      }

      template <class fn_ty>
      bool Initialize(std::string_view const svName, fn_ty&& fnInitialize) {
         if (bInitialized_) {
            return boInitializePassed_;
         }

         bInitialized_ = true;
         boInitializePassed_ = ExecuteExpectedSuccess_(test_case_kind::initialize, svName,
            std::forward<fn_ty>(fnInitialize), "Initialize completed successfully.");

         return boInitializePassed_;
      }

      template <class fn_ty>
      bool Initialize(fn_ty&& fnInitialize) {
         return Initialize("Initialize", std::forward<fn_ty>(fnInitialize));
      }

      template <class fn_ty>
      bool Finalize(std::string_view const svName, fn_ty&& fnFinalize) {
         if (bFinalized_) {
            return boFinalizePassed_;
         }

         bFinalized_ = true;
         boFinalizePassed_ = ExecuteExpectedSuccess_(test_case_kind::finish, svName,
            std::forward<fn_ty>(fnFinalize), "Finalize completed successfully.");

         return boFinalizePassed_;
      }

      template <class fn_ty>
      bool Finalize(fn_ty&& fnFinalize) {
         return Finalize("Finalize", std::forward<fn_ty>(fnFinalize));
      }

      template <class fn_ty>
      bool Finish(std::string_view const svName, fn_ty&& fnFinish) {
         return Finalize(svName, std::forward<fn_ty>(fnFinish));
      }

      template <class fn_ty>
      bool Finish(fn_ty&& fnFinish) {
         return Finalize("Finish", std::forward<fn_ty>(fnFinish));
      }

      template <class fn_ty>
      bool ExpectSuccess(std::string_view const svName, fn_ty&& fnTest) {
         if (!CanExecuteTest_()) {
            return false;
         }

         return ExecuteExpectedSuccess_(test_case_kind::expected_success, svName,
            std::forward<fn_ty>(fnTest), "Operation completed successfully.");
      }

      template <class fn_ty>
      bool ExpectError(std::string_view const svName, fn_ty&& fnTest) {
         if (!CanExecuteTest_()) {
            return false;
         }

         Begin_(test_case_kind::expected_error, svName);

         try {
            fnTest();
            Complete_(false, "Expected an exception, but the operation completed successfully.");
            return false;
         }
         catch (test_assertion_error const& ex) {
            Complete_(false, "Test assertion failed.", ex.what());
            return false;
         }
         catch (std::exception const& ex) {
            ExpectedError(svName);
            Complete_(true, "Expected exception was thrown.", ex.what());
            return true;
         }
         catch (...) {
            Complete_(true, "Expected non-standard exception was thrown.", "<non-standard exception>");
            return true;
         }
      }

      [[nodiscard]] bool IsInitialized() const noexcept {
         return bInitialized_;
      }

      [[nodiscard]] bool IsInitializePassed() const noexcept {
         return bInitialized_ && boInitializePassed_;
      }

      [[nodiscard]] bool IsFinalized() const noexcept {
         return bFinalized_;
      }

      [[nodiscard]] bool IsFinalizePassed() const noexcept {
         return bFinalized_ && boFinalizePassed_;
      }

      [[nodiscard]] bool CanExecuteTests() const noexcept {
         return CanExecuteTest_();
      }

      [[nodiscard]] std::vector<test_result> const& Results() const noexcept {
         return vecResults_;
      }

      [[nodiscard]] std::vector<test_result> const& TestResults() const noexcept {
         return vecResults_;
      }

      [[nodiscard]] int PassedCount() const {
         return adecc::test::PassedCount(vecResults_);
      }

      [[nodiscard]] int FailedCount() const {
         return adecc::test::FailedCount(vecResults_);
      }

      void Summary(std::ostream& out) const {
         adecc::test::Summary(out, strTitle_, vecResults_);
      }

      void Log(std::string_view const svText) {
         if (optActiveIndex_) {
            AppendStatusText_(vecResults_.at(*optActiveIndex_), svText);
         }
      }

      void Success(std::string_view const svName) {
         Log(std::format("OK: {}", svName));
      }

      [[noreturn]] void Failure(std::string_view const svName, std::string const& strMessage) {
         auto const strFullMessage = std::format("ERROR: {}\n{}", svName, strMessage);
         Log(strFullMessage);
         throw test_assertion_error{ strFullMessage };
      }

      void ExpectedError(std::string_view const svName) {
         Log(std::format("Expected error captured in {}", svName));
      }

      template <class left_ty, class right_ty>
      void ExpectEqual(std::string_view const svName,
         left_ty const& aLeft,
         right_ty const& aRight) {
         auto const& aNormLeft = NormalizeExpectedValue(aLeft);
         auto const& aNormRight = NormalizeExpectedValue(aRight);

         if (aNormLeft == aNormRight) {
            Success(svName);
         }
         else {
            Failure(svName, std::format("Expected '{}', got '{}'", aNormRight, aNormLeft));
         }
      }

   private:
      [[nodiscard]] bool CanExecuteTest_() const noexcept {
         return !bInitialized_ || boInitializePassed_;
      }

      template <class fn_ty>
      bool ExecuteExpectedSuccess_(test_case_kind const aKind,
         std::string_view const svName,
         fn_ty&& fnTest,
         std::string_view const svSuccessText) {
         Begin_(aKind, svName);

         try {
            fnTest();
            Complete_(true, svSuccessText);
            return true;
         }
         catch (test_assertion_error const& ex) {
            Complete_(false, "Test assertion failed.", ex.what());
            return false;
         }
         catch (std::exception const& ex) {
            Complete_(false, "Unexpected exception was thrown.", ex.what());
            return false;
         }
         catch (...) {
            Complete_(false, "Unexpected non-standard exception was thrown.", "<non-standard exception>");
            return false;
         }
      }

      void Begin_(test_case_kind const aKind, std::string_view const svName) {
         vecResults_.push_back(test_result{
            .aType = aKind,
            .strName = std::string{svName},
            .aStartedAt = std::chrono::system_clock::now(),
            .iDurationMs = 0,
            .boPassed = false,
            .strStatusText = {},
            .strExceptionText = {}
            });

         optActiveIndex_ = vecResults_.size() - 1;
         optActiveStart_ = std::chrono::steady_clock::now();
      }

      void Complete_(bool const boPassed,
         std::string_view const svStatusText,
         std::string_view const svExceptionText = {}) {
         if (!optActiveIndex_) {
            return;
         }

         auto& aResult = vecResults_.at(*optActiveIndex_);
         aResult.boPassed = boPassed;
         aResult.iDurationMs = ActiveDurationMs_();

         if (!svStatusText.empty()) {
            AppendStatusText_(aResult, svStatusText);
         }

         aResult.strExceptionText = std::string{ svExceptionText };
         optActiveIndex_.reset();
         optActiveStart_.reset();
      }

      [[nodiscard]] long long ActiveDurationMs_() const {
         if (!optActiveStart_) {
            return 0;
         }

         return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - *optActiveStart_).count();
      }

      static void AppendStatusText_(test_result& aResult, std::string_view const svText) {
         if (!aResult.strStatusText.empty()) {
            aResult.strStatusText += '\n';
         }

         aResult.strStatusText += svText;
      }

      template <class value_ty>
      static constexpr bool IsSingleTupleValue_() {
         using clean_ty = std::remove_cvref_t<value_ty>;

         if constexpr (adecc::is_tuple_like_v<clean_ty>) {
            return std::tuple_size_v<clean_ty> == 1;
         }
         else {
            return false;
         }
      }

      template <class value_ty>
      static decltype(auto) NormalizeExpectedValue(value_ty const& aValue) {
         if constexpr (IsSingleTupleValue_<value_ty>()) {
            return std::get<0>(aValue);
         }
         else {
            return (aValue);
         }
      }

   private:
      std::string strTitle_{};
      std::vector<test_result> vecResults_{};
      std::optional<std::size_t> optActiveIndex_{};
      std::optional<std::chrono::steady_clock::time_point> optActiveStart_{};
      bool bInitialized_ = false;
      bool boInitializePassed_ = true;
      bool bFinalized_ = false;
      bool boFinalizePassed_ = true;
   };

   inline void Summary(std::ostream& out, test_run const& theRun) {
      Summary(out, theRun.Title(), theRun.Results());
   }

   inline void PrintResults(std::ostream& theOut, std::vector<test_result> const& vecResults) {
      for (auto const& aResult : vecResults) {
         std::println(theOut, "{}\t{}\t{}\t{}\t{}",
            ToText(aResult.aType),
            aResult.strName,
            FormatTimestamp(aResult.aStartedAt),
            aResult.iDurationMs,
            aResult.boPassed ? "yes" : "no"
         );
      }
   }

   inline void PrintResults(std::ostream& theOut, test_run const& theRun) {
      PrintResults(theOut, theRun.Results());
   }

   inline void PrintExceptionResults(std::ostream& theOut, std::vector<test_result> const& vecResults) {
      bool const boHasExceptions = std::ranges::any_of(vecResults, [](test_result const& aResult) {
         return !aResult.strExceptionText.empty();
         });

      if (!boHasExceptions) {
         return;
      }

      std::println(theOut, "");
      std::println(theOut, "Exception details:");

      for (auto const& aResult : vecResults) {
         if (aResult.strExceptionText.empty()) {
            continue;
         }

         std::println(theOut, "");
         std::println(theOut, "Type: {}", ToText(aResult.aType));
         std::println(theOut, "Name: {}", aResult.strName);
         std::println(theOut, "Started at: {}", FormatTimestamp(aResult.aStartedAt));
         std::println(theOut, "Duration ms: {}", aResult.iDurationMs);
         std::println(theOut, "Passed: {}", aResult.boPassed ? "yes" : "no");
         std::println(theOut, "Exception:");
         std::println(theOut, "{}", aResult.strExceptionText);
      }
   }

   inline void PrintExceptionResults(std::ostream& theOut, test_run const& theRun) {
      PrintExceptionResults(theOut, theRun.Results());
   }

   template <size_t Index, class value_ty>
   decltype(auto) ExtractScalar(value_ty&& aValue) {
      if constexpr (adecc::is_tuple_like_v<adecc::remove_cvref_t<value_ty>>) {
         return std::get<Index>(std::forward<value_ty>(aValue));
      }
      else {
         return std::forward<value_ty>(aValue);
      }
   }

   template <adecc::db::logical_database_type db_ty>
   void ExecuteStatement(db_ty& db, std::string const& strSql, adecc::db_params const& vecParams = {}) {
      if constexpr (requires { db.ExecuteCommand(strSql, vecParams); }) {
         db.ExecuteCommand(strSql, vecParams);
      }
      else if constexpr (requires { db.ExecuteCommand(strSql); }) {
         if (!vecParams.empty()) {
            auto aResult = db.Execute(strSql, vecParams);

            if constexpr (requires { aResult.begin(); aResult.end(); }) {
               for (auto const& aRow : aResult) {
                  (void)aRow;
               }
            }
            else {
               (void)aResult;
            }
         }
         else {
            db.ExecuteCommand(strSql);
         }
      }
      else {
         auto aResult = db.Execute(strSql, vecParams);

         if constexpr (requires { aResult.begin(); aResult.end(); }) {
            for (auto const& aRow : aResult) {
               (void)aRow;
            }
         }
         else {
            (void)aResult;
         }
      }
   }

   template <class result_ty, adecc::db::logical_database_type db_ty>
   std::vector<result_ty> SelectValues(db_ty& db, std::string const& strSql, adecc::db_params const& vecParams = {}) {
      std::vector<result_ty> vecValues;

      for (auto const& aRow : db.template Execute<result_ty>(strSql, vecParams)) {
         vecValues.emplace_back(ExtractScalar<0>(aRow));
      }

      return vecValues;
   }

   template <class result_ty, adecc::db::logical_database_type db_ty>
   result_ty SelectSingleValue(db_ty& db, std::string const& strSql, adecc::db_params const& vecParams = {}) {
      auto vecValues = SelectValues<result_ty>(db, strSql, vecParams);

      if (vecValues.size() != 1) {
         throw std::runtime_error{
            std::format("Expected exactly one result row, got {}", vecValues.size())
         };
      }

      return vecValues.front();
   }

   template <adecc::db::logical_database_type db_ty>
   int SelectCount(db_ty& db, std::string const& strSql, adecc::db_params const& vecParams = {}) {
      return SelectSingleValue<int>(db, strSql, vecParams);
   }

   template <class value_ty>
   constexpr bool IsSingleTupleValue_() {
      using clean_ty = std::remove_cvref_t<value_ty>;

      if constexpr (adecc::is_tuple_like_v<clean_ty>) {
         return std::tuple_size_v<clean_ty> == 1;
      }
      else {
         return false;
      }
   }

   template <class value_ty>
   decltype(auto) NormalizeExpectedValue(value_ty const& aValue) {
      if constexpr (IsSingleTupleValue_<value_ty>()) {
         return std::get<0>(aValue);
      }
      else {
         return (aValue);
      }
   }

   template <class left_ty, class right_ty>
   void ExpectEqual(test_run& theRun,
      std::string_view const svName,
      left_ty const& aLeft,
      right_ty const& aRight) {
      theRun.ExpectEqual(svName, aLeft, aRight);
   }

} // namespace adecc::test

namespace database_error_scenario = adecc::test;

namespace std {

   template <>
   struct tuple_size<adecc::test::test_result> : integral_constant<size_t, 7> {
   };

   template <>
   struct tuple_element<0, adecc::test::test_result> {
      using type = adecc::test::test_case_kind;
   };

   template <>
   struct tuple_element<1, adecc::test::test_result> {
      using type = string;
   };

   template <>
   struct tuple_element<2, adecc::test::test_result> {
      using type = std::chrono::system_clock::time_point;
   };

   template <>
   struct tuple_element<3, adecc::test::test_result> {
      using type = long long;
   };

   template <>
   struct tuple_element<4, adecc::test::test_result> {
      using type = bool;
   };

   template <>
   struct tuple_element<5, adecc::test::test_result> {
      using type = string;
   };

   template <>
   struct tuple_element<6, adecc::test::test_result> {
      using type = string;
   };

} // namespace std
