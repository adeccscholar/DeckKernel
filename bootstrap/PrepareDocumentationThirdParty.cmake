cmake_minimum_required(VERSION 3.25)

# Prepares the browser-side documentation dependencies as standalone ThirdParty
# archives. It does not change Cache/thirdparty.lock and does not modify an
# existing GitHub release. This keeps all already published package hashes
# byte-identical until the new assets have been uploaded and verified.

get_filename_component(
   DECKKERNEL_ROOT
   "${CMAKE_CURRENT_LIST_DIR}/.."
   ABSOLUTE
)

set(
   OUTPUT_ROOT
   "${DECKKERNEL_ROOT}/Cache/documentation-thirdparty"
)

set(
   STAGING_ROOT
   "${OUTPUT_ROOT}/staging"
)

file(REMOVE_RECURSE "${STAGING_ROOT}")
file(MAKE_DIRECTORY "${STAGING_ROOT}" "${OUTPUT_ROOT}/packages")

function(deckkernel_download
   theUrl
   theDestination
   theSha256
)
   get_filename_component(theDirectory "${theDestination}" DIRECTORY)
   file(MAKE_DIRECTORY "${theDirectory}")

   file(
      DOWNLOAD
      "${theUrl}"
      "${theDestination}"
      EXPECTED_HASH "SHA256=${theSha256}"
      TLS_VERIFY ON
      SHOW_PROGRESS
      STATUS theStatus
   )

   list(GET theStatus 0 theCode)
   list(GET theStatus 1 theText)

   if(NOT theCode EQUAL 0)
      file(REMOVE "${theDestination}")
      message(FATAL_ERROR
         "Download failed: ${theText}\n${theUrl}"
      )
   endif()
endfunction()


function(deckkernel_copy_license
   theSource
   theDestination
)
   if(NOT EXISTS "${theSource}")
      message(FATAL_ERROR
         "Required license file is missing: ${theSource}"
      )
   endif()

   get_filename_component(theDirectory "${theDestination}" DIRECTORY)
   file(MAKE_DIRECTORY "${theDirectory}")
   file(COPY_FILE "${theSource}" "${theDestination}")
endfunction()


function(deckkernel_archive
   thePackage
   theVersion
   theArchiveName
)
   set(thePackageRoot "${STAGING_ROOT}/${thePackage}")
   set(theVersionRoot "${thePackageRoot}/${theVersion}")
   set(theArchive "${OUTPUT_ROOT}/packages/${theArchiveName}")

   if(NOT EXISTS "${theVersionRoot}")
      message(FATAL_ERROR
         "Package staging root is missing: ${theVersionRoot}"
      )
   endif()

   file(REMOVE "${theArchive}")

   execute_process(
      COMMAND
         "${CMAKE_COMMAND}" -E tar cf
         "${theArchive}"
         --format=zip
         "${theVersion}"
      WORKING_DIRECTORY "${thePackageRoot}"
      RESULT_VARIABLE theResult
   )

   if(NOT theResult EQUAL 0)
      message(FATAL_ERROR
         "Could not create ${theArchive}"
      )
   endif()

   file(SIZE "${theArchive}" theSize)
   file(SHA256 "${theArchive}" theSha256)

   message(STATUS
      "[PACKAGE] ${thePackage} ${theVersion}\n"
      "          archive=${theArchiveName}\n"
      "          size=${theSize}\n"
      "          sha256=${theSha256}"
   )

   file(APPEND
      "${OUTPUT_ROOT}/lock-fragment.txt"
      "package|${thePackage}|${theVersion}|2026.09.26-1|"
      "packages/${theArchiveName}|${theSize}|${theSha256}\n"
   )

   file(APPEND
      "${OUTPUT_ROOT}/lock-fragment.xml"
      "   <package name=\"${thePackage}\" version=\"${theVersion}\" "
      "release=\"2026.09.26-1\" "
      "archive=\"packages/${theArchiveName}\" "
      "size=\"${theSize}\" sha256=\"${theSha256}\" />\n"
   )
endfunction()


file(WRITE
   "${OUTPUT_ROOT}/lock-fragment.txt"
   "# Append only after the archives have been uploaded to release 2026.09.26-1.\n"
)

file(WRITE
   "${OUTPUT_ROOT}/lock-fragment.xml"
   "<!-- Append only after upload and verification. -->\n"
)

# ---------------------------------------------------------------------------
# highlight.js 11.12.0
# ---------------------------------------------------------------------------

set(HIGHLIGHT_ROOT "${STAGING_ROOT}/highlightjs/11.12.0")

