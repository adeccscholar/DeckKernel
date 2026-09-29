// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

#include "markdown_renderer.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace deckkernel::docu {
namespace {

struct CmarkParser;
struct CmarkNode;
struct CmarkSyntaxExtension;
struct CmarkList;

struct CmarkMemory {
   void* (*fnCalloc)(std::size_t, std::size_t);
   void* (*fnRealloc)(void*, std::size_t);
   void (*fnFree)(void*);
   };


struct MarkdownHeading {
   std::size_t uLevel{};
   std::string strTitle;
   std::string strAnchor;
   };


struct TocPreparation {
   bool boEnabled{};
   std::string strMarkdown;
   std::string strTitle;
   std::string strToken;
   std::string strTocAnchor;
   std::size_t uMainLevel{};
   std::vector<MarkdownHeading> vecHeadings;
   };


class Module final {
public:
   explicit Module(std::filesystem::path const& aFile)
      : hModule{ ::LoadLibraryW(aFile.c_str()) } {
      if(hModule == nullptr) {
         throw std::runtime_error{
            "cmark-gfm runtime DLL could not be loaded: " + aFile.string()
            };
         }
      }

   Module(Module const&) = delete;
   Module& operator=(Module const&) = delete;

   ~Module() {
      if(hModule != nullptr) {
         ::FreeLibrary(hModule);
         }
      }

