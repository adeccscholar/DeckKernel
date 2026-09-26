// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file FileOperations.h
\brief Filesystem utilities for discovery, metadata inspection, movement, and path validation.

\details
Provides small std::filesystem-based building blocks for enumerating files, reading file metadata,
normalizing extension filters, moving files, and validating file or directory paths. Physical filesystem
operations remain separate from the typed source/sink abstractions built on top of them.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Source and Sink: Data Flows in Type Space".
- "Data Movement Between Source and Sink".
- "Files as Typed Data Flows".
- "Files as Ranges: Resources, Tuples, and RAII".

\see ARCHITECTURE.md#files-as-typed-data-flows

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

#include "convert_core.h"

#include <filesystem>
#include <stdexcept>\n#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <set>
#include <cctype>
#include <algorithm>
#include <chrono>\n
#include <format>
#include <ranges>
#include <concepts>

namespace fs = std::filesystem;

namespace adecc::FileTools {

// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------
template<typename... Args>
     requires (std::constructible_from<std::string, Args const&> && ...)
std::set<std::string> TupleToSet(std::tuple<Args...> const& tpl) {
   std::set<std::string> setResult;
   std::apply(
      [&setResult](Args const&... args) {
         (setResult.insert(std::string { args }), ...);
        }, tpl );

   return setResult;
   }

// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------
inline auto GetFileInfo(fs::path const& theFile) {
   std::error_code ec;

   if (!fs::exists(theFile)) {
      throw std::runtime_error(std::format("File don't exist: {}", theFile.string()));
      }
   
   if (!fs::is_regular_file(theFile, ec)) {
      throw std::runtime_error(std::format("Not a regular file: {}", theFile.string()));
      }

   uintmax_t uSize { fs::file_size(theFile, ec) };
   if (ec) {
      throw std::runtime_error(std::format("Failed to read file size: {}", ec.message()));
      }

   auto const ftWriteTime { fs::last_write_time(theFile, ec) };
   if (ec) {
      throw std::runtime_error(std::format("Failed to read file time: {}", ec.message()));
      }

   auto const tpSystem {
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftWriteTime - fs::file_time_type::clock::now()
                    + std::chrono::system_clock::now()
                    )
        };
        
   return std::tuple{
      std::string { reinterpret_cast<char const*>(theFile.filename().u8string().data()) },  // Filename
      std::string { reinterpret_cast<char const*>(theFile.stem().u8string().data()) },      // Stem
      std::string { reinterpret_cast<char const*>(theFile.extension().u8string().data()) }, // Extension
      uSize,
      tpSystem
      };
   }

// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------
template <std::ranges::input_range range_ty>
   requires std::convertible_to<std::ranges::range_reference_t<range_ty>, fs::path>
inline auto GetFileInfos(range_ty const& rngFiles) {
   return rngFiles 
                   | std::views::transform([](fs::path const& theFile) {
                          return GetFileInfo(theFile);
                          })
                   | std::ranges::to<std::vector>();
   }

   
// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------
inline std::string to_lower(std::string&& str) {
   std::transform( str.begin(), str.end(), str.begin(),
                   [](unsigned char const c) {
                         return static_cast<char>(std::tolower(c));
                         });
   return str;
   }
   
// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------
inline auto split_strings(std::string_view const& strInput) {
   return strInput | std::views::split(';')  
                   | std::views::transform([](auto&& part) {
                        std::string strPart(part.begin(), part.end());
                        auto const uFirst = strPart.find_first_not_of(" \t\r\n");
                        if (uFirst == std::string::npos) {
                           return std::string{};
                           }
                        auto const uLast = strPart.find_last_not_of(" \t\r\n");
                        return strPart.substr(uFirst, uLast - uFirst + 1);
                        })                   
                   | std::views::filter([](std::string const& str) {
                        return !str.empty();
                        })
                   | std::views::transform([](std::string str) {
                        str = to_lower(std::forward<std::string>(str));
                        if (str.front() != '.') {
                           str.insert(0, 1, '.');
                           }
                        return str;
                        })
                   | std::ranges::to<std::set<std::string>>();
   }