deckkernel_download(
   "https://cdn.jsdelivr.net/gh/highlightjs/cdn-release@11.12.0/build/highlight.min.js"
   "${HIGHLIGHT_ROOT}/web/highlight.min.js"
   "8ab71eb09c51f501e5e25157d9cff100e46cc29bcbfc744d0b746d451fca7f53"
)

deckkernel_download(
   "https://raw.githubusercontent.com/highlightjs/cdn-release/11.12.0/build/styles/github.min.css"
   "${HIGHLIGHT_ROOT}/web/styles/github.min.css"
   "5f5db2458549f8b86de973acd7d5d7b26ff5413c07f26aa3f42b054b943c3448"
)

deckkernel_copy_license(
   "${DECKKERNEL_ROOT}/licenses/highlight.js/11.12.0/LICENSE"
   "${HIGHLIGHT_ROOT}/LICENSE"
)

file(WRITE
   "${HIGHLIGHT_ROOT}/PACKAGE-METADATA.txt"
   "name=highlightjs\n"
   "version=11.12.0\n"
   "license=BSD-3-Clause\n"
   "runtime=web/highlight.min.js\n"
   "style=web/styles/github.min.css\n"
)

deckkernel_archive(
   highlightjs
   11.12.0
   "highlightjs-11.12.0-web.zip"
)

# ---------------------------------------------------------------------------
# Mermaid 11.17.2
# ---------------------------------------------------------------------------

set(MERMAID_ROOT "${STAGING_ROOT}/mermaid/11.17.2")

deckkernel_download(
   "https://cdn.jsdelivr.net/npm/mermaid@11.17.2/dist/mermaid.min.js"
   "${MERMAID_ROOT}/web/mermaid.min.js"
   "581ed7d74bd9048d0e3a91363927d72ef22942d7722546b27f7cc29e35390eb8"
)

deckkernel_copy_license(
   "${DECKKERNEL_ROOT}/licenses/mermaid/11.17.2/LICENSE"
   "${MERMAID_ROOT}/LICENSE"
)

if(EXISTS
   "${DECKKERNEL_ROOT}/licenses/mermaid/11.17.2/THIRD_PARTY_NOTICES.txt"
)
   deckkernel_copy_license(
      "${DECKKERNEL_ROOT}/licenses/mermaid/11.17.2/THIRD_PARTY_NOTICES.txt"
      "${MERMAID_ROOT}/THIRD_PARTY_NOTICES.txt"
   )
else()
   message(WARNING
      "Mermaid is a bundled browser distribution with transitive dependencies. "
      "The archive can be prepared for technical verification, but it must not "
      "be treated as release-ready until "
      "licenses/mermaid/11.17.2/THIRD_PARTY_NOTICES.txt has been generated "
      "from the exact Mermaid 11.17.2 production dependency closure."
   )
endif()

file(WRITE
   "${MERMAID_ROOT}/PACKAGE-METADATA.txt"
   "name=mermaid\n"
   "version=11.17.2\n"
   "project-license=MIT\n"
   "runtime=web/mermaid.min.js\n"
   "runtime-sha256=581ed7d74bd9048d0e3a91363927d72ef22942d7722546b27f7cc29e35390eb8\n"
)

deckkernel_archive(
   mermaid
   11.17.2
   "mermaid-11.17.2-web.zip"
)

# ---------------------------------------------------------------------------
# MathJax 3.2.2
# ---------------------------------------------------------------------------

set(MATHJAX_ROOT "${STAGING_ROOT}/mathjax/3.2.2")

deckkernel_download(
   "https://raw.githubusercontent.com/mathjax/MathJax/3.2.2/es5/tex-svg.js"
   "${MATHJAX_ROOT}/web/es5/tex-svg.js"
   "d4295dc33744836935c1399feece5159577b34c5c8ffb9f1c6324cd82e03a882"
)

deckkernel_copy_license(
   "${DECKKERNEL_ROOT}/licenses/mathjax/3.2.2/LICENSE"
   "${MATHJAX_ROOT}/LICENSE"
)

file(WRITE
   "${MATHJAX_ROOT}/PACKAGE-METADATA.txt"
   "name=mathjax\n"
   "version=3.2.2\n"
   "license=Apache-2.0\n"
   "runtime=web/es5/tex-svg.js\n"
)

deckkernel_archive(
   mathjax
   3.2.2
   "mathjax-3.2.2-web.zip"
)

message(STATUS
   "Documentation ThirdParty packages prepared below:\n"
   "  ${OUTPUT_ROOT}/packages\n"
   "No existing release asset or lock-file entry was modified."
)
