// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file extented_test_details.h
\brief Typed transformations and persistence helpers used by the extended accounting scenarios.

\details
Connects raw accounting input to persistent domain tuples, generated identities, bridge maps, posting output
ranges, and balance calculations. The functions show how transformations remain explicit while ranges carry
data between sources, domain structures, and sinks.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Practical Example: Financial Data as a Typed Data Flow".
- "Tests as Architectural Proof".
- "Data Movement Between Source and Sink".
- "Migration as an Architecture Test".

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

#include "database.h"
#include "system_data_persistent_executor.h"
#include "type_lists.h"
#include "fixed_numeric.h"
#include "convert_fixed.h"

#include "extented_test_types.h"
#include "extented_test_helper.h"
#include "database_test_dialects.h"

#include "generator.h"

#include <iostream>
#include <string>
#include <map>
#include <algorithm>
#include <numeric>
#include <print>

namespace extented_test {

   // -------------------------------------------------------------------------------------------------
   //     Insert accounts into the database
   // -------------------------------------------------------------------------------------------------
 // -------------------------------------------------------------------------------------------------
   //     Insert tax classes through the generic persistence path
   // -------------------------------------------------------------------------------------------------
 
   template <class value_ty>
   [[nodiscard]] value_ty IdentityTransform(value_ty const& entry) {
      return entry;
      }
   
   [[nodiscard]] inline tax_class_ty TransformTaxClass(tax_class_ty const& entry, acct_bridge const& accounts) {
      return tax_class_ty { std::get<0>(entry), std::get<1>(entry), std::get<2>(entry), std::get<3>(entry),
                BridgeValue(accounts, std::get<4>(entry)),
                BridgeValue(accounts, std::get<5>(entry)),
                std::get<6>(entry), std::get<7>(entry) };
      }

   [[nodiscard]] inline acct_ty TransformAccount(acct_raw_ty const& entry,
                                                 acct_type_bridge const& acctTypes,
                                                 acct_class_bridge const& acctClasses) {
      return acct_ty { std::get<0>(entry), std::get<1>(entry), std::get<2>(entry),
                BridgeValue(acctTypes, std::get<4>(entry)),
                BridgeValue(acctClasses, std::get<3>(entry)), 
                std::get<5>(entry), std::get<6>(entry), ""s };
      }

   template <adecc::db::persistent_system_data_type data_ty,
             adecc::defined_type_list_ty input_list_ty,
             adecc::db::logical_database_type app_db,
             std::ranges::input_range input_range_ty,
             typename transform_fn_ty,
             typename... context_ty>
      requires adecc::db::persistent_insert_input<data_ty, input_list_ty, input_range_ty,
                                                  transform_fn_ty, context_ty...>
   adecc::Generator<typename data_ty::data_ty> InsertPersistent(app_db& db, input_range_ty&& rngInputData,
                                              transform_fn_ty Transform, context_ty const&... context) {
      using input_value_ty = typename input_list_ty::type_list;
      using output_value_ty = typename data_ty::data_ty;

      auto rngInput   = std::views::all(std::forward<input_range_ty>(rngInputData));
      auto aTransform = std::move(Transform);
      auto tupContext = std::tuple<context_ty const&...> { context... };

      for (input_value_ty const& tplInput : rngInput) {
         output_value_ty tplData = std::apply([&](auto const&... contextValues) -> output_value_ty {
                  return std::invoke(aTransform, tplInput, contextValues...);
                  }, tupContext);

         co_yield adecc::db::PersistentDataExecutor::template Insert<data_ty>(db, std::move(tplData));
         }
      co_return;
      }

   // ----------------------------------------------------------------------------------------
   //
   // ----------------------------------------------------------------------------------------

   template <adecc::db::logical_database_type app_db>
   [[nodiscard]] auto MakePostingOutputRange(app_db& db) {
      return entry_list::invoke([&]<class... Ts>() {
         return db.template MakeOutputRange<Ts...>(
            "INSERT INTO Test_Posting (Booking_Date, Booking_No, Debit_Acct_ID, "
            "Credit_Acct_ID, Amount, Tax_Class, Posting_Text) "
            "VALUES (:Booking_Date, :Booking_No, :Debit_Acct_ID, :Credit_Acct_ID, "
            ":Amount, :Tax_Class, :Posting_Text)",
            { { "ID",             adecc::db_output_param_role::identity },
              { "Booking_Date",   adecc::db_output_param_role::needed_value },
              { "Booking_No",     adecc::db_output_param_role::needed_value },
              { "Debit_Acct_ID",  adecc::db_output_param_role::needed_value },
              { "Credit_Acct_ID", adecc::db_output_param_role::needed_value },
              { "Amount",         adecc::db_output_param_role::needed_value },
              { "Tax_Class",      adecc::db_output_param_role::needed_value },
              { "Posting_Text",   adecc::db_output_param_role::needed_value } }
         );
      });
   }

   // ----------------------------------------------------------------------------------------
   //
   // ----------------------------------------------------------------------------------------
   template <adecc::db::logical_database_type app_db,
             std::ranges::input_range entry_raw_range_ty>
      requires std::same_as<std::ranges::range_value_t<entry_raw_range_ty>, entry_raw_ty>
   std::vector<entry_ty> InsertPostings(app_db& db, entry_raw_range_ty&& data,
                                        acct_bridge const& accounts, tax_class_bridge const& taxes) {
      std::vector<entry_ty> vecInserted;

      auto aPostingRange = MakePostingOutputRange(db);

      auto input = std::forward<entry_raw_range_ty>(data)
                          | std::views::transform([&](entry_raw_ty const& entry) {
                               return entry_ty {
                                     std::get<0>(entry),
                                     adecc::ConvertTo<adecc::date_ty>(std::get<1>(entry)),
                                     std::get<2>(entry),
                                     BridgeValue(accounts, std::get<3>(entry)),
                                     BridgeValue(accounts, std::get<4>(entry)),
                                     std::get<5>(entry),
                                     BridgeValue(taxes, std::get<6>(entry)),
                                     std::get<7>(entry)
                                     };
                               });

      std::ranges::move(aPostingRange(input), std::back_inserter(vecInserted));
      return vecInserted;
      }



   // -------------------------------------------------------------------------------------------------
   //     Calculate account balances for a year
   // -------------------------------------------------------------------------------------------------
   template <adecc::db::logical_database_type app_db>
   std::map<int, adecc::money_ty> CalculateBalanceOfAccountByYear(
      app_db& db,
      int const iYear,
      adecc::test::database_dialect const aDialect = adecc::test::database_dialect::sql_server) {
      using bal_by_year_list = adecc::defined_type_list <int, int, adecc::money_ty>;
      std::string const strBalByAcct =
         "SELECT Debit_Acct_ID, Credit_Acct_ID, Amount "
         "FROM Test_Posting WHERE " +
         std::string{ adecc::test::YearFilterExpression(aDialect) };
      adecc::db_params params = { { "ByYear", iYear, true  } };

      return bal_by_year_list::invoke([&]<class... Ts>() {
         return std::ranges::fold_left(
                 db.template Execute<Ts...>(strBalByAcct, params),
                 std::map<int, adecc::money_ty> {},
                 [](std::map<int, adecc::money_ty> mpBalByAcct, std::tuple<Ts...> const& entry) {
                       auto const& [debit_acct, credit_acct, amount] = entry;
                       mpBalByAcct[debit_acct] += amount;
                       mpBalByAcct[credit_acct] -= amount;
                       return mpBalByAcct;
                       });
         });
      }


} // namespace extented_test