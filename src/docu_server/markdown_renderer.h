// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file markdown_renderer.h
\brief Markdown-to-HTML rendering for the DeckKernel documentation server.
\details
The renderer follows the proven BuildEngine-Server/Common approach: cmark-gfm performs
Markdown conversion while browser-side libraries add syntax highlighting, Mermaid and
MathJax only when the document requires them.
*/

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace deckkernel::docu {

enum class MarkdownFeature : std::uint32_t {
   none = 0U,
   syntaxHighlighting = 1U,
   mermaid = 2U,
   mathJax = 4U
   };

[[nodiscard]] constexpr MarkdownFeature operator|(
   MarkdownFeature const aLeft,
   MarkdownFeature const aRight
) noexcept {
   return static_cast<MarkdownFeature>(
      static_cast<std::uint32_t>(aLeft) |
      static_cast<std::uint32_t>(aRight)
      );
   }

constexpr MarkdownFeature& operator|=(
   MarkdownFeature& aLeft,
   MarkdownFeature const aRight
) noexcept {
   aLeft = aLeft | aRight;
   return aLeft;
   }

[[nodiscard]] constexpr bool HasMarkdownFeature(
   MarkdownFeature const aFeatures,
   MarkdownFeature const aFeature
) noexcept {
   return (
      static_cast<std::uint32_t>(aFeatures) &
      static_cast<std::uint32_t>(aFeature)
      ) != 0U;
   }

struct MarkdownRenderResult {
   std::string strHtml;
   MarkdownFeature aFeatures{ MarkdownFeature::none };
   };

class MarkdownRenderer final {
public:
   /**
   \brief Detects browser-side features used by a Markdown document.
   \param svMarkdown UTF-8 Markdown source.
   \return Feature mask for syntax highlighting, Mermaid and MathJax.
   */
   [[nodiscard]] static MarkdownFeature Analyze(
      std::string_view svMarkdown
   ) noexcept;

   /**
   \brief Verifies the app-local cmark-gfm runtime.
   \param aRuntimeDirectory Directory containing the cmark-gfm runtime DLLs.
   */
   static void Validate(
      std::filesystem::path const& aRuntimeDirectory
   );

   /**
   \brief Converts one UTF-8 Markdown string to HTML.
   \param aRuntimeDirectory Directory containing the cmark-gfm runtime DLLs.
   \param svMarkdown UTF-8 Markdown source.
   \return Rendered HTML and detected browser-side features.
   */
   [[nodiscard]] static MarkdownRenderResult Render(
      std::filesystem::path const& aRuntimeDirectory,
      std::string_view svMarkdown
   );

   /**
   \brief Reads and renders a Markdown file.
   \param aRuntimeDirectory Directory containing the cmark-gfm runtime DLLs.
   \param aFile Markdown file to read.
   \return Rendered HTML and detected browser-side features.
   */
   [[nodiscard]] static MarkdownRenderResult RenderFile(
      std::filesystem::path const& aRuntimeDirectory,
      std::filesystem::path const& aFile
   );
   };

} // namespace deckkernel::docu
