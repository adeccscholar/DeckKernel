// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file extented_test_types.h
\brief Type lists, metadata, and persistent domain classes for the accounting integration model.

\details
Declares the typed structures for account classes, account types, accounts, tax classes, and postings,
together with persistence metadata and projections. The file demonstrates how relational rows become
domain-oriented C++ values while retaining tuple compatibility.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Type Lists as Structured Type Relations".
- "SystemData as an Application of the Type List".
- "PersistentSystemData as an Extension of SystemData".
- "Practical Example: Financial Data as a Typed Data Flow".

\see ../ARCHITECTURE.md#type-lists-tuples-and-systemdata

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
#include "fixed_numeric.h"

#include "system_data_persistent.h"

#include <map>
#include <set>
#include <string_view>
#include <optional>

namespace extented_test {

using account_class_list = adecc::defined_type_list<
   int,              // Account_Class_ID
   std::string,      // Account_Class_Name
   std::string,      // Description
   std::string,      // Account_Class_Code
   std::string,      // Balance_Side
   bool,             // Pnl_Relevant
   bool,             // Balance_Sheet_Relevant
   bool,             // Natural_Debit
   bool,             // Natural_Credit
   int               // Sort_Order
      >;

using account_type_list = adecc::defined_type_list<
   int,              // ID
   std::string,      // Name
   std::string,      // Abbreviation
   std::string       // Notices
   >;

using account_raw_list = adecc::defined_type_list<
   int,                 // 0: account_id
   int,                 // 1: account_no (unique external key)
   std::string,         // 2: account name (unique)
   std::string,         // 3: account type
   std::string,         // 4: category (SK denotes a nominal account)
   std::string,         // 5: currency
   bool                 // 6: active
   >;

using account_list = adecc::defined_type_list<
   int,                 // 0: account_id
   int,                 // 1: account_no (unique external key)
   std::string,         // 2: account name (unique)
   int,                 // 3: account type
   int,                 // 4: account class
   std::string,         // 5: currency
   bool,                // 6: active
   std::string          // 7: description
>;


using tax_class_list = adecc::defined_type_list<
   int,
   std::string,         // tax class
   std::string,         // description
   double,              // percentage
   std::optional<int>,  // debit tax account
   std::optional<int>,  // credit tax account
   bool,                // input-tax eligible
   bool                 // subject to output tax
   >;

using entry_raw_list = adecc::defined_type_list<
   int,                 // posting_id
   std::string,         // posting date, ISO yyyy-mm-dd
   std::string,         // document number
   int,                 // debit account
   int,                 // credit account
   double,              // amount
   std::string,         // tax class
   std::string          // posting text
>;

using entry_list = adecc::defined_type_list<
   int,                 // posting_id
   adecc::date_ty,      // posting date 
   std::string,         // document number
   int,                 // debit account_id
   int,                 // credit account_id
   adecc::money_ty,     // amount
   int,                 // tax class
   std::string          // posting text
>;

using acct_type_ty      = typename account_type_list::type_list;
using acct_class_ty     = typename account_class_list::type_list;
using acct_raw_ty       = typename account_raw_list::type_list;
using acct_ty           = typename account_list::type_list;
using tax_class_ty      = typename tax_class_list::type_list;
using entry_raw_ty      = typename entry_raw_list::type_list;
using entry_ty          = typename entry_list::type_list;

using acct_type_bridge  = std::map<std::string, int>;
using acct_class_bridge = std::map<std::string, int>;
using acct_bridge       = std::map<int, int>;
using tax_class_bridge  = std::map<std::string, int>;



struct AccountTypeMetaData {
   static constexpr std::string_view svTableName { "Test_AccountType" };

   static constexpr std::array<std::string_view, 4> arrAttributeNames{
             "ID", "Denotation", "Abbreviation", "Description" 
         };

   static constexpr std::optional<std::size_t> optIdentityIndex { 0 };

   static constexpr std::array<std::size_t, 1> arrKeyIndices { 0 };

   static constexpr std::array<std::size_t, 0> arrReadOnlyIndices { };
   };

class TAccountType : public adecc::db::PersistentSystemData<account_type_list, AccountTypeMetaData> {
public:
   using base_type = adecc::db::PersistentSystemData<account_type_list, AccountTypeMetaData>;

