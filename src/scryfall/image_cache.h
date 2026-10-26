// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once
/**
\file image_cache.h
\brief Persistent, content-addressed image cache independent of database and transport.
*/
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace deckkernel::scryfall {

/** \brief An independently verified cached original image. */
struct CachedImage {
   std::vector<std::uint8_t> vecBytes;
   std::string strSha256;
   std::string strMediaType;
};

/** \brief Reusable immutable-blob image cache.
    \details URLs are mapped to SHA-256 image blobs. No HTTP requests are made
    by this class. A corrupt or incomplete entry is treated as a cache miss.
*/
class ImageCache final {
public:
   static constexpr std::size_t uMaximumBytes=12U*1024U*1024U;
   explicit ImageCache(std::filesystem::path aRoot);

   /** \brief Read and verify a previously stored URL.
       \returns Cached bytes and hash, or nullopt for a missing/invalid entry.
   */
   [[nodiscard]] std::optional<CachedImage> Find(std::string_view svUrl) const;

   /** \brief Validate and persist original bytes under their content hash.
       \throw std::runtime_error For invalid bytes or storage failures.
   */
   [[nodiscard]] CachedImage Store(std::string_view svUrl,
      std::span<std::uint8_t const> spBytes) const;

   /** \brief Drop the URL reference after PostgreSQL committed successfully.
       \details Shared SHA-256 blobs are kept until the final URL reference
       is removed. This cache folder must not be written concurrently by
       another process.
       \returns true if the referenced entry was deleted.
   */
   [[nodiscard]] bool RemoveCommitted(std::string_view svUrl,
      std::string_view svExpectedSha256);

   /** \brief Return original cache root. */
   [[nodiscard]] std::filesystem::path const& Root() const noexcept;

private:
   std::filesystem::path m_aRoot;
   std::unordered_map<std::string,std::size_t> m_mpBlobReferences;
   bool m_bReferenceIndexBuilt { false };
};

/** \brief Compute lowercase SHA-256 for arbitrary binary data. */
[[nodiscard]] std::string Sha256(std::span<std::uint8_t const> spBytes);

/** \brief Accept supported image signatures; never trust HTTP content type alone. */
[[nodiscard]] std::optional<std::string> DetectImageType(
   std::span<std::uint8_t const> spBytes);
}
