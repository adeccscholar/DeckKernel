// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file diagnostic_text.h
\brief UTF-8 diagnostic text conversion and console output helpers.

\details
Diagnostic and exception text in the adecc core uses UTF-8 in std::string. This is a
diagnostic contract only; it does not redefine the encoding semantics of ordinary
application std::string values.

Wide values are converted to UTF-8 when they enter diagnostics. On Windows, diagnostic
text written to an attached console is converted to UTF-16 and emitted with
WriteConsoleW. Redirected output remains UTF-8 byte text, which keeps log files and
pipes independent from the active console code page.

\author Volker Hillmann
\copyright Copyright © 2021 - 2026 adecc Systemhaus GmbH
*/

#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <cstdint>
#include <iostream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace adecc::diagnostic {

   inline void AppendUtf8(std::string& strResult, std::uint32_t const uCodePoint) {
      if (uCodePoint <= 0x7f) {
         strResult.push_back(static_cast<char>(uCodePoint));
         }
      else if (uCodePoint <= 0x7ff) {
         strResult.push_back(static_cast<char>(0xc0 | (uCodePoint >> 6)));
         strResult.push_back(static_cast<char>(0x80 | (uCodePoint & 0x3f)));
         }
      else if (uCodePoint <= 0xffff) {
         strResult.push_back(static_cast<char>(0xe0 | (uCodePoint >> 12)));
         strResult.push_back(static_cast<char>(0x80 | ((uCodePoint >> 6) & 0x3f)));
         strResult.push_back(static_cast<char>(0x80 | (uCodePoint & 0x3f)));
         }
      else if (uCodePoint <= 0x10ffff) {
         strResult.push_back(static_cast<char>(0xf0 | (uCodePoint >> 18)));
         strResult.push_back(static_cast<char>(0x80 | ((uCodePoint >> 12) & 0x3f)));
         strResult.push_back(static_cast<char>(0x80 | ((uCodePoint >> 6) & 0x3f)));
         strResult.push_back(static_cast<char>(0x80 | (uCodePoint & 0x3f)));
         }
      else {
         throw std::runtime_error("invalid Unicode code point for diagnostic text");
         }
      }


   inline std::string WideToUtf8(std::wstring_view const svValue) {
      std::string strResult;
      strResult.reserve(svValue.size());

      if constexpr (sizeof(wchar_t) == 2) {
         for (std::size_t uIndex{}; uIndex < svValue.size(); ++uIndex) {
            std::uint32_t uCodePoint = static_cast<std::uint32_t>(svValue[uIndex]);

            if (uCodePoint >= 0xd800 && uCodePoint <= 0xdbff) {
               if (++uIndex >= svValue.size()) {
                  throw std::runtime_error("unpaired wchar_t high surrogate");
                  }

               std::uint32_t const uLow = static_cast<std::uint32_t>(svValue[uIndex]);
               if (uLow < 0xdc00 || uLow > 0xdfff) {
                  throw std::runtime_error("invalid wchar_t surrogate pair");
                  }

               uCodePoint = 0x10000 + ((uCodePoint - 0xd800) << 10) + (uLow - 0xdc00);
               }
            else if (uCodePoint >= 0xdc00 && uCodePoint <= 0xdfff) {
               throw std::runtime_error("unpaired wchar_t low surrogate");
               }

            AppendUtf8(strResult, uCodePoint);
            }
         }
      else {
         for (wchar_t const ch : svValue) {
            std::uint32_t const uCodePoint = static_cast<std::uint32_t>(ch);

            if (uCodePoint >= 0xd800 && uCodePoint <= 0xdfff) {
               throw std::runtime_error("invalid wchar_t Unicode code point");
               }

            AppendUtf8(strResult, uCodePoint);
            }
         }

      return strResult;
      }


   inline std::wstring Utf8ToWide(std::string_view const svValue) {
      std::wstring strResult;
      strResult.reserve(svValue.size());

      for (std::size_t uPos{}; uPos < svValue.size();) {
         unsigned char const uFirst = static_cast<unsigned char>(svValue[uPos]);
         std::uint32_t uCodePoint{};
         std::size_t uCount{};

         if (uFirst < 0x80) {
            uCodePoint = uFirst;
            uCount = 1;
            }
         else if ((uFirst & 0xe0) == 0xc0) {
            uCodePoint = uFirst & 0x1f;
            uCount = 2;
            }
         else if ((uFirst & 0xf0) == 0xe0) {
            uCodePoint = uFirst & 0x0f;
            uCount = 3;
            }
         else if ((uFirst & 0xf8) == 0xf0) {
            uCodePoint = uFirst & 0x07;
            uCount = 4;
            }
         else {
            throw std::runtime_error("invalid UTF-8 leading byte in diagnostic text");
            }

         if (uPos + uCount > svValue.size()) {
            throw std::runtime_error("truncated UTF-8 sequence in diagnostic text");
            }

         for (std::size_t uIndex{ 1 }; uIndex < uCount; ++uIndex) {
            unsigned char const uNext =
               static_cast<unsigned char>(svValue[uPos + uIndex]);

            if ((uNext & 0xc0) != 0x80) {
               throw std::runtime_error("invalid UTF-8 continuation byte in diagnostic text");
               }

            uCodePoint = (uCodePoint << 6) | (uNext & 0x3f);
            }

         if ((uCount == 2 && uCodePoint < 0x80) ||
             (uCount == 3 && uCodePoint < 0x800) ||
             (uCount == 4 && uCodePoint < 0x10000) ||
             uCodePoint > 0x10ffff ||
             (uCodePoint >= 0xd800 && uCodePoint <= 0xdfff)) {
            throw std::runtime_error("invalid UTF-8 code point in diagnostic text");
            }

         if constexpr (sizeof(wchar_t) == 2) {
            if (uCodePoint <= 0xffff) {
               strResult.push_back(static_cast<wchar_t>(uCodePoint));
               }
            else {
               uCodePoint -= 0x10000;
               strResult.push_back(static_cast<wchar_t>(0xd800 + (uCodePoint >> 10)));
               strResult.push_back(static_cast<wchar_t>(0xdc00 + (uCodePoint & 0x3ff)));
               }
            }
         else {
            strResult.push_back(static_cast<wchar_t>(uCodePoint));
            }

         uPos += uCount;
         }

      return strResult;
      }


   inline std::string NarrowToUtf8(std::string_view const svValue) {
#if defined(_WIN32)
      if (svValue.empty()) {
         return {};
         }

      int const iWideLength = MultiByteToWideChar(
         CP_ACP,
         MB_ERR_INVALID_CHARS,
         svValue.data(),
         static_cast<int>(svValue.size()),
         nullptr,
         0
         );

      if (iWideLength <= 0) {
         return std::string{ svValue };
         }

      std::wstring strWide(static_cast<std::size_t>(iWideLength), wchar_t{});
      int const iConverted = MultiByteToWideChar(
         CP_ACP,
         MB_ERR_INVALID_CHARS,
         svValue.data(),
         static_cast<int>(svValue.size()),
         strWide.data(),
         iWideLength
         );

      if (iConverted != iWideLength) {
         return std::string{ svValue };
         }

      return WideToUtf8(strWide);
#else
      return std::string{ svValue };
#endif
      }