   using base_type::base_type;
   using base_type::operator=;

   explicit TAccountType(int const iAcctTypeId) {
      Set<0>(iAcctTypeId);
   }

   auto operator <=> (TAccountType const& theOther) const {
      return CompareSelected<0>(theOther.Data());
   }

   bool operator == (TAccountType const& theOther) const {
      return EqualSelected<0>(theOther);
   }

   auto const& ID() const { return Get<0>(); }
   auto const& Denotation() const { return Get<1>(); }
   auto const& Abbreviation() const { return Get<2>(); }
   auto const& Description() const { return Get<3>(); }
};

using act_type_set = std::set<TAccountType>;


struct AccountClassMetaData {
   static constexpr std::string_view svTableName{ "Test_AccountClass" };
   static constexpr std::array<std::string_view, 10> arrAttributeNames {
           "ID", "Denotation", "Description", "Type_Code",
           "Balance_Side", "Pnl_Relevant", "Balance_Sheet_Relevant",
           "Natural_Debit", "Natural_Credit", "Sort_Order"
         };
   static constexpr std::array<std::size_t, 1> arrKeyIndices{ 0 };
   static constexpr std::optional<std::size_t> optIdentityIndex{ 0 };
   static constexpr std::array<std::size_t, 0> arrReadOnlyIndices{ };
};


class TAccountClass : public adecc::db::PersistentSystemData<account_class_list, AccountClassMetaData> {
public:
   using base_type = adecc::db::PersistentSystemData<account_class_list, AccountClassMetaData>;

   using base_type::base_type;
   using base_type::operator=;

   explicit TAccountClass(int const iAcctClassId) {
      Set<0>(iAcctClassId);
   }

   auto operator <=> (TAccountClass const& theOther) const {
      return CompareSelected<0>(theOther.Data());
   }

   bool operator == (TAccountClass const& theOther) const {
      return EqualSelected<0>(theOther);
   }

   auto const& ID() const { return Get<0>(); }
   auto const& Denotation() const { return Get<1>(); }
   auto const& Description() const { return Get<2>(); }
   auto const& Type_Code() const { return Get<3>(); }
   auto const& Balance_Side() const { return Get<4>(); }
   auto const& Pnl_Relevant() const { return Get<5>(); }
   auto const& Balance_Sheet_Relevant() const { return Get<6>(); }
   auto const& Natural_Debit() const { return Get<7>(); }
   auto const& Natural_Credit() const { return Get<8>(); }
   auto const& Sort_Order() const { return Get<9>(); }
};

using act_class_set = std::set<TAccountClass>;

struct AccountMetaData {
   static constexpr std::string_view svTableName { "Test_Account" };

   static constexpr std::array<std::string_view, 4> arrAttributeNames{
          "ID", "Account_No", "Denotation", "Account_Type"
      };

   static constexpr std::array<std::size_t, 1> arrKeyIndices { 0 };
   static constexpr std::optional<std::size_t> optIdentityIndex { 0 };
   static constexpr std::array<std::size_t, 0> arrReadOnlyIndices { };
};


struct NominalAccountMetaData {
   static constexpr std::string_view svTableName { "Test_NominalAcct" };

   static constexpr std::array<std::string_view, 5> arrAttributeNames{
         "ID", "Account_Class", "Currency", "IsActive", "Description"
      };

