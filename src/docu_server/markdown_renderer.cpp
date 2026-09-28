// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

#include "markdown_renderer.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

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
      svMarkdown.data(),
      svMarkdown.size()
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

   return MarkdownRenderResult{
      .strHtml = std::string{ upHtml.get() },
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
