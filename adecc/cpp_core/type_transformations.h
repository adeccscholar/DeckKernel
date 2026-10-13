#pragma once

// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file type_transformations.h
\brief value to value transformations for adecc::defined_type_list

\details
Defines Value to Value transformations for type lists.

\version 1.0
\date 11.10.2026
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


#include "type_lists.h"
#include "convert_core.h"

#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <format>
#include <map>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace adecc {

   /**
   \brief Tests whether a type provides the requested key/value mapping.
   \details Accepts associative maps with compatible find/end operations
      or a callable returning exactly the requested value type.
   \tparam mapping_ty Candidate mapping type.
   \tparam key_ty Key type.
   \tparam value_ty Value type.
   \returns True for a compatible mapping or callable.
   \throw Nothing; this is a compile-time constraint.
   */
   template<typename mapping_ty, typename key_ty, typename value_ty>
   concept key_value_mapping = (
      requires(std::remove_cvref_t<mapping_ty> const& theMap, key_ty const& theKey) {
         typename std::remove_cvref_t<mapping_ty>::key_type;
         typename std::remove_cvref_t<mapping_ty>::mapped_type;
         requires std::same_as<typename std::remove_cvref_t<mapping_ty>::key_type, key_ty>;
         requires std::same_as<typename std::remove_cvref_t<mapping_ty>::mapped_type, value_ty>;
         { theMap.find(theKey) } -> std::same_as<typename std::remove_cvref_t<mapping_ty>::const_iterator>;
         { theMap.end() } -> std::same_as<typename std::remove_cvref_t<mapping_ty>::const_iterator>;
         }
      ) || (
         std::invocable<mapping_ty, key_ty const&> &&
         requires { requires std::same_as<std::invoke_result_t<mapping_ty, key_ty const&>, value_ty>; }
      );

   /**
   \brief Gets a value from a mapping or callable.
   \details Looks up the key with find or invokes the callable.
   \tparam key_ty Key type.
   \tparam value_ty Value type.
   \tparam mapping_ty Mapping or callable type.
   \param theMapping Mapping or callable.
   \param theKey Lookup key.
   \returns The mapped value.
   \throw std::out_of_range If the key is absent from a map.
   \throw ... Exceptions raised by a callable or result construction.
   */
   
   template<typename key_ty, typename value_ty, typename mapping_ty>
      requires key_value_mapping<mapping_ty, key_ty, value_ty>
   value_ty GetValue(mapping_ty&& theMapping, key_ty const& theKey) {
      if constexpr (requires { theMapping.find(theKey); theMapping.end(); }) {
         auto it = theMapping.find(theKey);
         if (it == theMapping.end()) {
            throw std::out_of_range("GetValue: key not found");
            }
         return it->second;
         }
      else {
         return std::invoke(std::forward<mapping_ty>(theMapping), theKey);
         }
      }

   template<defined_type_list_ty list_ty, std::size_t I>
   using defined_type_at_t = std::tuple_element_t<I, typename list_ty::type_list>;

   /**
   \brief Binds an index to a statically validated mapping.
   \details Key and value types are extracted from the type lists.
   \tparam I Index in both lists.
   \tparam from_list_ty Source type list.
   \tparam to_list_ty Destination type list.
   \tparam mapping_ty Mapping or callable storage type.
   \returns A mapping descriptor type.
   \throw Nothing; invalid bindings are rejected during compilation.
   */
   template<std::size_t I, defined_type_list_ty from_list_ty, defined_type_list_ty to_list_ty, typename mapping_ty>
      requires (I < std::tuple_size_v<typename from_list_ty::type_list> &&
                I < std::tuple_size_v<typename to_list_ty::type_list> &&
                key_value_mapping<mapping_ty&, defined_type_at_t<from_list_ty, I>, defined_type_at_t<to_list_ty, I>>)
   struct indexed_mapping {
      static constexpr std::size_t index = I;
      using source_list_ty = from_list_ty;
      using target_list_ty = to_list_ty;
      mapping_ty theMapping;
      };

   /**
   \brief Constructs a checked indexed mapping.
   \details Preserves references to lvalue mappings and owns rvalue mappings.
   \tparam I Index in both lists.
   \tparam from_list_ty Source type list.
   \tparam to_list_ty Target type list.
   \tparam mapping_ty Deduced mapping type.
   \param theMapping Mapping object or callable.
   \returns A constrained index/mapping binding.
   \throw ... Exceptions during mapping construction.
   */
   template<std::size_t I, typename from_list_ty, typename to_list_ty, typename mapping_ty>
      requires requires { typename indexed_mapping<I, from_list_ty, to_list_ty, mapping_ty>; }
   constexpr auto BindMapping(mapping_ty&& theMapping) -> 
           indexed_mapping<I, from_list_ty, to_list_ty, mapping_ty> {
      return { std::forward<mapping_ty>(theMapping) };
      }

   namespace detail {

      template<typename binding_ty, typename from_list_ty, typename to_list_ty>
      concept matching_binding = requires {
         { std::remove_cvref_t<binding_ty>::index } -> std::convertible_to<std::size_t>;
         requires std::same_as<typename std::remove_cvref_t<binding_ty>::source_list_ty, from_list_ty>;
         requires std::same_as<typename std::remove_cvref_t<binding_ty>::target_list_ty, to_list_ty>;
         };

      template<std::size_t... I>
      consteval bool UniqueIndices() {
         constexpr std::array<std::size_t, sizeof...(I)> arrIndices{ I... };
         for (std::size_t i = 0; i < arrIndices.size(); ++i) {
            for (std::size_t j = i + 1; j < arrIndices.size(); ++j) {
               if (arrIndices[i] == arrIndices[j]) { return false; }
               }
            }
         return true;
         }

      template<std::size_t I, typename... binding_ty>
      inline constexpr bool has_mapping_v = ((I == std::remove_cvref_t<binding_ty>::index) || ... || false);

      template<std::size_t I, typename... binding_ty>
      consteval std::size_t MappingPosition() {
         constexpr std::array<std::size_t, sizeof...(binding_ty)> arrIndices{
            std::remove_cvref_t<binding_ty>::index...
            };
         for (std::size_t i = 0; i < arrIndices.size(); ++i) {
            if (arrIndices[i] == I) { return i; }
            }
         return sizeof...(binding_ty);
         }

      template<typename from_list_ty, typename to_list_ty, typename... binding_ty>
      consteval bool ValidTransformation() {
         constexpr std::size_t uSize = std::tuple_size_v<typename from_list_ty::type_list>;
         if constexpr (uSize != std::tuple_size_v<typename to_list_ty::type_list> ||
                       !(matching_binding<binding_ty, from_list_ty, to_list_ty> && ...)) {
            return false;
            }
         else if constexpr (!UniqueIndices<std::remove_cvref_t<binding_ty>::index...>()) {
            return false;
            }
         else {
            return []<std::size_t... I>(std::index_sequence<I...>) {
               return ((has_mapping_v<I, binding_ty...> ||
                  (std::same_as<defined_type_at_t<from_list_ty, I>, defined_type_at_t<to_list_ty, I>> ||
                  Convertible<defined_type_at_t<from_list_ty, I>, defined_type_at_t<to_list_ty, I>> ||
                  StdConvertible<defined_type_at_t<from_list_ty, I>, defined_type_at_t<to_list_ty, I>>)) && ...);
               }(std::make_index_sequence<uSize>{});
            }
         }

      template<std::size_t I, typename from_list_ty, typename to_list_ty,
         typename source_ty, typename... binding_ty>
      constexpr auto TransformPosition(source_ty&& theSource, std::tuple<binding_ty...>& theBindings)
         -> defined_type_at_t<to_list_ty, I> {
         using from_ty = defined_type_at_t<from_list_ty, I>;
         using to_ty = defined_type_at_t<to_list_ty, I>;
         auto&& theValue = std::get<I>(std::forward<source_ty>(theSource));
         if constexpr (has_mapping_v<I, binding_ty...>) {
            constexpr std::size_t uPosition = MappingPosition<I, binding_ty...>();
            auto&& theMapping = std::get<uPosition>(theBindings).theMapping;
            return GetValue<from_ty, to_ty>(theMapping, theValue);
            }
         else if constexpr (std::same_as<from_ty, to_ty> &&
            std::constructible_from<to_ty, decltype(theValue)>) {
            return to_ty(std::forward<decltype(theValue)>(theValue));
            }
         else {
            return adecc::ConvertTo<to_ty>(std::forward<decltype(theValue)>(theValue));
            }
         }

      // Accept only tuples whose elements are valid indexed mappings.
      template<typename from_list_ty, typename to_list_ty, typename tuple_ty>
      struct valid_binding_tuple : std::false_type { };

      template<typename from_list_ty, typename to_list_ty, typename... binding_ty>
      struct valid_binding_tuple<from_list_ty, to_list_ty, std::tuple<binding_ty...>>
         : std::bool_constant<ValidTransformation<from_list_ty, to_list_ty, binding_ty...>()> { };

      template<typename from_list_ty, typename to_list_ty, typename tuple_ty>
      concept binding_tuple_for =
         valid_binding_tuple<from_list_ty, to_list_ty, std::remove_cvref_t<tuple_ty>>::value;

      template<typename from_list_ty, typename to_list_ty, typename source_ty,
         typename bindings_ty, std::size_t... I>
         requires binding_tuple_for<from_list_ty, to_list_ty, bindings_ty>
      constexpr auto TransformImpl(source_ty&& theSource, bindings_ty& theBindings,
         std::index_sequence<I...>) -> typename to_list_ty::type_list {
         using result_ty = typename to_list_ty::type_list;
         return result_ty{ TransformPosition<I, from_list_ty, to_list_ty>(
            std::forward<source_ty>(theSource), theBindings)... };
         }

      }

   /**
   \brief Transforms tuple elements using optional index-specific mappings.
   \details Explicit mappings take precedence. Otherwise equal types are
      forwarded and remaining values use adecc::ConvertTo.
   \tparam from_list_ty Source type list.
   \tparam to_list_ty Destination type list.
   \tparam source_ty Source tuple reference type.
   \tparam binding_ty Indexed mapping descriptor types.
   \param theSource Source tuple.
   \param theBindings Zero or more checked indexed mappings.
   \returns Tuple of destination values.
   \throw std::out_of_range If an indexed map lacks its key.
   \throw ... Exceptions propagated by mappings and conversions.
   \pre The source tuple matches the source type list.
   \post The result has the destination type list's element types.
   \note Mapping indices must be unique; unmapped positions require identity, Convertible or StdConvertible.
   */
   template<defined_type_list_ty from_list_ty, defined_type_list_ty to_list_ty, typename source_ty, typename... binding_ty>
      requires (defined_type_list_row<from_list_ty, std::remove_cvref_t<source_ty>> &&
         detail::ValidTransformation<from_list_ty, to_list_ty, binding_ty...>())
   constexpr auto Transform(source_ty&& theSource, binding_ty&&... theBindings)
      -> typename to_list_ty::type_list {
      auto theMappingTuple = std::forward_as_tuple(theBindings...);
      return detail::TransformImpl<from_list_ty, to_list_ty>(
         std::forward<source_ty>(theSource), theMappingTuple,
         std::make_index_sequence<std::tuple_size_v<typename from_list_ty::type_list>>{});
      }

   } // namespace adecc
