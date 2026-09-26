// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file type_lists.h
\brief Compile-time type-list infrastructure connecting type sequences, tuples, variants, and transformations.

\details
Defines the structured type relations used throughout the library to describe valid sequences of value
types. It supports invocation over type packs, tuple and variant formation, compatibility checks,
projections, and element-wise conversion into target type spaces.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Type Space, Core, and Boundary: The Conceptual Framework".
- "Tuple Calculus and Concepts".
- "Type Lists as Structured Type Relations".
- "Compile-Time Transformations as Algebra".

\see ARCHITECTURE.md#type-space-core-and-boundary

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

#include "type_traits_ext.h"
#include "convert_core.h"

#include <tuple>
#include <type_traits>
#include <variant>
#include <optional>
#include <ranges>
#include <concepts>
#include <utility>

namespace adecc {

   /**
     \brief Helper alias for \c remove_cvref_t
   */
   template <class ty>
   using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<ty>>;

   namespace details {

      template <typename target_ty, typename source_ty>
      [[nodiscard]] constexpr target_ty ConvertTupleValue(source_ty&& aSource) {
         return adecc::ConvertTo<target_ty>(
            std::forward<source_ty>(aSource)
         );
      }

      template <typename target_ty, typename source_ty>
      constexpr void AssignTupleValue(target_ty& aTarget, source_ty&& aSource) {
         aTarget =
            ConvertTupleValue<adecc::remove_cvref_t<target_ty>>(
               std::forward<source_ty>(aSource)
            );
      }

   } // namespace details


   template <class ty>
   concept tuple_like = is_tuple_like_v<ty>;

   template <typename... Types>
   struct defined_type_list {
      using type_list = std::tuple<Types...>;
      using type_variant = std::variant<Types...>;
      using param_type_variant = std::variant<Types..., std::optional<Types>...>;

      template <template<class...> class v_ty>
      using apply = v_ty<Types...>;


      template <class func_ty>
      static decltype(auto) invoke(func_ty&& f)
         requires requires(func_ty&& g) {
         std::forward<func_ty>(g).template operator() <Types...> ();
      } {
         return std::forward<func_ty>(f).template operator() <Types...> ();
      }

      template <class ty, class = void> struct is_tuple_like : std::false_type {};
      template <class ty> struct is_tuple_like<ty, std::void_t<decltype(std::tuple_size<std::remove_cvref_t<ty>>::value)>> : std::true_type {};
      template <class ty> static constexpr bool is_tuple_like_v = is_tuple_like<ty>::value;

      template <class Tup>
      static constexpr bool tuple_compatible_v =
         is_tuple_like_v<std::remove_cvref_t<Tup>> &&
         (std::tuple_size_v<std::remove_cvref_t<Tup>> == sizeof...(Types)) &&
         []<std::size_t... Is>(std::index_sequence<Is...>) {
         using U = std::remove_cvref_t<Tup>;
         return ((adecc::Convertible<std::tuple_element_t<Is, U>, Types> ||
            adecc::StdConvertible<std::tuple_element_t<Is, U>, Types>) && ...);
      }(std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<Tup>>>{});
      template <class Tup>
         requires (tuple_compatible_v<Tup>)
      static auto to_target(Tup&& src) -> type_list {
         return std::apply(
            []<typename... arg_tys>(arg_tys&&... xs) -> type_list {
            return type_list{
               adecc::ConvertTo<std::remove_cvref_t<Types>>(
                  std::forward<arg_tys>(xs)
               )...
            };
         },
            std::forward<Tup>(src)
         );
      }
   };

   template <typename ty>
   struct is_defined_type_list : std::false_type {};

   template <typename... Types>
   struct is_defined_type_list<defined_type_list<Types...>> : std::true_type {};

   template <typename ty>
   inline constexpr bool is_defined_type_list_v = is_defined_type_list<std::remove_cvref_t<ty>>::value;

   template <typename ty>
   concept defined_type_list_ty = is_defined_type_list_v<ty>;