/**
\brief Searches a directory for files with a specified extension.
\details Searches only the specified directory; the search is not recursive.
\param theSource Source directory.
\param svExt Extension to search for, with or without a leading dot, for example ".xml" or "xml".
\returns All matching files as paths.
\throw std::runtime_error If the source path does not exist or is not a directory.
*/
inline std::vector<fs::path> FindFilesByExtension(fs::path const& theSource,
                                                  std::string_view const svExt) {
   if (!fs::exists(theSource)) {
      throw std::runtime_error(std::format("Source directory does not exist: {}", theSource.string()));
      }
   if (!fs::is_directory(theSource)) {
      throw std::runtime_error(std::format("Source path is not a directory: {}", theSource.string()));
      }

   std::set<std::string> setExt = split_strings(svExt);
   
   std::vector<fs::path> vecFiles;
   
   for (auto const& theEntry : fs::directory_iterator(theSource)) {
      if (!theEntry.is_regular_file()) {
         continue;
         }

      fs::path const theFile = theEntry.path();
      if (setExt.empty() || setExt.contains(to_lower(theFile.extension().string()))) {
         vecFiles.emplace_back(theFile);
         }
      }
      
   return vecFiles;
   }


 
 
// Legacy timestamped copy helper; retained for compatibility.
inline fs::path MoveFile(fs::path const& aSource, fs::path const& target, 
                     std::chrono::time_point<std::chrono::system_clock> const& now) {

   std::error_code ec;
   if (!fs::exists(target, ec) || !fs::is_directory(target, ec)) {
      throw std::runtime_error(std::format("Target path does not exist or is not a directory: {}", target.string()));
      }
                     
   fs::path aDestination = target / (aSource.stem().string() + 
                           adecc::ConvertTo<std::string>(now, adecc::DateTimeFmt::ISO_TIMESTAMP) +
                           aSource.extension().string());

   fs::copy_file(aSource, aDestination, fs::copy_options::overwrite_existing);                           
   //fs::rename(aSource, aDestination);
   return aDestination;
   }
  

   
/**
\brief Moves a file into a target directory.
\details Keeps the source file name and places it below the target directory.
\param theSourceFile Source file.
\param theTarget Target directory.
\returns Destination path of the moved file.
\throw std::runtime_error If the target directory is invalid, the source is missing,
        the destination already exists, or moving the file fails.
*/
inline fs::path MoveFileToDirectory(fs::path const& theSourceFile,
                                    fs::path const& theTarget) {
   if (!fs::exists(theTarget)) {
      throw std::runtime_error("Target directory does not exist: " + theTarget.string());
      }
   if (!fs::is_directory(theTarget)) {
      throw std::runtime_error("Target path is not a directory: " + theTarget.string());
      }

   if (!fs::exists(theSourceFile)) {
      throw std::runtime_error("Source file does not exist: " + theSourceFile.string());
      }
   if (!fs::is_regular_file(theSourceFile)) {
      throw std::runtime_error("Source path is not a regular file: " + theSourceFile.string());
      }

   fs::path const theDestFile = theTarget / theSourceFile.filename();

   if (fs::exists(theDestFile)) {
      throw std::runtime_error("Target file already exists: " + theDestFile.string());
      }

   std::error_code ec;
   fs::rename(theSourceFile, theDestFile, ec);
   if (!ec) {
      return theDestFile;
      }

   // Fallback for, for example, different file systems
   ec.clear();
   fs::copy_file(theSourceFile, theDestFile, fs::copy_options::none, ec);
   if (ec) {
      throw std::runtime_error("Move failed during copy phase: " + ec.message());
      }

   ec.clear();
   fs::remove(theSourceFile, ec);
   if (ec) {
      throw std::runtime_error("Move partially completed; remove phase failed: " + ec.message());
      }

   return theDestFile;
   }


/**
 \brief Checks whether a path is a valid directory using strict validation.
 \details A valid directory path is absolute, can be canonicalized, and exists as a directory.
 \param thePath Path to validate.
 \returns true if the path is a valid directory.
*/
inline bool IsFullDirectoryPathStrict(fs::path const& thePath) {
   if (!thePath.is_absolute()) {
      return false;
      }

   std::error_code ec;

   fs::path const theCanonical = fs::weakly_canonical(thePath, ec);
   if (ec) {
      return false;
      }

   if (!fs::exists(theCanonical, ec) || ec) {
      return false;
      }

   if (!fs::is_directory(theCanonical, ec) || ec) {
      return false;
      }

   return true;
   }
   
inline bool IsFullFilePathStrict(fs::path const& thePath) {
   if (!thePath.is_absolute()) {
      return false;
      }

   std::error_code ec;
   fs::path const theCanonical = fs::weakly_canonical(thePath, ec);

   if (ec) {
      return false;
      }

   return fs::is_regular_file(theCanonical, ec) && !ec;
   }

   
} // namespace FileTools
