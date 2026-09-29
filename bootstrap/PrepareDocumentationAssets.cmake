cmake_minimum_required(VERSION 3.25)

# ---------------------------------------------------------------------------
# DeckKernel documentation browser assets
#
# This script is called by bootstrap/Bootstrap.cmake.
# Docs/js is generated output and is intentionally not tracked by Git.
# ---------------------------------------------------------------------------

get_filename_component(ADECC_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(ADECC_DOCS_JS_ROOT "${ADECC_REPOSITORY_ROOT}/Docs/js")
set(ADECC_DOCS_CLIENT_SOURCE
   "${ADECC_REPOSITORY_ROOT}/src/docu_server/web/docu_client.js"
)

set(ADECC_HIGHLIGHT_VERSION "11.12.0")
set(ADECC_MERMAID_VERSION "11.17.2")
set(ADECC_MATHJAX_VERSION "3.2.2")

set(ADECC_DOCS_ASSET_SIGNATURE
   "highlight=${ADECC_HIGHLIGHT_VERSION};mermaid=${ADECC_MERMAID_VERSION};mathjax=${ADECC_MATHJAX_VERSION}"
)
set(ADECC_DOCS_ASSET_STATE "${ADECC_DOCS_JS_ROOT}/.assets-version")

function(_adecc_download_documentation_asset theName theUrl theFile)
   set(_target "${ADECC_DOCS_JS_ROOT}/${theFile}")
   set(_part "${_target}.part")

   file(REMOVE "${_part}")

   message(STATUS "[DOC-ASSET] ${theName}")
   file(
      DOWNLOAD
      "${theUrl}"
      "${_part}"
      TLS_VERIFY ON
      SHOW_PROGRESS
      STATUS _download_status
   )

   list(GET _download_status 0 _download_code)
   list(GET _download_status 1 _download_text)

   if(NOT _download_code EQUAL 0)
      file(REMOVE "${_part}")
      message(FATAL_ERROR
         "Documentation asset download failed for ${theName}: "
         "${_download_text}\n${theUrl}"
      )
   endif()

   file(SIZE "${_part}" _download_size)
   if(_download_size EQUAL 0)
      file(REMOVE "${_part}")
      message(FATAL_ERROR
         "Documentation asset download returned an empty file: ${theName}"
      )
   endif()

   file(RENAME "${_part}" "${_target}")
endfunction()

set(_adecc_docs_assets_ready FALSE)

if(EXISTS "${ADECC_DOCS_ASSET_STATE}")
   file(READ "${ADECC_DOCS_ASSET_STATE}" _adecc_docs_asset_signature)
   string(STRIP "${_adecc_docs_asset_signature}" _adecc_docs_asset_signature)

   if(_adecc_docs_asset_signature STREQUAL ADECC_DOCS_ASSET_SIGNATURE AND
      EXISTS "${ADECC_DOCS_JS_ROOT}/docu_client.js" AND
      EXISTS "${ADECC_DOCS_JS_ROOT}/highlight.min.js" AND
      EXISTS "${ADECC_DOCS_JS_ROOT}/github.min.css" AND
      EXISTS "${ADECC_DOCS_JS_ROOT}/mermaid.min.js" AND
      EXISTS "${ADECC_DOCS_JS_ROOT}/mathjax-tex-svg.js")
      set(_adecc_docs_assets_ready TRUE)
   endif()
endif()

if(_adecc_docs_assets_ready)
   message(STATUS "[DOC-ASSET] browser assets already prepared")
   return()
endif()

file(REMOVE_RECURSE "${ADECC_DOCS_JS_ROOT}")
file(MAKE_DIRECTORY "${ADECC_DOCS_JS_ROOT}")

if(NOT EXISTS "${ADECC_DOCS_CLIENT_SOURCE}")
   message(FATAL_ERROR
      "DeckKernel documentation client source is missing: "
      "${ADECC_DOCS_CLIENT_SOURCE}"
   )
endif()

file(
   COPY_FILE
   "${ADECC_DOCS_CLIENT_SOURCE}"
   "${ADECC_DOCS_JS_ROOT}/docu_client.js"
   ONLY_IF_DIFFERENT
)

_adecc_download_documentation_asset(
   "highlight.js ${ADECC_HIGHLIGHT_VERSION}"
   "https://raw.githubusercontent.com/highlightjs/cdn-release/${ADECC_HIGHLIGHT_VERSION}/build/highlight.min.js"
   "highlight.min.js"
)

_adecc_download_documentation_asset(
   "highlight.js GitHub stylesheet ${ADECC_HIGHLIGHT_VERSION}"
   "https://raw.githubusercontent.com/highlightjs/cdn-release/${ADECC_HIGHLIGHT_VERSION}/build/styles/github.min.css"
   "github.min.css"
)

_adecc_download_documentation_asset(
   "Mermaid ${ADECC_MERMAID_VERSION}"
   "https://cdn.jsdelivr.net/npm/mermaid@${ADECC_MERMAID_VERSION}/dist/mermaid.min.js"
   "mermaid.min.js"
)

_adecc_download_documentation_asset(
   "MathJax ${ADECC_MATHJAX_VERSION}"
   "https://cdn.jsdelivr.net/npm/mathjax@${ADECC_MATHJAX_VERSION}/es5/tex-svg.js"
   "mathjax-tex-svg.js"
)

file(WRITE "${ADECC_DOCS_ASSET_STATE}" "${ADECC_DOCS_ASSET_SIGNATURE}\n")

message(STATUS "Documentation assets: ${ADECC_DOCS_JS_ROOT}")