   template <typename types_ty, typename Tup>
   concept tuple_compatible_v =
      is_tuple_like_v<std::remove_cvref_t<Tup>> &&
      (std::tuple_size_v<std::remove_cvref_t<Tup>> ==
         std::tuple_size_v<typename types_ty::type_list>) &&
      []<std::size_t... uIs>(std::index_sequence<uIs...>) {
      using Src = std::remove_cvref_t<Tup>;
      using Dst = typename types_ty::type_list;
      return ((adecc::Convertible<
         std::tuple_element_t<uIs, Src>,
         std::tuple_element_t<uIs, Dst>> ||
         adecc::StdConvertible<
         std::tuple_element_t<uIs, Src>,
         std::tuple_element_t<uIs, Dst>>) && ...);
   }(std::make_index_sequence<std::tuple_size_v<typename types_ty::type_list>>{});

   template <class TupA, class TupB>
   using tuple_cat_t = decltype(std::tuple_cat(std::declval<TupA>(), std::declval<TupB>()));

   template <class Tup>
   struct tuple_to_defined_type_list;

   template <class... Types>
   struct tuple_to_defined_type_list<std::tuple<Types...>> {
      using type = defined_type_list<Types...>;
   };

   template <class Tup>
   using tuple_to_defined_type_list_t =
      typename tuple_to_defined_type_list<std::remove_cvref_t<Tup>>::type;


   template <class type_list_ty, class Tup, class = void>
   struct is_same_pack : std::false_type {};

   template <class type_list_ty, class Tup>
   struct is_same_pack<type_list_ty, Tup,
      std::enable_if_t<is_tuple_like_v<std::remove_cvref_t<Tup>> &&
      (std::tuple_size_v<typename type_list_ty::type_list> ==
         std::tuple_size_v<std::remove_cvref_t<Tup>>) >> : std::bool_constant <
      []<std::size_t... uIs>(std::index_sequence<uIs...>) {
      using A = typename type_list_ty::type_list;
      using B = std::remove_cvref_t<Tup>;
      return (std::same_as<
         std::remove_cvref_t<std::tuple_element_t<uIs, B>>,
         std::tuple_element_t<uIs, A>> && ...);
   }(std::make_index_sequence<std::tuple_size_v<typename type_list_ty::type_list>>{})
         > {};


   template <class type_list_ty, class Tup>
   inline constexpr bool is_same_pack_v = is_same_pack<type_list_ty, Tup>::value;

   template <class type_list_ty, class... Args>
   inline constexpr bool is_same_args_v = is_same_pack_v<type_list_ty, std::tuple<Args...>>;

   template <typename type_list_ty, typename... Args>
   concept same_defined_type_args = defined_type_list_ty<type_list_ty> &&
      is_same_args_v<type_list_ty, Args...>;


   template <class type_list_ty>
      requires defined_type_list_ty<type_list_ty>
   using defined_type_list_row_t = typename remove_cvref_t<type_list_ty>::type_list;

   template <class type_list_ty, class row_ty>
   struct defined_type_list_row_impl : std::false_type {};

   template <class type_list_ty, class row_ty>
      requires defined_type_list_ty<type_list_ty>
   struct defined_type_list_row_impl<type_list_ty, row_ty> :
      std::bool_constant<adecc::is_same_pack_v<type_list_ty, row_ty>> {
   };

   template <class type_list_ty, class row_ty>
   concept defined_type_list_row = defined_type_list_row_impl<type_list_ty, row_ty>::value;

   template <class input_list_ty, class range_ty>
   concept defined_type_list_input_range = defined_type_list_ty<input_list_ty> &&
      std::ranges::input_range<range_ty> &&
      defined_type_list_row<input_list_ty,
      std::ranges::range_value_t<range_ty>
      >;

   template <class output_list_ty, class range_ty>
   concept defined_type_list_output_range = defined_type_list_ty<output_list_ty> &&
      std::ranges::output_range<range_ty,
      defined_type_list_row_t<output_list_ty>
      >;

   template <class range_ty, class input_list_ty>
   concept source = defined_type_list_input_range<remove_cvref_t<input_list_ty>,
      range_ty
   >;

