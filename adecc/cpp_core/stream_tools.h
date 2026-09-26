// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file stream_tools.h
\brief Stream policies, stream-buffer foundations, captions, and generic stream helpers.

\details
Defines narrow and wide stream type families as policies, provides a common StreamBufBase, caption
structures, and text splitting helpers. The policy approach keeps character and stream families coherent
without duplicating the surrounding architecture.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "StreamPolicy: Type Families as Architectural Building Blocks".
- "Text Wrapper and the Universal Wrapper Concept".
- "Output Iterators and Standard Algorithms".
- "From Text and Grid to a General Adapter Strategy".

\see ARCHITECTURE.md#text-wrappers-and-stream-integration

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
#include "type_traits_ext.h"

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <streambuf>
#include <type_traits>
#include <string_view>
#include <concepts>

#include <ranges>
#include "generator.h"

namespace adecc {

template <typename ty>
concept StreamPolicy =
   requires {
      typename ty::char_type;
      typename ty::traits_type;
      typename ty::streambuf;
      typename ty::stringstream;
	  typename ty::string_type;
	  typename ty::string_view_type;
      typename ty::istream;
      typename ty::ostream;
      { ty::cNL } -> std::convertible_to<typename ty::char_type>;
      { ty::strEmpty } -> std::convertible_to<typename ty::string_type const&>;
   } &&
   std::derived_from<typename ty::streambuf,
					 std::basic_streambuf<typename ty::char_type, typename ty::traits_type>> &&
   std::same_as<typename ty::istream,
				std::basic_istream<typename ty::char_type, typename ty::traits_type>> &&
   std::same_as<typename ty::ostream,
				std::basic_ostream<typename ty::char_type, typename ty::traits_type>> &&
   std::same_as<typename ty::string_view_type,
				std::basic_string_view<typename ty::char_type, typename ty::traits_type>>;

struct AnsiStreamPolicy {
   using char_type        = char;
   using traits_type      = std::char_traits<char_type>;
   using streambuf        = std::basic_streambuf<char_type, traits_type>;
   using istream          = std::basic_istream<char_type, traits_type>;
   using ostream          = std::basic_ostream<char_type, traits_type>;
   using stringstream     = std::basic_stringstream<char_type, traits_type>;
   using string_type      = std::basic_string<char_type>;
   using string_view_type = std::basic_string_view<char_type, traits_type>;

   static constexpr char_type cNL = '\n';
   static inline string_type const strEmpty{};
};

struct WideStreamPolicy {
   using char_type        = wchar_t;
   using traits_type      = std::char_traits<char_type>;
   using streambuf        = std::basic_streambuf<char_type, traits_type>;
   using istream          = std::basic_istream<char_type, traits_type>;
   using ostream          = std::basic_ostream<char_type, traits_type>;
   using stringstream     = std::basic_stringstream<char_type, traits_type>;
   using string_type      = std::basic_string<char_type>;
   using string_view_type = std::basic_string_view<char_type, traits_type>;

   static constexpr char_type cNL = L'\n';
   static inline string_type const strEmpty{};
};



template <StreamPolicy ty>
class StreamBufBase : public ty::streambuf {
protected:
   using traits_type = typename ty::traits_type;
   using int_type = typename traits_type::int_type;

   typename ty::stringstream os;

public:
   StreamBufBase(void) { }
   virtual ~StreamBufBase(void) { }

   virtual void Write(void) = 0;

   int_type overflow(int_type const iC) override {
	  if(iC == traits_type::to_int_type(ty::cNL)) {
		 Write();
		 os.str(ty::strEmpty);
		 }
	  else if(!traits_type::eq_int_type(iC, traits_type::eof())) {
		 os.put(traits_type::to_char_type(iC));
		 }
	  return iC;
	  }
 
};


template <typename ty>
struct StreamPolicyFromString;

template <>
struct StreamPolicyFromString<std::string> {
   using type = AnsiStreamPolicy;
   };

template <>
struct StreamPolicyFromString<std::wstring> {
   using type = WideStreamPolicy;
   };

template <typename tup_ty>
struct StreamPolicyFromTuple {
   using type = typename StreamPolicyFromString<std::tuple_element_t<0, std::remove_cvref_t<tup_ty>>>::type;
   };

template <typename tup_ty>
using StreamPolicyFromTuple_t = typename StreamPolicyFromTuple<std::remove_cvref_t<tup_ty>>::type;


template <typename ty>
concept CaptionTuplePolicy = 
   requires { typename std::tuple_element_t<0, std::remove_cvref_t<ty>>;
              typename std::tuple_element_t<1, std::remove_cvref_t<ty>>;
              typename std::tuple_element_t<2, std::remove_cvref_t<ty>>;
              typename StreamPolicyFromTuple_t<ty>;
            } && 
             (std::tuple_size_v<std::remove_cvref_t<ty>> >= 3) &&
             std::same_as<std::tuple_element_t<0, std::remove_cvref_t<ty>>, typename StreamPolicyFromTuple_t<ty>::string_type > &&
             std::same_as<std::tuple_element_t<1, std::remove_cvref_t<ty>>, int> &&
             std::same_as<std::tuple_element_t<2, std::remove_cvref_t<ty>>, EAlignmentType>;

template <typename ty>
concept CaptionVector = vector_type <ty> &&
                        CaptionTuplePolicy<typename std::remove_cvref_t<ty>::value_type>;


template <StreamPolicy ty = AnsiStreamPolicy>
using tplCaption  = std::tuple<typename ty::string_type, int, EAlignmentType>;

template <StreamPolicy ty = AnsiStreamPolicy>
using vecCaptions = std::vector<tplCaption<ty>>;


template <CaptionVector captions_ty>
[[nodiscard]] captions_ty operator +(captions_ty const& vecLeft, captions_ty const& vecRight) {
   captions_ty vecResult;
   vecResult.reserve(vecLeft.size() + vecRight.size());
   vecResult.insert(vecResult.end(), vecLeft.begin(), vecLeft.end());
   vecResult.insert(vecResult.end(), vecRight.begin(), vecRight.end());
   return vecResult;
   }

static_assert(CaptionVector<vecCaptions<AnsiStreamPolicy>>, "Caption Bedingungen nicht erfüllt.");


/*!
 \brief Splits a string into string views using a delimiter.
 \details
    The generator returns views into the original string without copying. The source string
    must remain valid for the duration of the iteration.

 \tparam SP StreamPolicy
 \param strText Eingabetext
 \param chDelimiter Trennzeichen
 \returns Generator yielding string_view/wstring_view values
*/
template <adecc::StreamPolicy SP = adecc::AnsiStreamPolicy>
Generator<typename SP::string_view_type> SplitView(typename SP::string_type const& strText,
												   typename SP::char_type const chDelimiter) {
   using string_view_ty = typename SP::string_view_type;
   using size_ty        = typename SP::string_type::size_type;

   size_ty uStart = 0;

   while(uStart <= strText.size()) {
	  size_ty const uPos = strText.find(chDelimiter, uStart);

	  if(uPos == SP::string_type::npos) {
		 co_yield string_view_ty { strText.data() + uStart,	strText.size() - uStart };
		 break;
		 }

	  co_yield string_view_ty { strText.data() + uStart, uPos - uStart };
	  uStart = uPos + 1;
	  }

   co_return;
   }


} // namespace adecc
