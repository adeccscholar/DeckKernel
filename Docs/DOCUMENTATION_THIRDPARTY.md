# Documentation ThirdParty extension

The documentation server adds four runtime dependencies:

| Component | Version | Distribution role | License status |
| --- | ---: | --- | --- |
| cmark-gfm | 0.29.0.gfm.13 | native Markdown parser | BSD-style main license plus bundled notices |
| highlight.js | 11.12.0 | browser-side syntax highlighting | BSD-3-Clause |
| Mermaid | 11.17.2 | browser-side diagrams | MIT project license; bundled dependency notices required |
| MathJax | 3.2.2 | browser-side TeX/LaTeX rendering | Apache-2.0 |

## Release extension strategy

The existing DeckKernel ThirdParty release is:

`2026.09.26-1`

The existing native archives must remain byte-identical. Their recorded sizes and SHA-256
digests are not changed.

The release can be extended by adding three new, independent web packages:

```text
highlightjs-11.12.0-web.zip
mermaid-11.17.2-web.zip
mathjax-3.2.2-web.zip
```

cmark-gfm is already present in the release as:

```text
cmark-gfm-0.29.0.gfm.13-bcc64x.zip
```

Only after the three new archives have been uploaded and their GitHub release sizes and
SHA-256 digests have been verified should their package records be appended to
`Cache/thirdparty.lock` and `Cache/thirdparty.lock.xml`.

This avoids creating a repository state in which bootstrap refers to release assets that
do not yet exist.

## Preparing the new archives

Run:

```bat
cmake -P bootstrap\PrepareDocumentationThirdParty.cmake
```

The script writes the new archives to:

```text
Cache\documentation-thirdparty\packages
```

and generates candidate lock fragments:

```text
Cache\documentation-thirdparty\lock-fragment.txt
Cache\documentation-thirdparty\lock-fragment.xml
```

The preparation step never rewrites an existing release asset and never edits the current
lock files.

## Package layout

The web packages use the same generic ThirdParty extraction contract as the native
packages.

Example:

```text
ThirdParty/
   highlightjs/
      11.12.0/
         LICENSE
         PACKAGE-METADATA.txt
         web/
            highlight.min.js
            styles/
               github.min.css

   mermaid/
      11.17.2/
         LICENSE
         THIRD_PARTY_NOTICES.txt
         PACKAGE-METADATA.txt
         web/
            mermaid.min.js

   mathjax/
      3.2.2/
         LICENSE
         PACKAGE-METADATA.txt
         web/
            es5/
               tex-svg.js
```

The documentation server CMake already prefers these future ThirdParty roots. During the
transition it still accepts the existing bootstrap-managed web asset locations so the
current checkout remains buildable.

## Licensing

### cmark-gfm

The cmark-gfm `COPYING` file must be preserved verbatim. The main parser code uses a
BSD-style two-clause license, but the upstream file also carries notices for derived
Houdini, GitHub and utf8proc code under permissive MIT-style terms.

The upstream CommonMark specification included in the source/test tree is CC-BY-SA-4.0.
That does not turn the runtime DLL into CC-BY-SA software. If the specification or test
data are redistributed, however, their separate terms apply.

For DeckKernel the safest distribution rule is therefore simple: ship the complete
upstream `COPYING` file beside the cmark-gfm notice material rather than reducing it to
a single SPDX label.

### highlight.js

highlight.js 11.12.0 is BSD-3-Clause. The prebuilt CDN asset package has no package
dependencies of its own. Ship its complete upstream `LICENSE` with the JavaScript and
CSS assets.

### MathJax

MathJax 3.2.2 is Apache-2.0. The exact upstream `LICENSE` is stored in the repository and
must accompany the redistributed runtime. The selected `es5/tex-svg.js` build is the
same single-file runtime already used by the BuildEngine server.

### Mermaid

The Mermaid project itself is MIT licensed, but `mermaid.min.js` is a bundled browser
artifact. Mermaid 11.17.2 has a non-trivial production dependency closure. Therefore the
top-level MIT license alone is not a sufficient compliance record for the bundled file.

Before the Mermaid archive is added to the public ThirdParty release, generate a
`THIRD_PARTY_NOTICES.txt` from the exact Mermaid 11.17.2 production dependency closure
and place it under:

```text
licenses\mermaid\11.17.2\THIRD_PARTY_NOTICES.txt
```

The package preparation script deliberately warns while that file is absent. This keeps
the technical packaging work usable without silently declaring an incomplete license
review finished.

## Client-side printing

Printing belongs to the browser client, not to the HTTP server.

The server only provides the rendered document, print CSS and local browser assets.
`src/docu_server/assets/docu_client.js` owns the Print action. Before opening the browser
print dialog it waits for:

- browser fonts,
- MathJax startup,
- Mermaid source blocks to become rendered SVG diagrams,
- two final animation frames for layout.

It then calls the browser print API. This keeps the printed representation identical to
the visualized document and avoids a second server-side PDF rendering stack.
