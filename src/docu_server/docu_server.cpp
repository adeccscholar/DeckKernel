// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file docu_server.cpp
\brief HTTP delivery and browser page assembly for DeckKernel documentation.
*/

#include "docu_server.h"
#include "markdown_renderer.h"

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <print>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace deckkernel::docu {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;


[[nodiscard]] std::string HtmlEscape(
   std::string_view const svText
) {
   std::string strResult;
   strResult.reserve(svText.size());

   for(char const chValue : svText) {
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


[[nodiscard]] std::string Lower(
   std::string strValue
) {
   std::ranges::transform(
      strValue,
      strValue.begin(),
      [](unsigned char const chValue) {
         return static_cast<char>(std::tolower(chValue));
         }
      );

   return strValue;
   }


[[nodiscard]] std::string ContentType(
   std::filesystem::path const& aFile
) {
   std::string const strExtension =
      Lower(aFile.extension().string());

   if(strExtension == ".html" || strExtension == ".htm") {
      return "text/html; charset=utf-8";
      }
   if(strExtension == ".css") {
      return "text/css; charset=utf-8";
      }
   if(strExtension == ".js" || strExtension == ".mjs") {
      return "text/javascript; charset=utf-8";
      }
   if(strExtension == ".svg") {
      return "image/svg+xml";
      }
   if(strExtension == ".png") {
      return "image/png";
      }
   if(strExtension == ".jpg" || strExtension == ".jpeg") {
      return "image/jpeg";
      }
   if(strExtension == ".gif") {
      return "image/gif";
      }
   if(strExtension == ".webp") {
      return "image/webp";
      }
   if(strExtension == ".pdf") {
      return "application/pdf";
      }

   return "application/octet-stream";
   }


[[nodiscard]] std::string PercentDecode(
   std::string_view const svValue
) {
   auto const fnHex = [](char const chValue) -> int {
      if(chValue >= '0' && chValue <= '9') {
         return chValue - '0';
         }
      if(chValue >= 'a' && chValue <= 'f') {
         return chValue - 'a' + 10;
         }
      if(chValue >= 'A' && chValue <= 'F') {
         return chValue - 'A' + 10;
         }
      return -1;
      };

   std::string strResult;
   strResult.reserve(svValue.size());

   for(std::size_t uIndex{}; uIndex < svValue.size(); ++uIndex) {
      if(svValue[uIndex] == '%' && uIndex + 2U < svValue.size()) {
         int const iHigh = fnHex(svValue[uIndex + 1U]);
         int const iLow = fnHex(svValue[uIndex + 2U]);

         if(iHigh >= 0 && iLow >= 0) {
            strResult += static_cast<char>((iHigh << 4) | iLow);
            uIndex += 2U;
            continue;
            }
         }

      strResult += svValue[uIndex];
      }

   return strResult;
   }


[[nodiscard]] std::string PercentEncode(
   std::string_view const svValue
) {
   constexpr char szHex[] = "0123456789ABCDEF";
   std::string strResult;

   for(unsigned char const chValue : svValue) {
      bool const boSafe =
         std::isalnum(chValue) != 0 ||
         chValue == '-' ||
         chValue == '_' ||
         chValue == '.' ||
         chValue == '/' ||
         chValue == '~';

      if(boSafe) {
         strResult += static_cast<char>(chValue);
         }
      else {
         strResult += '%';
         strResult += szHex[(chValue >> 4) & 0x0f];
         strResult += szHex[chValue & 0x0f];
         }
      }

   return strResult;
   }


[[nodiscard]] std::string_view WithoutQuery(
   std::string_view const svTarget
) noexcept {
   std::size_t const uQuestion = svTarget.find('?');
   return svTarget.substr(0U, uQuestion);
   }


[[nodiscard]] std::string HostNameOnly(
   std::string_view const svHost
) {
   std::string strHost{ svHost };

   while(!strHost.empty() &&
         std::isspace(
            static_cast<unsigned char>(strHost.front())
            ) != 0) {
      strHost.erase(strHost.begin());
      }

   while(!strHost.empty() &&
         std::isspace(
            static_cast<unsigned char>(strHost.back())
            ) != 0) {
      strHost.pop_back();
      }

   if(strHost.empty()) {
      return strHost;
      }

   if(strHost.front() == '[') {
      std::size_t const uClose = strHost.find(']');
      if(uClose != std::string::npos) {
         return Lower(
            strHost.substr(
               1U,
               uClose - 1U
               )
            );
         }
      }

   std::size_t const uFirstColon = strHost.find(':');
   std::size_t const uLastColon = strHost.rfind(':');

   if(uFirstColon != std::string::npos &&
      uFirstColon == uLastColon) {
      strHost.resize(uFirstColon);
      }

   return Lower(std::move(strHost));
   }


[[nodiscard]] bool IsLoopbackName(
   std::string_view const svHost
) noexcept {
   return svHost == "localhost" ||
          svHost == "127.0.0.1" ||
          svHost == "::1";
   }


[[nodiscard]] bool HostAllowed(
   http::request<http::string_body> const& aRequest,
   ServerConfiguration const& aConfiguration
) {
   auto const svHost =
      aRequest[http::field::host];

   std::string const strHost =
      HostNameOnly(
         std::string_view{
            svHost.data(),
            svHost.size()
            }
         );

   if(strHost.empty()) {
      return false;
      }

   std::string const strServerName =
      Lower(aConfiguration.strServerName);
   std::string const strBindAddress =
      Lower(aConfiguration.strBindAddress);

   if(strHost == strServerName ||
      strHost == strBindAddress) {
      return true;
      }

   boost::system::error_code aAddressError;
   asio::ip::address const aAddress =
      asio::ip::make_address(
         aConfiguration.strBindAddress,
         aAddressError
         );

   return !aAddressError &&
          aAddress.is_loopback() &&
          IsLoopbackName(strHost);
   }


[[nodiscard]] std::filesystem::path SafeRelativeFile(
   std::filesystem::path const& aRoot,
   std::string_view const svRequestPath
) {
   std::string const strDecoded = PercentDecode(svRequestPath);
   std::filesystem::path aRelative;

   std::size_t uStart{};
   while(uStart < strDecoded.size()) {
      while(uStart < strDecoded.size() && strDecoded[uStart] == '/') {
         ++uStart;
         }

      if(uStart >= strDecoded.size()) {
         break;
         }

      std::size_t const uEnd = strDecoded.find('/', uStart);
      std::string const strSegment =
         strDecoded.substr(
            uStart,
            uEnd == std::string::npos
               ? std::string::npos
               : uEnd - uStart
            );

      if(strSegment.empty() ||
         strSegment == "." ||
         strSegment == ".." ||
         strSegment.find('\\') != std::string::npos ||
         strSegment.find(':') != std::string::npos) {
         throw std::runtime_error{
            "invalid documentation path"
            };
         }

      aRelative /= strSegment;

      if(uEnd == std::string::npos) {
         break;
         }

      uStart = uEnd + 1U;
      }

   return (aRoot / aRelative).lexically_normal();
   }


[[nodiscard]] std::string Page(
   std::string_view const svTitle,
   std::string_view const svBody,
   MarkdownFeature const aFeatures,
   bool const boDocument
) {
   bool const boHighlight = HasMarkdownFeature(
      aFeatures,
      MarkdownFeature::syntaxHighlighting
      );
   bool const boMermaid = HasMarkdownFeature(
      aFeatures,
      MarkdownFeature::mermaid
      );
   bool const boMathJax = HasMarkdownFeature(
      aFeatures,
      MarkdownFeature::mathJax
      );

   std::ostringstream osHtml;

   osHtml
      << "<!doctype html><html lang=\"en\"><head>"
      << "<meta charset=\"utf-8\">"
      << "<meta name=\"viewport\" "
         "content=\"width=device-width,initial-scale=1\">"
      << "<title>" << HtmlEscape(svTitle) << "</title>";

   if(boHighlight) {
      osHtml
         << "<link rel=\"stylesheet\" "
            "href=\"/docs/js/github.min.css\">";
      }

   osHtml
      << "<style>"
      << ":root{font-family:Inter,Segoe UI,Arial,sans-serif;"
         "color:#172033;background:#f4f7fb}"
      << "*{box-sizing:border-box}"
      << "body{margin:0;min-height:100vh;background:#f4f7fb;color:#172033}"
      << "header{position:sticky;top:0;z-index:3;background:#172033;"
         "color:white;padding:10px 24px;box-shadow:0 8px 24px rgba(15,23,42,.12)}"
      << ".nav{max-width:1280px;margin:auto;display:flex;align-items:center;"
         "gap:8px;flex-wrap:wrap}"
      << ".brand{font-weight:750;margin-right:12px}"
      << ".nav a,.print-button{border:1px solid #48627f;border-radius:8px;"
         "padding:6px 11px;color:#e8f1ff;background:#27384e;text-decoration:none;"
         "font:inherit;font-weight:600;cursor:pointer}"
      << ".print-button{margin-left:auto}"
      << "main{padding:28px;max-width:1280px;margin:auto}"
      << ".hero{padding:22px 26px;border-radius:16px;background:#274568;"
         "color:white;margin-bottom:20px}"
      << ".markdown{background:white;border:1px solid #d9e2ef;border-radius:14px;"
         "padding:30px;box-shadow:0 8px 24px rgba(15,23,42,.06);line-height:1.65}"
      << ".markdown h1{border-bottom:1px solid #d9e2ef;padding-bottom:.35em}"
      << ".markdown blockquote{margin-left:0;border-left:4px solid #94a3b8;"
         "padding-left:16px;color:#475569}"
      << ".markdown-toc{margin:1.25rem 0 2rem;padding:16px 20px;"
         "border:1px solid #d9e2ef;border-radius:10px;background:#f8fafc}"
      << ".markdown-toc h2{margin-top:0}"
      << ".markdown-toc ul{margin:.35rem 0 .35rem 1.2rem;padding-left:1rem}"
      << ".markdown-back-to-toc{font-size:.9rem;margin:.2rem 0 .5rem}"
      << ".markdown-back-to-toc a{text-decoration:none}"
      << ".markdown code{font-family:Consolas,monospace}"
      << ".markdown pre{position:relative;overflow:auto;background:#f8fafc;"
         "border:1px solid #e2e8f0;border-radius:10px;padding:14px}"
      << ".markdown pre.copyable-code{padding-top:42px}"
      << ".copy-code-button{position:absolute;right:8px;top:8px;"
         "border:1px solid #94a3b8;border-radius:6px;background:white;"
         "padding:4px 9px;font:600 12px Segoe UI,Arial,sans-serif;cursor:pointer}"
      << ".markdown table{border-collapse:collapse;width:100%;margin:16px 0}"
      << ".markdown th,.markdown td{border:1px solid #d9e2ef;padding:8px 10px;"
         "text-align:left;vertical-align:top}"
      << ".markdown img{max-width:100%;height:auto}"
      << ".markdown .mermaid{margin:1.25rem 0;display:flex;justify-content:center;"
         "overflow:auto}"
      << ".markdown .mermaid svg{display:block;max-width:100%!important;"
         "height:auto!important;margin:0 auto}"
      << ".doc-list{background:white;border:1px solid #d9e2ef;border-radius:14px;"
         "padding:22px;box-shadow:0 8px 24px rgba(15,23,42,.06)}"
      << ".doc-list li{margin:.45rem 0}"
      << "@media print{"
         "@page{margin:15mm}"
         "header,.hero,.print-button,.copy-code-button,.markdown-toc,"
         ".markdown-back-to-toc{display:none!important}"
         "body{background:white;color:black}"
         "main{max-width:none;padding:0;margin:0}"
         ".markdown{border:0;border-radius:0;box-shadow:none;padding:0}"
         ".markdown pre,.markdown table,.markdown blockquote,.markdown .mermaid{"
         "break-inside:avoid;page-break-inside:avoid}"
         ".markdown a{color:black;text-decoration:none}"
         ".markdown a[href]:after{content:\" (\" attr(href) \")\";"
         "font-size:.8em;word-break:break-all}"
         ".markdown a[href^=\"#\"]:after{content:\"\"}"
         "}"
      << "</style></head><body>"
      << "<header><nav class=\"nav\">"
      << "<span class=\"brand\">DeckKernel Documentation</span>"
      << "<a href=\"/docs/\">Documents</a>";

   if(boDocument) {
      osHtml
         << "<button id=\"print-document\" class=\"print-button\" "
            "type=\"button\">Print</button>";
      }

   osHtml
      << "</nav></header><main>"
      << svBody
      << "</main>";

   if(boMathJax) {
      osHtml
         << "<script>"
            "window.MathJax={tex:{inlineMath:[[\'$\',\'$\'],"
            "[\'\\\\(\',\'\\\\)\']],displayMath:[[\'$$\',\'$$\'],"
            "[\'\\\\[\',\'\\\\]\']]},svg:{fontCache:\'global\'}};"
            "</script>"
         << "<script defer src=\"/docs/js/mathjax-tex-svg.js\"></script>";
      }

   if(boHighlight) {
      osHtml
         << "<script defer src=\"/docs/js/highlight.min.js\"></script>"
         << "<script>"
            "window.addEventListener(\'DOMContentLoaded\',()=>{"
            "if(window.hljs)window.hljs.highlightAll();});"
            "</script>";
      }

   if(boMermaid) {
      osHtml
         << "<script defer src=\"/docs/js/mermaid.min.js\"></script>"
         << "<script>"
            "window.addEventListener(\'DOMContentLoaded\',async()=>{"
            "document.querySelectorAll(\'pre code.language-mermaid\')"
            ".forEach((code)=>{const div=document.createElement(\'div\');"
            "div.className=\'mermaid\';div.textContent=code.textContent;"
            "code.parentElement.replaceWith(div);});"
            "if(window.mermaid){window.mermaid.initialize({startOnLoad:false,"
            "securityLevel:\'strict\'});"
            "await window.mermaid.run({querySelector:\'.mermaid\'});}});"
            "</script>";
      }

   osHtml
      << "<script defer src=\"/docs/js/docu_client.js\"></script>"
      << "</body></html>";
   return osHtml.str();
   }


[[nodiscard]] std::string DocumentPage(
   std::filesystem::path const& aRuntimeDirectory,
   std::filesystem::path const& aFile
) {
   MarkdownRenderResult const aRendered =
      MarkdownRenderer::RenderFile(
         aRuntimeDirectory,
         aFile
         );

   std::ostringstream osBody;
   osBody
      << "<section class=\"hero\"><h1>"
      << HtmlEscape(aFile.filename().string())
      << "</h1><p>Rendered directly from the DeckKernel Markdown source.</p>"
      << "</section><article class=\"markdown\">"
      << aRendered.strHtml
      << "</article>";

   return Page(
      aFile.filename().string(),
      osBody.str(),
      aRendered.aFeatures,
      true
      );
   }


[[nodiscard]] std::string IndexPage(
   std::filesystem::path const& aDocsRoot
) {
   std::vector<std::filesystem::path> vecDocuments;

   for(auto const& aEntry :
       std::filesystem::recursive_directory_iterator{ aDocsRoot }) {
      if(!aEntry.is_regular_file()) {
         continue;
         }

      std::string const strExtension =
         Lower(aEntry.path().extension().string());

      if(strExtension == ".md") {
         vecDocuments.emplace_back(
            std::filesystem::relative(
               aEntry.path(),
               aDocsRoot
               )
            );
         }
      }

   std::ranges::sort(vecDocuments);

   std::ostringstream osBody;
   osBody
      << "<section class=\"hero\"><h1>DeckKernel Documentation</h1>"
      << "<p>Markdown sources from the repository Docs directory.</p></section>";

   std::string strCurrentCategory;

   for(std::filesystem::path const& aDocument : vecDocuments) {
      std::string const strPath = aDocument.generic_string();
      std::string strCategory =
         aDocument.parent_path().generic_string();

      if(strCategory.empty()) {
         strCategory = "overview";
         }

      if(strCategory != strCurrentCategory) {
         if(!strCurrentCategory.empty()) {
            osBody << "</ul></section>";
            }

         strCurrentCategory = strCategory;
         osBody
            << "<section class=\"doc-list\"><h2>"
            << HtmlEscape(strCurrentCategory)
            << "</h2><ul>";
         }

      osBody
         << "<li><a href=\"/docs/"
         << PercentEncode(strPath)
         << "\">"
         << HtmlEscape(aDocument.filename().string())
         << "</a></li>";
      }

   if(!strCurrentCategory.empty()) {
      osBody << "</ul></section>";
      }

   return Page(
      "DeckKernel Documentation",
      osBody.str(),
      MarkdownFeature::none,
      false
      );
   }


template <typename stream_ty>
void WriteHtml(
   stream_ty& aStream,
   http::request<http::string_body> const& aRequest,
   http::status const aStatus,
   std::string strBody
) {
   http::response<http::string_body> aResponse{
      aStatus,
      aRequest.version()
      };

   aResponse.set(http::field::server, "DeckKernelDocServer");
   aResponse.set(
      http::field::content_type,
      "text/html; charset=utf-8"
      );
   aResponse.keep_alive(false);
   aResponse.body() = std::move(strBody);
   aResponse.prepare_payload();

   http::write(aStream, aResponse);
   }


template <typename stream_ty>
void WriteTextError(
   stream_ty& aStream,
   http::request<http::string_body> const& aRequest,
   http::status const aStatus,
   std::string_view const svMessage
) {
   http::response<http::string_body> aResponse{
      aStatus,
      aRequest.version()
      };

   aResponse.set(http::field::server, "DeckKernelDocServer");
   aResponse.set(
      http::field::content_type,
      "text/plain; charset=utf-8"
      );
   aResponse.keep_alive(false);
   aResponse.body() = std::string{ svMessage } + "\n";
   aResponse.prepare_payload();

   http::write(aStream, aResponse);
   }


template <typename stream_ty>
void WriteFile(
   stream_ty& aStream,
   http::request<http::string_body> const& aRequest,
   std::filesystem::path const& aFile
) {
   beast::error_code aError;
   http::file_body::value_type aBody;

   std::string const strFile = aFile.string();
   aBody.open(
      strFile.c_str(),
      beast::file_mode::scan,
      aError
      );

   if(aError) {
      throw std::runtime_error{
         "documentation asset could not be opened"
         };
      }

   std::uint64_t const uSize = aBody.size();

   http::response<http::file_body> aResponse{
      std::piecewise_construct,
      std::make_tuple(std::move(aBody)),
      std::make_tuple(
         http::status::ok,
         aRequest.version()
         )
      };

   aResponse.set(http::field::server, "DeckKernelDocServer");
   aResponse.set(
      http::field::content_type,
      ContentType(aFile)
      );
   aResponse.content_length(uSize);
   aResponse.keep_alive(false);

   http::write(aStream, aResponse);
   }

} // namespace


DocuServer::DocuServer(
   ServerConfiguration aNewConfiguration
)
   : aConfiguration{ std::move(aNewConfiguration) } {
   aConfiguration.aRepositoryRoot =
      std::filesystem::absolute(
         aConfiguration.aRepositoryRoot
         ).lexically_normal();

   aConfiguration.aRuntimeDirectory =
      std::filesystem::absolute(
         aConfiguration.aRuntimeDirectory
         ).lexically_normal();

   std::filesystem::path const aDocsRoot =
      aConfiguration.aRepositoryRoot / L"Docs";

   if(!std::filesystem::is_directory(aDocsRoot)) {
      throw std::runtime_error{
         "DeckKernel Docs directory is missing: " +
         aDocsRoot.string()
         };
      }

   MarkdownRenderer::Validate(
      aConfiguration.aRuntimeDirectory
      );
   }


void DocuServer::Run() {
   std::filesystem::path const aDocsRoot =
      aConfiguration.aRepositoryRoot / L"Docs";

   asio::io_context aContext{ 1 };

   boost::system::error_code aAddressError;
   asio::ip::address const aAddress =
      asio::ip::make_address(
         aConfiguration.strBindAddress,
         aAddressError
         );

   if(aAddressError) {
      throw std::runtime_error{
         "server address must be an IP address"
         };
      }

   if(aAddress.is_unspecified() ||
      aAddress.is_multicast()) {
      throw std::runtime_error{
         "server address must identify one concrete interface"
         };
      }

   tcp::endpoint const aEndpoint{
      aAddress,
      aConfiguration.uPort
      };

   tcp::acceptor aAcceptor{
      aContext,
      aEndpoint
      };

   std::println(
      "DeckKernelDocServer: http://{}:{}/docs/",
      aConfiguration.strBindAddress,
      aConfiguration.uPort
      );

   for(;;) {
      tcp::socket aSocket{ aContext };
      aAcceptor.accept(aSocket);

      try {
         beast::flat_buffer aBuffer;
         http::request<http::string_body> aRequest;
         http::read(
            aSocket,
            aBuffer,
            aRequest
            );

         if(aRequest.method() != http::verb::get) {
            WriteTextError(
               aSocket,
               aRequest,
               http::status::method_not_allowed,
               "only GET is supported"
               );
            }
         else if(!HostAllowed(
            aRequest,
            aConfiguration
            )) {
            WriteTextError(
               aSocket,
               aRequest,
               http::status::bad_request,
               "invalid Host header"
               );
            }
         else {
            std::string const strTarget{
               aRequest.target().data(),
               aRequest.target().size()
               };
            std::string_view const svPath =
               WithoutQuery(strTarget);

            if(svPath == "/" ||
               svPath == "/docs" ||
               svPath == "/docs/") {
               WriteHtml(
                  aSocket,
                  aRequest,
                  http::status::ok,
                  IndexPage(aDocsRoot)
                  );
               }
            else if(svPath.starts_with("/docs/")) {
               std::filesystem::path const aFile =
                  SafeRelativeFile(
                     aDocsRoot,
                     svPath.substr(6U)
                     );

               if(!std::filesystem::is_regular_file(aFile)) {
                  WriteTextError(
                     aSocket,
                     aRequest,
                     http::status::not_found,
                     "documentation file not found"
                     );
                  }
               else if(Lower(aFile.extension().string()) == ".md") {
                  WriteHtml(
                     aSocket,
                     aRequest,
                     http::status::ok,
                     DocumentPage(
                        aConfiguration.aRuntimeDirectory,
                        aFile
                        )
                     );
                  }
               else {
                  WriteFile(
                     aSocket,
                     aRequest,
                     aFile
                     );
                  }
               }
            else if(
               svPath.size() > 1U &&
               svPath.find('/', 1U) == std::string_view::npos
            ) {
               // Root-level Markdown remains addressable so documents moved
               // to Docs can still reference ../bootstrap_readme.md.
               std::filesystem::path const aFile =
                  SafeRelativeFile(
                     aConfiguration.aRepositoryRoot,
                     svPath.substr(1U)
                     );

               if(std::filesystem::is_regular_file(aFile) &&
                  Lower(aFile.extension().string()) == ".md") {
                  WriteHtml(
                     aSocket,
                     aRequest,
                     http::status::ok,
                     DocumentPage(
                        aConfiguration.aRuntimeDirectory,
                        aFile
                        )
                     );
                  }
               else {
                  WriteTextError(
                     aSocket,
                     aRequest,
                     http::status::not_found,
                     "route not found"
                     );
                  }
               }
            else {
               WriteTextError(
                  aSocket,
                  aRequest,
                  http::status::not_found,
                  "route not found"
                  );
               }
            }
         }
      catch(std::exception const& aException) {
         try {
            http::request<http::string_body> const aSynthetic{
               http::verb::get,
               "/",
               11
               };

            WriteTextError(
               aSocket,
               aSynthetic,
               http::status::internal_server_error,
               aException.what()
               );
            }
         catch(...) {
            }
         }

      beast::error_code aShutdownError;
      aSocket.shutdown(
         tcp::socket::shutdown_send,
         aShutdownError
         );
      }
   }

} // namespace deckkernel::docu