   template <class range_ty, class output_list_ty>
   concept sink = defined_type_list_output_range<remove_cvref_t<output_list_ty>,
      range_ty
   >;



   // ------------------------------------------------------------

   template <class type_list_ty, class Tup, class = void>
   struct is_convertible_pack : std::false_type {};

   template <class type_list_ty, class Tup>
   struct is_convertible_pack<type_list_ty, Tup,
      std::enable_if_t<
      is_tuple_like_v<std::remove_cvref_t<Tup>> &&
      (std::tuple_size_v<typename type_list_ty::type_list> ==
         std::tuple_size_v<std::remove_cvref_t<Tup>>)
      >
   > : std::bool_constant <
      []<std::size_t... uIs>(std::index_sequence<uIs...>) {
      using A = typename type_list_ty::type_list;
      using B = std::remove_cvref_t<Tup>;
      return ((adecc::Convertible<
         std::tuple_element_t<uIs, B>,
         std::tuple_element_t<uIs, A>> ||
         adecc::StdConvertible<
         std::tuple_element_t<uIs, B>,
         std::tuple_element_t<uIs, A>>) && ...);
   }(std::make_index_sequence<std::tuple_size_v<typename type_list_ty::type_list>>{})
         > {};

   template <class type_list_ty, class Tup>
   inline constexpr bool is_convertible_pack_v =
      is_convertible_pack<type_list_ty, Tup>::value;

   template <class type_list_ty, class... Args>
   inline constexpr bool is_convertible_args_v =
      is_convertible_pack_v<type_list_ty, std::tuple<Args...>>;

   // ------------------------------------------------------------

   template <class... Args>
   concept all_rvalues = (std::is_rvalue_reference_v<Args&&> && ...);

   template <class tuple_ty, std::size_t... Is>
   [[nodiscard]] constexpr auto selectTplValues_(tuple_ty const& tplValue, std::index_sequence<Is...>) {
      return std::tuple{ std::get<Is>(tplValue)... };
   }


   template <class ty>
   struct is_index_sequence : std::false_type {};

   template <std::size_t... Is>
   struct is_index_sequence<std::index_sequence<Is...>> : std::true_type {};


   template <class ty>
   inline constexpr bool is_index_sequence_v = is_index_sequence<std::remove_cvref_t<ty>>::value;

   template <class ty>
   concept index_sequence_ty = is_index_sequence_v<ty>;


   template <class sequence_ty>
   struct index_sequence_size;

   template <std::size_t... Is>
   struct index_sequence_size<std::index_sequence<Is...>>
      : std::integral_constant<std::size_t, sizeof...(Is)> {
   };

   template <class sequence_ty>
      requires index_sequence_ty<sequence_ty>
   inline constexpr std::size_t index_sequence_size_v = index_sequence_size<std::remove_cvref_t<sequence_ty>>::value;


   // ----------------------------------------------------------------------------------------------
   template <class sequence_ty, std::size_t t_uSourceSize>
   struct index_sequence_fits_size : std::false_type {};

   template <std::size_t t_uSourceSize, std::size_t... Is>
   struct index_sequence_fits_size<std::index_sequence<Is...>, t_uSourceSize>
      : std::bool_constant<((Is < t_uSourceSize) && ...)> {};

   template <class sequence_ty, std::size_t t_uSourceSize>
      requires index_sequence_ty<sequence_ty>
   inline constexpr bool index_sequence_fits_size_v =
      index_sequence_fits_size<std::remove_cvref_t<sequence_ty>, t_uSourceSize>::value;

   template <class sequence_ty>
   concept non_empty_index_sequence_ty = index_sequence_ty<sequence_ty> &&
      (index_sequence_size_v<sequence_ty> > 0);

   template <typename tuple_ty>
   struct defined_type_list_from_tuple;

   template <typename... value_ty>
   struct defined_type_list_from_tuple<std::tuple<value_ty...>> {
      using type = adecc::defined_type_list<value_ty...>;
   };

   template <typename tuple_ty>
   using defined_type_list_from_tuple_t =
      typename defined_type_list_from_tuple<std::remove_cvref_t<tuple_ty>>::type;

