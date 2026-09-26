// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file persistent_insert_complete.h
\brief Lazy range-based insertion machinery used to test complete persistent data flows.

\details
Implements transformation views and dispatch helpers that convert input ranges into persistent output ranges
without forcing eager materialization. The test utility explores how lazy range composition can replace
bespoke insertion loops while preserving typed context.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "PersistentSystemData as an Extension of SystemData".
- "Ranges, Lazy Processing, and Materialization".
- "Tests as Architectural Proof".
- "Migration as an Architecture Test".

\see ../ARCHITECTURE.md#persistentsystemdata

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

#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace extented_test {

   /*!\brief Identity transformation for already compatible persistent data types. */
   template <class value_ty>
   [[nodiscard]]
   value_ty IdentityTransform(value_ty const& entry) {
      return entry;
   }

   /*!\brief Applies a transformation function with a context tuple. */
   template <
      class output_value_ty,
      class input_value_ty,
      class transform_fn_ty,
      class context_tuple_ty
   >
   [[nodiscard]]
   output_value_ty TransformPersistentInput(
      input_value_ty const& entry,
      transform_fn_ty& Transform,
      context_tuple_ty const& tupContext
   ) {
      return std::apply(
         [&](auto const&... contextValues) -> output_value_ty {
            return std::invoke(
               Transform,
               entry,
               contextValues...
            );
         },
         tupContext
      );
   }

   /*!\brief Transforms an optional external tax account into an internal account ID. */
   [[nodiscard]]
   inline std::optional<int> TransformOptionalAccountNumber(
      std::optional<int> const& optAccountNo,
      acct_bridge const& accounts
   ) {
      return optAccountNo
         ? std::make_optional(AccountNumber_ID(accounts, *optAccountNo))
         : std::nullopt;
   }

  

   /*!\brief Lazy insert view without a coroutine. */
   template <
      class output_range_ty,
      class input_range_ty,
      class input_value_ty,
      class output_value_ty,
      class transform_fn_ty,
      class context_tuple_ty
   >
   class persistent_insert_view
      : public std::ranges::view_interface<
      persistent_insert_view<
      output_range_ty,
      input_range_ty,
      input_value_ty,
      output_value_ty,
      transform_fn_ty,
      context_tuple_ty
      >
      > {
   private:
      struct state_ty {
         state_ty(
            output_range_ty aOutputRange,
            input_range_ty rngInput,
            transform_fn_ty Transform,
            context_tuple_ty tupContext
         )
            : aOutputRange{ std::move(aOutputRange) },
            rngInput{ std::move(rngInput) },
            Transform{ std::move(Transform) },
            tupContext{ std::move(tupContext) } {
         }

         output_range_ty aOutputRange;
         input_range_ty rngInput;
         transform_fn_ty Transform;
         context_tuple_ty tupContext;
      };

   public:
      persistent_insert_view() = default;

      persistent_insert_view(
         output_range_ty aOutputRange,
         input_range_ty rngInput,
         transform_fn_ty Transform,
         context_tuple_ty tupContext
      )
         : pState_{
              std::make_shared<state_ty>(
                 std::move(aOutputRange),
                 std::move(rngInput),
                 std::move(Transform),
                 std::move(tupContext)
              )
         } {
      }

      class iterator {
      public:
         using iterator_concept = std::input_iterator_tag;
         using iterator_category = std::input_iterator_tag;
         using value_type = output_value_ty;
         using difference_type = std::ptrdiff_t;
         using reference = output_value_ty const&;

         iterator() = default;

         explicit iterator(std::shared_ptr<state_ty> pState)
            : pState_{ std::move(pState) } {
            if (pState_) {
               itCurrent_ = std::ranges::begin(pState_->rngInput);
               itEnd_ = std::ranges::end(pState_->rngInput);
               ReadCurrent_();
            }
         }

         reference operator*() const {
            return *optCurrent_;
         }

         output_value_ty const* operator->() const {
            return std::addressof(*optCurrent_);
         }

         iterator& operator++() {
            ++itCurrent_;
            ReadCurrent_();
            return *this;
         }

         void operator++(int) {
            ++(*this);
         }

         friend bool operator==(iterator const& it, std::default_sentinel_t) {
            return !it.optCurrent_.has_value();
         }

         friend bool operator!=(iterator const& it, std::default_sentinel_t s) {
            return !(it == s);
         }

      private:
         void ReadCurrent_() {
            if (!pState_ || itCurrent_ == itEnd_) {
               optCurrent_.reset();
               return;
            }

            output_value_ty tupInput =
               TransformPersistentInput<output_value_ty>(
                  *itCurrent_,
                  pState_->Transform,
                  pState_->tupContext
               );

            optCurrent_ =
               pState_->aOutputRange.ExecuteOne(
                  std::move(tupInput)
               );
         }

      private:
         std::shared_ptr<state_ty> pState_{};
         std::ranges::iterator_t<input_range_ty> itCurrent_{};
         std::ranges::sentinel_t<input_range_ty> itEnd_{};
         std::optional<output_value_ty> optCurrent_{};
      };

      iterator begin() {
         return iterator{ pState_ };
      }

      std::default_sentinel_t end() const noexcept {
         return {};
      }

   private:
      std::shared_ptr<state_ty> pState_{};
   };

   /*!\brief Primary dispatch template. */
   template <
      class data_ty,
      class input_list_ty,
      class output_list_ty
   >
   struct persistent_insert_view_dispatch;

   /*!\brief Dispatch for adecc::defined_type_list<Ts...>. */
   template <
      class data_ty,
      class input_list_ty,
      class... Ts
   >
   struct persistent_insert_view_dispatch<
      data_ty,
      input_list_ty,
      adecc::defined_type_list<Ts...>
   > {
      using input_value_ty = typename input_list_ty::type_list;
      using output_value_ty = typename data_ty::data_ty;

      template <
         class app_db,
         class input_range_ty,
         class transform_fn_ty,
         class context_tuple_ty
      >
      [[nodiscard]]
      static auto Make(
         app_db& db,
         input_range_ty rngInput,
         transform_fn_ty Transform,
         context_tuple_ty tupContext
      ) {
         auto aPersistentClassRange =
            db.template MakeOutputRange<Ts...>(
               data_ty::CreateInsertSql(),
               data_ty::CreateInsertOutputParameters()
            );

         using output_range_ty = decltype(aPersistentClassRange);

         return persistent_insert_view<
            output_range_ty,
            input_range_ty,
            input_value_ty,
            output_value_ty,
            transform_fn_ty,
            context_tuple_ty
         > {
            std::move(aPersistentClassRange),
               std::move(rngInput),
               std::move(Transform),
               std::move(tupContext)
         };
      }
   };

   /*!\brief Creates a lazy insert range without a coroutine. */
   template <
      class data_ty,
      class input_list_ty,
      class app_db,
      class input_range_ty,
      class transform_fn_ty,
      class... context_ty
   >
      requires adecc::db::logical_database_type<app_db>
   && adecc::db::persistent_insert_input<
      data_ty,
      input_list_ty,
      input_range_ty,
      transform_fn_ty,
      context_ty...
   >
      [[nodiscard]]
   auto InsertPersistentClass(
      app_db& db,
      input_range_ty&& rngInputData,
      transform_fn_ty Transform,
      context_ty const&... context
   ) {
      auto rngInput =
         std::views::all(std::forward<input_range_ty>(rngInputData));

      auto tupContext =
         std::tuple<context_ty const&...>{
            context...
      };

      return persistent_insert_view_dispatch<
         data_ty,
         input_list_ty,
         typename data_ty::types_list
      >::Make(
         db,
         std::move(rngInput),
         std::move(Transform),
         std::move(tupContext)
      );
   }

   /*!\brief Direct debug insert without a lazy range. */
   template <
      class data_ty,
      adecc::db::logical_database_type app_db,
      std::ranges::input_range range_ty
   >
      requires std::same_as<
         std::remove_cvref_t<std::ranges::range_value_t<range_ty>>,
            typename data_ty::data_ty
      >
   [[nodiscard]]
   std::vector<typename data_ty::data_ty> DebugInsertAlreadyPersistentWithExecuteOne(
      app_db& db,
      range_ty&& rngInputData
   ) {
      using output_value_ty = typename data_ty::data_ty;

      std::vector<output_value_ty> vecInserted;

      if constexpr (std::ranges::sized_range<range_ty>) {
         vecInserted.reserve(std::ranges::size(rngInputData));
      }

      data_ty::types_list::invoke([&]<class... Ts>() {
         auto aPersistentClassRange =
            db.template MakeOutputRange<Ts...>(
               data_ty::CreateInsertSql(),
               data_ty::CreateInsertOutputParameters()
            );

         for (output_value_ty const& entry : rngInputData) {
            vecInserted.push_back(
               aPersistentClassRange.ExecuteOne(entry)
            );
         }
      });

      return vecInserted;
   }

} // namespace extented_test
