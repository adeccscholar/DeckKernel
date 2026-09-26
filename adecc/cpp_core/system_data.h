// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file system_data.h
\brief Typed data object derived from a defined type list and represented as a tuple.

\details
Turns a compile-time type sequence into a reusable value object with typed indexed access, tuple-compatible
construction and assignment, conversion-aware input, and full or selected comparisons. It is the bridge from
type-list metadata to concrete domain-oriented C++ data.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Type Lists as Structured Type Relations".
- "From Type Sequence to Data Class".
- "SystemData as an Application of the Type List".
- "The Core Belongs in C++".

\see ARCHITECTURE.md#type-lists-tuples-and-systemdata

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
#include "type_lists.h"

#include <string>
#include <tuple>

namespace adecc {

template <adecc::defined_type_list_ty types_ty>
class SystemData  {
   public:
      using types_list = types_ty; // extern use later
      using data_ty    = typename types_ty::type_list;
      
      data_ty data;
            
      static constexpr std::size_t size = std::tuple_size_v<data_ty>;

      template <std::size_t Index>
      using element_ty = std::tuple_element_t<Index, data_ty>;
      
      template <std::size_t Index>
      constexpr element_ty<Index>& Get() & noexcept {
         return std::get<Index>(data);
         }

      template <std::size_t Index>
      constexpr element_ty<Index> const& Get() const& noexcept {
         return std::get<Index>(data);
         }

      template <std::size_t Index>
      constexpr element_ty<Index>&& Get() && noexcept {
         return std::get<Index>(std::move(data));
         }

      template <std::size_t Index, typename value_ty, typename... args_tys>
         requires (Index < size) && adecc::HasConvertTo<value_ty, element_ty<Index>, args_tys...>
      auto& Set(value_ty&& theValue, args_tys&&... args) & {
         Get<Index>() = adecc::ConvertTo<element_ty<Index>>(std::forward<value_ty>(theValue),
                                                            std::forward<args_tys>(args)...);

         return *this;
         }   

      template <std::size_t Index, typename... args_ty>
           requires (Index < size) && std::constructible_from<element_ty<Index>, args_ty...>
      constexpr auto& Emplace(args_ty&&... args) & {
         Get<Index>() = element_ty<Index>{ std::forward<args_ty>(args)... };
         return *this;
         }              
      
   public:

      SystemData() = default;
      SystemData(SystemData const&) = default;
      SystemData(SystemData&&) noexcept = default;

      // Variadic move: exact same types plus rvalues
      template <typename... Args> 
         requires adecc::is_same_args_v<types_ty, std::remove_cvref_t<Args>...> && 
                  adecc::all_rvalues<Args...>
      SystemData(Args&&... args)  : data { std::forward<Args>(args)... } {
         }

      // exakt data tuple (copy / move)
      SystemData(data_ty const& theTuple) : data(theTuple) {
          }
         
      SystemData(data_ty&& theTuple) noexcept : data(std::move(theTuple)) {
         }
         

      // Variadic copy/convert: each element is convertible
      template <typename... Args> requires adecc::is_convertible_args_v<types_ty, Args const&...>
      SystemData(Args const&... args) {
         data = types_ty::to_target(std::forward_as_tuple(args...));
         }

 
      template <typename... Args>
         requires adecc::is_convertible_args_v<types_ty, Args...>
      SystemData(std::tuple<Args...> const& args) {
         data = types_ty::to_target(args);
         }

      SystemData& operator = (SystemData const&) = default;
      SystemData& operator = (SystemData&&) noexcept = default;
         
      SystemData& operator=(data_ty const& theTuple) {
         data = theTuple;
         return *this;
         }

      SystemData& operator=(data_ty&& theTuple) noexcept {
         data = std::move(theTuple);
         return *this;
         }

      auto operator<=>(SystemData const&) const = default;
      bool operator==(SystemData const&) const = default;


      
      template <typename... Args> requires adecc::is_same_args_v<types_ty, std::remove_cvref_t<Args>...> &&
                                           adecc::all_rvalues<Args...>
      SystemData& Assign(Args&&... args) {
         data = data_ty{ std::forward<Args>(args)... };
         return *this;
         }

      template <typename... Args> requires adecc::is_convertible_args_v<types_ty, Args const&...>
      SystemData& Assign(Args const&... args) {
         data = types_ty::to_target(std::forward_as_tuple(args...));
         return *this;
         }

      template <typename Tup> requires (adecc::tuple_compatible_v<types_ty, Tup>) &&
                                       (!std::same_as<std::remove_cvref_t<Tup>, data_ty>)
      SystemData& operator=(Tup&& theTuple) {
         data = types_ty::to_target(std::forward<Tup>(theTuple));
         return *this;
         }


      template <std::size_t... Indices> requires (sizeof...(Indices) > 0) 
                                                  && ((Indices < size) && ...)
      constexpr auto CompareSelected(SystemData const& rhs) const {
         using result_ty = std::common_comparison_category_t<
              decltype(std::get<Indices>(data) <=> std::get<Indices>(rhs.data))...
              >;

         result_ty result = result_ty::equivalent;

         bool const bFound =
            (((result = std::get<Indices>(data) <=> std::get<Indices>(rhs.data)),
                 result != 0) || ...);
         (void)bFound;

         return result;
         }

      template <std::size_t... Indices> requires (sizeof...(Indices) > 0) 
                                                  && ((Indices < size) && ...)
      constexpr bool EqualSelected(SystemData const& rhs) const {
         return CompareSelected<Indices...>(rhs) == 0;
         }

         
      operator data_ty const&() const& { return data; }
      operator data_ty() && { return std::move(data); }      
      
   public:
      data_ty&       Data() { return data; }
      data_ty const& Data() const { return data; }
      
   };

} // namespace adecc