   // ----------------------------------------------------------------------------------------------


   namespace details {
      template <class tuple_ty, std::size_t... Is>
         requires is_tuple_like_v<tuple_ty> && ((Is < std::tuple_size_v<std::remove_cvref_t<tuple_ty>>) && ...)
      [[nodiscard]] constexpr auto SelectTupleValues(tuple_ty&& tplValue, std::index_sequence<Is...>) {
         using std::get;
         return std::tuple<std::remove_cvref_t<decltype(get<Is>(std::forward<tuple_ty>(tplValue)))>...> {
            get<Is>(std::forward<tuple_ty>(tplValue))...
         };
      }

      template <class tuple_ty, std::size_t... Is>
         requires is_tuple_like_v<tuple_ty>&& std::is_lvalue_reference_v<tuple_ty&&> &&
      ((Is < std::tuple_size_v<std::remove_cvref_t<tuple_ty>>) && ...)
         [[nodiscard]] constexpr auto SelectTupleRefs(tuple_ty&& tplValue, std::index_sequence<Is...>) {
         using std::get;
         return std::forward_as_tuple(get<Is>(tplValue)...);
      }


      template <class target_tuple_ty, class source_tuple_ty, std::size_t... Is>
         requires is_tuple_like_v<target_tuple_ty>&&
      is_tuple_like_v<source_tuple_ty> &&
         ((Is < std::tuple_size_v<std::remove_cvref_t<target_tuple_ty>>) && ...) &&
         (sizeof...(Is) == std::tuple_size_v<std::remove_cvref_t<source_tuple_ty>>)
         constexpr void AssignTupleValues(target_tuple_ty& tplTarget, source_tuple_ty const& tplSource,
            std::index_sequence<Is...>) {
         using std::get;

         [&] <std::size_t... Js>(std::index_sequence<Js...>) {
            (
               adecc::details::AssignTupleValue(
                  get<Is>(tplTarget),
                  get<Js>(tplSource)
               ),
               ...
               );
         }(std::make_index_sequence<sizeof...(Is)> {});
      }

 
   } // namespace details

   template <typename sequence_ty, class tuple_ty>
      requires index_sequence_ty<sequence_ty>&& is_tuple_like_v<tuple_ty>
   [[nodiscard]] constexpr auto selectTplValues(tuple_ty&& tplValue) {
      return details::SelectTupleValues(std::forward<tuple_ty>(tplValue), sequence_ty{});
   }

   template <typename sequence_ty, class tuple_ty>
      requires index_sequence_ty<sequence_ty>&& is_tuple_like_v<tuple_ty>&&
   std::is_lvalue_reference_v<tuple_ty&&>
      [[nodiscard]] constexpr auto selectTplRefs(tuple_ty&& tplValue) {
      return details::SelectTupleRefs(std::forward<tuple_ty>(tplValue), sequence_ty{});
   }

   template <typename sequence_ty, class target_tuple_ty, class source_tuple_ty>
      requires index_sequence_ty<sequence_ty>&& is_tuple_like_v<target_tuple_ty>&&
                                 is_tuple_like_v<source_tuple_ty>
   constexpr void assignTplValues(target_tuple_ty& tplTarget, source_tuple_ty const& tplSource) {
      details::AssignTupleValues(tplTarget, tplSource, sequence_ty{});
   }





   // ============================================================================
   //   TYPE TRAITS & FOLDS
   // ============================================================================

   /*!
   \brief Checks whether \c T is contained in type list \c TL (my_type_list<...>)
   \tparam T Type to check
   \tparam TL my_type_list<...>
   */
   template <class ty, class ty_list>
   struct is_in_type_list;

   template <class ty, class... Ts>
   struct is_in_type_list<ty, defined_type_list<Ts...>>
      : std::bool_constant<(std::same_as<remove_cvref_t<ty>, Ts> || ...)> {
   };

   template <class ty, class ty_list>
   inline constexpr bool is_in_type_list_v = is_in_type_list<ty, ty_list>::value;



} // endof namespace adecc