#if defined(_WIN32)
   inline HANDLE ConsoleHandle(std::ostream& os) noexcept {
      if (&os == &std::cout) {
         return GetStdHandle(STD_OUTPUT_HANDLE);
         }

      if (&os == &std::cerr || &os == &std::clog) {
         return GetStdHandle(STD_ERROR_HANDLE);
         }

      return INVALID_HANDLE_VALUE;
      }


   inline bool IsAttachedConsole(HANDLE const hHandle) noexcept {
      if (hHandle == nullptr || hHandle == INVALID_HANDLE_VALUE) {
         return false;
         }

      DWORD uMode{};
      return GetConsoleMode(hHandle, &uMode) != 0;
      }
#endif


   inline void WriteUtf8(std::ostream& os, std::string_view const svText) {
#if defined(_WIN32)
      HANDLE const hHandle = ConsoleHandle(os);

      if (IsAttachedConsole(hHandle)) {
         try {
            std::wstring const strWide = Utf8ToWide(svText);

            DWORD uWritten{};
            if (WriteConsoleW(
                   hHandle,
                   strWide.data(),
                   static_cast<DWORD>(strWide.size()),
                   &uWritten,
                   nullptr
                ) != 0) {
               return;
               }
            }
         catch (...) {
            }
         }
#endif

      os.write(svText.data(), static_cast<std::streamsize>(svText.size()));
      }


   inline void WriteUtf8Line(std::ostream& os, std::string_view const svText) {
      WriteUtf8(os, svText);
      os.put('\n');
      }

} // namespace adecc::diagnostic
