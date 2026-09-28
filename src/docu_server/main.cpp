// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file main.cpp
\brief Entry point for the DeckKernel Markdown documentation server.
*/

#include "docu_server.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

[[nodiscard]] std::filesystem::path ExecutableDirectory() {
   std::array<wchar_t, 32768U> arrModulePath{};

   DWORD const uLength = ::GetModuleFileNameW(
      nullptr,
      arrModulePath.data(),
      static_cast<DWORD>(arrModulePath.size())
      );

   if(uLength == 0U ||
      uLength >= arrModulePath.size()) {
      throw std::runtime_error{
         "cannot resolve executable directory"
         };
      }

   return std::filesystem::path{
      std::wstring_view{
         arrModulePath.data(),
         uLength
         }
      }.parent_path();
   }


[[nodiscard]] std::uint16_t ParsePort(
   char const* const szValue
) {
   unsigned long const uPort = std::stoul(szValue);

   if(uPort == 0UL ||
      uPort > 65535UL) {
      throw std::runtime_error{
         "port is outside the valid range"
         };
      }

   return static_cast<std::uint16_t>(uPort);
   }

} // namespace


int main(
   int const iArgc,
   char const* const argv[]
) {
   try {
      std::filesystem::path const aExecutableDirectory =
         ExecutableDirectory();

      deckkernel::docu::ServerConfiguration aConfiguration{
         .aRepositoryRoot =
            aExecutableDirectory.parent_path().parent_path(),
         .aRuntimeDirectory = aExecutableDirectory
         };

      for(int iIndex = 1; iIndex < iArgc; ++iIndex) {
         std::string_view const svArgument{ argv[iIndex] };

         if(svArgument == "--root") {
            if(++iIndex >= iArgc) {
               throw std::runtime_error{
                  "--root requires a path"
                  };
               }

            aConfiguration.aRepositoryRoot =
               std::filesystem::path{ argv[iIndex] };
            }
         else if(svArgument == "--address") {
            if(++iIndex >= iArgc) {
               throw std::runtime_error{
                  "--address requires an IP address"
                  };
               }

            aConfiguration.strBindAddress = argv[iIndex];
            }
         else if(svArgument == "--port") {
            if(++iIndex >= iArgc) {
               throw std::runtime_error{
                  "--port requires a value"
                  };
               }

            aConfiguration.uPort =
               ParsePort(argv[iIndex]);
            }
         else if(
            svArgument == "--help" ||
            svArgument == "-h"
         ) {
            std::println(
               "usage: deckkernel_docu_server "
               "[--root <repository>] "
               "[--address <IP>] "
               "[--port <1..65535>]"
               );
            return 0;
            }
         else {
            throw std::runtime_error{
               "unknown argument: " +
               std::string{ svArgument }
               };
            }
         }

      deckkernel::docu::DocuServer aServer{
         std::move(aConfiguration)
         };

      aServer.Run();
      return 0;
      }
   catch(std::exception const& aException) {
      std::println(
         std::cerr,
         "DeckKernelDocServer ERROR: {}",
         aException.what()
         );
      return 1;
      }
   }