   static constexpr std::array<std::size_t, 1> arrKeyIndices { 0 };
   static constexpr std::optional<std::size_t> optIdentityIndex { std::nullopt };
   static constexpr std::array<std::size_t, 0> arrReadOnlyIndices { };
   };

using AccountBaseProj =
        adecc::db::persistent_meta_projection<std::index_sequence<0, 1, 2, 3>,
                                              AccountMetaData>;

using NominalAcctProj =
        adecc::db::persistent_meta_projection<std::index_sequence<0, 4, 5, 6, 7>,
                                              NominalAccountMetaData>;

class TAccount : public adecc::db::PersistentSystemData<account_list,
                                                        AccountBaseProj,
                                                        NominalAcctProj> {
public:
   using base_type = adecc::db::PersistentSystemData<account_list,
                                                     AccountBaseProj,
                                                     NominalAcctProj>;

   using base_type::base_type;
   using base_type::operator=;

   explicit TAccount(int const iAcctId) {
      Set<0>(iAcctId);
   }

   auto operator <=> (TAccount const& theOther) const {
      return CompareSelected<0>(theOther.Data());
   }

   bool operator == (TAccount const& theOther) const {
      return EqualSelected<0>(theOther);
   }

   auto const& ID() const { return Get<0>(); }
   auto const& Account_No() const { return Get<1>(); }
   auto const& Denotation() const { return Get<2>(); }
   auto const& Account_Type() const { return Get<3>(); }
   auto const& Account_Class() const { return Get<4>(); }
   auto const& Currency() const { return Get<5>(); }
   auto const& IsActive() const { return Get<6>(); }
   auto const& Description() const { return Get<7>(); }

   template <typename val_ty> requires std::assignable_from<element_ty<0>&, val_ty>
   void ID(val_ty&& theValue) { Set<0>(std::forward<val_ty>(theValue)); }

   template <typename val_ty> requires std::assignable_from<element_ty<1>&, val_ty>
   void Account_No(val_ty&& theValue) { Set<1>(std::forward<val_ty>(theValue)); }

   template <typename val_ty> requires std::assignable_from<element_ty<2>&, val_ty>
   void Denotation(val_ty&& theValue) { Set<2>(std::forward<val_ty>(theValue)); }

   template <typename val_ty> requires std::assignable_from<element_ty<3>&, val_ty>
   void Account_Type(val_ty&& theValue) { Set<3>(std::forward<val_ty>(theValue)); }

   template <typename val_ty> requires std::assignable_from<element_ty<4>&, val_ty>
   void Account_Class(val_ty&& theValue) { Set<4>(std::forward<val_ty>(theValue)); }

   template <typename val_ty> requires std::assignable_from<element_ty<5>&, val_ty>
   void Currency(val_ty&& theValue) { Set<5>(std::forward<val_ty>(theValue)); }

   template <typename val_ty> requires std::assignable_from<element_ty<6>&, val_ty>
   void IsActive(val_ty&& theValue) { Set<6>(std::forward<val_ty>(theValue)); }


   template <typename val_ty> requires std::assignable_from<element_ty<7>&, val_ty> 
   void Description(val_ty&& theValue) { Set<7>(std::forward<val_ty>(theValue)); }

};

using act_set = std::set<TAccount>;



struct TaxClassMetaData {
   static constexpr std::string_view svTableName{ "Test_TaxClass" };
   static constexpr std::array<std::string_view, 8> arrAttributeNames{
      "ID", "Tax_Class", "Description", "Tax_Percent", "Tax_Acct_Debit",
      "Tax_Acct_Credit", "Eligible_for_Tax", "Subject_to_Tax" };
   static constexpr std::array<std::size_t, 1> arrKeyIndices{ 0 };
   static constexpr std::optional<std::size_t> optIdentityIndex{ 0 };
   static constexpr std::array<std::size_t, 0> arrReadOnlyIndices{ };
};


class TTaxClass : public adecc::db::PersistentSystemData<tax_class_list, TaxClassMetaData> {
public:
   using base_type = adecc::db::PersistentSystemData<tax_class_list, TaxClassMetaData>;

   using base_type::base_type;
   using base_type::operator=;

   explicit TTaxClass(int const iTaxClassId) {
      Set<0>(iTaxClassId);
   }

   auto operator <=> (TTaxClass const& theOther) const {
      return CompareSelected<0>(theOther.Data());
   }

   bool operator == (TTaxClass const& theOther) const {
      return EqualSelected<0>(theOther);
   }

   auto const& ID() const { return Get<0>(); }
   auto const& Tax_Class() const { return Get<1>(); }
   auto const& Description() const { return Get<2>(); }
   auto const& Tax_Percent() const { return Get<3>(); }
   auto const& Tax_Acct_Debit() const { return Get<4>(); }
   auto const& Tax_Acct_Credit() const { return Get<5>(); }
   auto const& Eligible_for_Tax() const { return Get<6>(); }
   auto const& Subject_to_Tax() const { return Get<7>(); }
};

using tax_class_set = std::set<TTaxClass>;


} // namespace extented_test 