   [[nodiscard]] HMODULE Handle() const noexcept {
      return hModule;
      }

private:
   HMODULE hModule{};
   };


template <typename function_ty>
[[nodiscard]] function_ty Resolve(
   HMODULE const hModule,
   char const* const szName
) {
   // GetProcAddress is a C ABI boundary. The returned raw address is borrowed
   // from the loaded module and must not be released independently.
   FARPROC const pFunction = ::GetProcAddress(hModule, szName);

   if(pFunction == nullptr) {
      throw std::runtime_error{
         std::string{ "cmark-gfm function is missing: " } + szName
         };
      }

   return reinterpret_cast<function_ty>(pFunction);
   }


[[nodiscard]] std::string ReadMarkdown(
   std::filesystem::path const& aFile
) {
   std::ifstream isFile{ aFile, std::ios::binary };

   if(!isFile) {
      throw std::runtime_error{
         "Markdown file could not be opened: " + aFile.string()
         };
      }

   return std::string{
      std::istreambuf_iterator<char>{ isFile },
      std::istreambuf_iterator<char>{}
      };
   }


[[nodiscard]] std::string_view TrimHorizontalWhitespace(
   std::string_view svValue
) noexcept {
   while(!svValue.empty() &&
         (svValue.front() == ' ' || svValue.front() == '\t')) {
      svValue.remove_prefix(1U);
      }

   while(!svValue.empty() &&
         (svValue.back() == ' ' || svValue.back() == '\t')) {
      svValue.remove_suffix(1U);
      }

   return svValue;
   }


[[nodiscard]] std::string HtmlEscape(
   std::string_view const svValue
) {
   std::string strResult;
   strResult.reserve(svValue.size());

   for(char const chValue : svValue) {
      switch(chValue) {
         case '&': strResult += "&amp;"; break;
         case '<': strResult += "&lt;"; break;
         case '>': strResult += "&gt;"; break;
         case '"': strResult += "&quot;"; break;
         case '\'': strResult += "&#39;"; break;
         default: strResult += chValue; break;
         }
      }

   return strResult;
   }


[[nodiscard]] std::uint64_t MarkdownHash(
   std::string_view const svMarkdown
) noexcept {
   std::uint64_t uHash{ 14695981039346656037ULL };

   for(unsigned char const chValue : svMarkdown) {
      uHash ^= static_cast<std::uint64_t>(chValue);
      uHash *= 1099511628211ULL;
      }

   return uHash;
   }


[[nodiscard]] std::string PlainHeadingTitle(
   std::string_view const svTitle
) {
   std::string strResult;
   strResult.reserve(svTitle.size());

   for(char const chValue : svTitle) {
      if(chValue == '`' ||
         chValue == '*' ||
         chValue == '_') {
         continue;
         }

      strResult += chValue;
      }

   return strResult;
   }


[[nodiscard]] bool ParseTocDirective(
   std::string_view const svLine,
   std::string& strTitle
) {
   std::string_view const svTrimmed =
      TrimHorizontalWhitespace(svLine);

   if(!svTrimmed.starts_with("[TOC|") ||
      !svTrimmed.ends_with(']')) {
      return false;
      }

   std::string_view const svTitle =
      TrimHorizontalWhitespace(
         svTrimmed.substr(
            5U,
            svTrimmed.size() - 6U
            )
         );

   strTitle =
      svTitle.empty()
         ? "Content"
         : std::string{ svTitle };

   return true;
   }


[[nodiscard]] bool ParseHeading(
   std::string_view const svLine,
   std::size_t& uLevel,
   std::string& strTitle
) {
   std::string_view svValue =
      TrimHorizontalWhitespace(svLine);

   uLevel = 0U;

   while(uLevel < svValue.size() &&
         uLevel < 6U &&
         svValue[uLevel] == '#') {
      ++uLevel;
      }

   if(uLevel == 0U ||
      (
         uLevel < svValue.size() &&
         svValue[uLevel] != ' ' &&
         svValue[uLevel] != '\t'
      )) {
      return false;
      }

   svValue.remove_prefix(uLevel);
   svValue = TrimHorizontalWhitespace(svValue);

   std::size_t uHashBegin = svValue.size();

   while(uHashBegin > 0U &&
         svValue[uHashBegin - 1U] == '#') {
      --uHashBegin;
      }

   if(uHashBegin < svValue.size() &&
      uHashBegin > 0U &&
      (
         svValue[uHashBegin - 1U] == ' ' ||
         svValue[uHashBegin - 1U] == '\t'
      )) {
      svValue =
         TrimHorizontalWhitespace(
            svValue.substr(0U, uHashBegin - 1U)
            );
      }

   strTitle = PlainHeadingTitle(svValue);
   return true;
   }


[[nodiscard]] std::size_t FenceLength(
   std::string_view const svLine,
   char const chFence
) noexcept {
   std::size_t uLength{};

   while(uLength < svLine.size() &&
         svLine[uLength] == chFence) {
      ++uLength;
      }

   return uLength;
   }


[[nodiscard]] TocPreparation PrepareToc(
   std::string_view const svMarkdown
) {
   TocPreparation aResult;
   aResult.strMarkdown.reserve(
      svMarkdown.size() + 64U
      );

   std::uint64_t const uDocumentHash =
      MarkdownHash(svMarkdown);
   std::string const strHash =
      std::format("{:016x}", uDocumentHash);

   aResult.strToken =
      "DECKKERNEL_TOC_" + strHash;
   aResult.strTocAnchor =
      "dk-toc-" + strHash;

   bool boAfterToc{};
   bool boInFence{};
   char chFence{};
   std::size_t uFenceLength{};
   std::size_t uPosition{};

   while(uPosition < svMarkdown.size()) {
      std::size_t const uLineEnd =
         svMarkdown.find('\n', uPosition);
      std::size_t const uNext =
         uLineEnd == std::string_view::npos
            ? svMarkdown.size()
            : uLineEnd + 1U;
      std::size_t uContentEnd =
         uLineEnd == std::string_view::npos
            ? svMarkdown.size()
            : uLineEnd;

      if(uContentEnd > uPosition &&
         svMarkdown[uContentEnd - 1U] == '\r') {
         --uContentEnd;
         }

      std::string_view const svLine =
         svMarkdown.substr(
            uPosition,
            uContentEnd - uPosition
            );
      std::string_view const svTrimmed =
         TrimHorizontalWhitespace(svLine);

      bool boFenceLine{};

      if(!svTrimmed.empty() &&
         (
            svTrimmed.front() == '`' ||
            svTrimmed.front() == '~'
         )) {
         char const chCandidate =
            svTrimmed.front();
         std::size_t const uCandidateLength =
            FenceLength(
               svTrimmed,
               chCandidate
               );

         if(uCandidateLength >= 3U) {
            if(!boInFence) {
               boInFence = true;
               chFence = chCandidate;
               uFenceLength = uCandidateLength;
               boFenceLine = true;
               }
            else if(
               chCandidate == chFence &&
               uCandidateLength >= uFenceLength &&
               TrimHorizontalWhitespace(
                  svTrimmed.substr(uCandidateLength)
                  ).empty()
            ) {
               boInFence = false;
               chFence = 0;
               uFenceLength = 0U;
               boFenceLine = true;
               }
            }
         }

      if(!boFenceLine &&
         !boInFence) {
         std::string strDirectiveTitle;

         if(!aResult.boEnabled &&
            ParseTocDirective(
               svLine,
               strDirectiveTitle
               )) {
            aResult.boEnabled = true;
            boAfterToc = true;
            aResult.strTitle =
               std::move(strDirectiveTitle);
            aResult.strMarkdown +=
               aResult.strToken;

            if(uLineEnd != std::string_view::npos) {
               aResult.strMarkdown += '\n';
               }

            uPosition = uNext;
            continue;
            }

         if(boAfterToc) {
            std::size_t uLevel{};
            std::string strTitle;

            if(ParseHeading(
               svLine,
               uLevel,
               strTitle
               )) {
               if(aResult.uMainLevel == 0U ||
                  uLevel < aResult.uMainLevel) {
                  aResult.uMainLevel = uLevel;
                  }

               aResult.vecHeadings.emplace_back(
                  MarkdownHeading{
                     .uLevel = uLevel,
                     .strTitle = std::move(strTitle),
                     .strAnchor =
                        "dk-heading-" +
                        strHash +
                        "-" +
                        std::to_string(
                           aResult.vecHeadings.size() + 1U
                           )
                     }
                  );
               }
            }
         }

      aResult.strMarkdown.append(
         svMarkdown.substr(
            uPosition,
            uNext - uPosition
            )
         );
      uPosition = uNext;
      }

   if(!aResult.boEnabled) {
      aResult.strMarkdown.assign(svMarkdown);
      }

   return aResult;
   }


void AppendTocLevel(
   std::string& strHtml,
   std::vector<MarkdownHeading> const& vecHeadings,
   std::vector<std::size_t> const& vecParents,
   std::size_t const uParent
) {
   bool boOpened{};

   for(std::size_t uHeading{};
       uHeading < vecHeadings.size();
       ++uHeading) {
      if(vecParents[uHeading] != uParent) {
         continue;
         }

      if(!boOpened) {
         strHtml += "<ul>";
         boOpened = true;
         }

      MarkdownHeading const& aHeading =
         vecHeadings[uHeading];

      strHtml +=
         "<li><a href=\"#" +
         aHeading.strAnchor +
         "\">" +
         HtmlEscape(aHeading.strTitle) +
         "</a>";

      AppendTocLevel(
         strHtml,
         vecHeadings,
         vecParents,
         uHeading
         );

      strHtml += "</li>";
      }

   if(boOpened) {
      strHtml += "</ul>";
      }
   }


[[nodiscard]] std::string BuildTocHtml(
   TocPreparation const& aPreparation
) {
   constexpr std::size_t uNoParent =
      static_cast<std::size_t>(-1);

   std::vector<std::size_t> vecParents(
      aPreparation.vecHeadings.size(),
      uNoParent
      );

   for(std::size_t uHeading{};
       uHeading < aPreparation.vecHeadings.size();
       ++uHeading) {
      for(std::size_t uCandidate = uHeading;
          uCandidate > 0U;
          --uCandidate) {
         std::size_t const uPrevious =
            uCandidate - 1U;

         if(
            aPreparation.vecHeadings[uPrevious].uLevel <
            aPreparation.vecHeadings[uHeading].uLevel
         ) {
            vecParents[uHeading] = uPrevious;
            break;
            }
         }
      }

   std::string strHtml =
      "<a id=\"" +
      aPreparation.strTocAnchor +
      "\"></a>\n"
      "<nav class=\"markdown-toc\" aria-label=\"" +
      HtmlEscape(aPreparation.strTitle) +
      "\"><h2>" +
      HtmlEscape(aPreparation.strTitle) +
      "</h2>";

   AppendTocLevel(
      strHtml,
      aPreparation.vecHeadings,
      vecParents,
      uNoParent
      );

   strHtml += "</nav>\n";
   return strHtml;
   }


void ApplyTocHtml(
   std::string& strHtml,
   TocPreparation const& aPreparation
) {
   if(!aPreparation.boEnabled) {
      return;
      }

   std::string const strPlaceholder =
      "<p>" +
      aPreparation.strToken +
      "</p>";

   std::size_t const uPlaceholder =
      strHtml.find(strPlaceholder);

   if(uPlaceholder == std::string::npos) {
      throw std::runtime_error{
         "Markdown TOC placeholder was not rendered as expected"
         };
      }

   std::string const strToc =
      BuildTocHtml(aPreparation);

   strHtml.replace(
      uPlaceholder,
      strPlaceholder.size(),
      strToc
      );

   std::size_t uSearch =
      uPlaceholder + strToc.size();

   for(MarkdownHeading const& aHeading :
       aPreparation.vecHeadings) {
      std::string const strHeadingTag =
         "<h" +
         std::to_string(aHeading.uLevel) +
         ">";

      std::size_t uHeading =
         strHtml.find(
            strHeadingTag,
            uSearch
            );

      if(uHeading == std::string::npos) {
         throw std::runtime_error{
            "Markdown TOC heading sequence does not match rendered HTML"
            };
         }

      std::string strPrefix;

      if(aHeading.uLevel ==
         aPreparation.uMainLevel) {
         strPrefix +=
            "<p class=\"markdown-back-to-toc\">"
            "<a href=\"#" +
            aPreparation.strTocAnchor +
            "\">Back to " +
            HtmlEscape(aPreparation.strTitle) +
            "</a></p>\n";
         }

      if(!strPrefix.empty()) {
         strHtml.insert(
            uHeading,
            strPrefix
            );
         uHeading += strPrefix.size();
         }

      std::string const strAnchoredHeadingTag =
         "<h" +
         std::to_string(aHeading.uLevel) +
         " id=\"" +
         aHeading.strAnchor +
         "\">";

      strHtml.replace(
         uHeading,
         strHeadingTag.size(),
         strAnchoredHeadingTag
         );

      uSearch =
         uHeading +
         strAnchoredHeadingTag.size();
      }
   }


[[nodiscard]] bool ContainsMath(
   std::string_view const svMarkdown
) noexcept {
   return svMarkdown.find("$$") != std::string_view::npos ||
          svMarkdown.find("\\(") != std::string_view::npos ||
          svMarkdown.find("\\[") != std::string_view::npos;
   }


[[nodiscard]] MarkdownRenderResult RenderMarkdown(
   std::filesystem::path const& aRuntimeDirectory,
   std::string_view const svMarkdown
) {
   TocPreparation const aToc =
      PrepareToc(svMarkdown);
   std::string_view const svPreparedMarkdown{
      aToc.strMarkdown
      };

   Module const aCore{ aRuntimeDirectory / L"libcmark-gfm.dll" };
   Module const aExtensions{
      aRuntimeDirectory / L"libcmark-gfm-extensions.dll"
      };

   using parser_new_func = CmarkParser* (*)(int);
   using parser_free_func = void (*)(CmarkParser*);
   using parser_feed_func = void (*)(
      CmarkParser*,
      char const*,
      std::size_t
      );
   using parser_finish_func = CmarkNode* (*)(CmarkParser*);
   using node_free_func = void (*)(CmarkNode*);
   using find_extension_func = CmarkSyntaxExtension* (*)(char const*);
   using attach_extension_func = int (*)(
      CmarkParser*,
      CmarkSyntaxExtension*
      );
   using get_extensions_func = CmarkList* (*)(CmarkParser*);
   using render_html_func = char* (*)(
      CmarkNode*,
      int,
      CmarkList*
      );
   using get_memory_func = CmarkMemory* (*)();
   using ensure_extensions_func = void (*)();

   parser_new_func const fnParserNew =
      Resolve<parser_new_func>(aCore.Handle(), "cmark_parser_new");
   parser_free_func const fnParserFree =
      Resolve<parser_free_func>(aCore.Handle(), "cmark_parser_free");
   parser_feed_func const fnParserFeed =
      Resolve<parser_feed_func>(aCore.Handle(), "cmark_parser_feed");
   parser_finish_func const fnParserFinish =
      Resolve<parser_finish_func>(aCore.Handle(), "cmark_parser_finish");
   node_free_func const fnNodeFree =
      Resolve<node_free_func>(aCore.Handle(), "cmark_node_free");
   find_extension_func const fnFindExtension =
      Resolve<find_extension_func>(
         aCore.Handle(),
         "cmark_find_syntax_extension"
         );
   attach_extension_func const fnAttachExtension =
      Resolve<attach_extension_func>(
         aCore.Handle(),
         "cmark_parser_attach_syntax_extension"
         );
   get_extensions_func const fnGetExtensions =
      Resolve<get_extensions_func>(
         aCore.Handle(),
         "cmark_parser_get_syntax_extensions"
         );
   render_html_func const fnRenderHtml =
      Resolve<render_html_func>(aCore.Handle(), "cmark_render_html");
   get_memory_func const fnGetMemory =
      Resolve<get_memory_func>(
         aCore.Handle(),
         "cmark_get_default_mem_allocator"
         );
   ensure_extensions_func const fnEnsureExtensions =
      Resolve<ensure_extensions_func>(
         aExtensions.Handle(),
         "cmark_gfm_core_extensions_ensure_registered"
         );

   // cmark owns this descriptor. It is borrowed only while aCore is loaded.
   CmarkMemory* const pMemory = fnGetMemory();

   if(pMemory == nullptr || pMemory->fnFree == nullptr) {
      throw std::runtime_error{ "cmark-gfm allocator is unavailable" };
      }

   fnEnsureExtensions();

   struct ParserDeleter {
      parser_free_func fnFree{};

      void operator()(CmarkParser* pParser) const noexcept {
         if(pParser != nullptr && fnFree != nullptr) {
            fnFree(pParser);
            }
         }
      };

   struct NodeDeleter {
      node_free_func fnFree{};

      void operator()(CmarkNode* pNode) const noexcept {
         if(pNode != nullptr && fnFree != nullptr) {
            fnFree(pNode);
            }
         }
      };

   struct HtmlDeleter {
      CmarkMemory* pMemory{};

      void operator()(char* szHtml) const noexcept {
         if(szHtml != nullptr &&
            pMemory != nullptr &&
            pMemory->fnFree != nullptr) {
            pMemory->fnFree(szHtml);
            }
         }
      };

   using parser_ptr_ty =
      std::unique_ptr<CmarkParser, ParserDeleter>;
   using node_ptr_ty =
      std::unique_ptr<CmarkNode, NodeDeleter>;
   using html_ptr_ty =
      std::unique_ptr<char, HtmlDeleter>;

   parser_ptr_ty upParser{
      fnParserNew(0),
      ParserDeleter{ fnParserFree }
      };

   if(!upParser) {
      throw std::runtime_error{
         "cmark-gfm parser could not be created"
         };
      }

   constexpr std::array<char const*, 5U> aExtensionNames{
      "table",
      "tasklist",
      "strikethrough",
      "autolink",
      "tagfilter"
      };

   for(char const* const szExtension : aExtensionNames) {
      // Extension objects are owned by cmark-gfm. The parser only borrows them.
      CmarkSyntaxExtension* const pExtension =
         fnFindExtension(szExtension);

      if(pExtension == nullptr ||
         fnAttachExtension(upParser.get(), pExtension) == 0) {
         throw std::runtime_error{
            std::string{
               "cmark-gfm extension could not be attached: "
               } + szExtension
            };
         }
      }

   fnParserFeed(
      upParser.get(),
      svPreparedMarkdown.data(),
      svPreparedMarkdown.size()
      );

   node_ptr_ty upDocument{
      fnParserFinish(upParser.get()),
      NodeDeleter{ fnNodeFree }
      };

   if(!upDocument) {
      throw std::runtime_error{
         "cmark-gfm did not return a document"
         };
      }

   // The extension list belongs to the parser and is only borrowed here.
   CmarkList* const pExtensions =
      fnGetExtensions(upParser.get());

   html_ptr_ty upHtml{
      fnRenderHtml(
         upDocument.get(),
         0,
         pExtensions
         ),
      HtmlDeleter{ pMemory }
      };

   if(!upHtml) {
      throw std::runtime_error{
         "cmark-gfm HTML rendering failed"
         };
      }

   std::string strHtml{ upHtml.get() };
   ApplyTocHtml(
      strHtml,
      aToc
      );

   return MarkdownRenderResult{
      .strHtml = std::move(strHtml),
      .aFeatures = MarkdownRenderer::Analyze(svMarkdown)
      };
   }

} // namespace


MarkdownFeature MarkdownRenderer::Analyze(
   std::string_view const svMarkdown
) noexcept {
   MarkdownFeature aFeatures = MarkdownFeature::none;
   std::size_t uPosition{};
   std::string_view constexpr svFence{ "\x60\x60\x60" };

   while(
      (uPosition = svMarkdown.find(svFence, uPosition)) !=
      std::string_view::npos
   ) {
      std::size_t const uLanguageBegin =
         uPosition + svFence.size();
      std::size_t const uLineEnd =
         svMarkdown.find_first_of("\r\n", uLanguageBegin);

      std::string_view const svLanguage =
         TrimHorizontalWhitespace(
            svMarkdown.substr(
               uLanguageBegin,
               (
                  uLineEnd == std::string_view::npos
                     ? svMarkdown.size()
                     : uLineEnd
                  ) - uLanguageBegin
               )
            );

      if(svLanguage == "mermaid") {
         aFeatures |= MarkdownFeature::mermaid;
         }
      else if(!svLanguage.empty()) {
         aFeatures |= MarkdownFeature::syntaxHighlighting;
         }

      uPosition =
         uLineEnd == std::string_view::npos
            ? svMarkdown.size()
            : uLineEnd;
      }

   if(ContainsMath(svMarkdown)) {
      aFeatures |= MarkdownFeature::mathJax;
      }

   return aFeatures;
   }


void MarkdownRenderer::Validate(
   std::filesystem::path const& aRuntimeDirectory
) {
   Module const aCore{
      aRuntimeDirectory / L"libcmark-gfm.dll"
      };
   Module const aExtensions{
      aRuntimeDirectory / L"libcmark-gfm-extensions.dll"
      };

   (void)Resolve<void (*)()>(
      aExtensions.Handle(),
      "cmark_gfm_core_extensions_ensure_registered"
      );
   (void)Resolve<void* (*)(int)>(
      aCore.Handle(),
      "cmark_parser_new"
      );
   }


MarkdownRenderResult MarkdownRenderer::Render(
   std::filesystem::path const& aRuntimeDirectory,
   std::string_view const svMarkdown
) {
   return RenderMarkdown(aRuntimeDirectory, svMarkdown);
   }


MarkdownRenderResult MarkdownRenderer::RenderFile(
   std::filesystem::path const& aRuntimeDirectory,
   std::filesystem::path const& aFile
) {
   return RenderMarkdown(
      aRuntimeDirectory,
      ReadMarkdown(aFile)
      );
   }

} // namespace deckkernel::docu
