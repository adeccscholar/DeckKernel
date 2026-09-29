# Documentation ThirdParty dependencies

[TOC|Documentation ThirdParty]

The documentation server uses one native ThirdParty library and three browser-side
libraries.

| Component | Version | Role | Distribution model |
| --- | ---: | --- | --- |
| cmark-gfm | 0.29.0.gfm.13 | native Markdown parser | locked ThirdParty release package |
| highlight.js | 11.12.0 | syntax highlighting | pinned browser asset |
| Mermaid | 11.17.2 | diagrams | pinned browser asset |
| MathJax | 3.2.2 | TeX/LaTeX rendering | pinned browser asset |

The browser assets are currently present inside the DeckKernel repository, but this is a
transitional state. The intended model is to download and verify the exact pinned browser
files during documentation preparation. Only DeckKernel-owned browser code should remain
permanently versioned once that reproducible preparation step is available in the
repository.

## Repository layout

```text
Docs/js/
   docu_client.js
   highlight.min.js
   github.min.css
   mermaid.min.js
   mathjax-tex-svg.js
```

The corresponding upstream licence texts are retained below:

```text
licenses/
   cmark-gfm/0.29.0.gfm.13/COPYING
   highlight.js/11.12.0/LICENSE
   mermaid/11.17.2/LICENSE
   mathjax/3.2.2/LICENSE
```

## cmark-gfm

cmark-gfm remains in the normal BuildEngine ThirdParty stack because it is compiled native
code. The complete upstream `COPYING` file is retained. It contains the main BSD-style
terms and additional notices for derived code.

## highlight.js

highlight.js 11.12.0 is distributed under BSD-3-Clause. The exact JavaScript runtime and
GitHub stylesheet used by DeckKernel are stored in the repository together with the
upstream licence text.

## Mermaid

Mermaid 11.17.2 is MIT licensed at project level. The repository stores the exact browser
bundle used by the documentation server.

Because the browser bundle contains Mermaid's production dependency closure, binary/web
redistribution should preserve the relevant transitive notices when required. The
top-level Mermaid licence text alone must not be treated as proof that every bundled
dependency has identical terms.

## MathJax

MathJax 3.2.2 is distributed under Apache-2.0. DeckKernel stores the exact `tex-svg.js`
runtime used by the browser together with the upstream licence text.

## Browser asset preparation model

The browser files are not compiler- or platform-dependent libraries and do not need
Debug/Release variants. They belong to one central runtime location below `Docs/js`.

The target preparation flow is:

```text
git clone / git pull
      |
      +--> source code
      +--> Markdown documentation
      +--> DeckKernel-owned browser code

documentation preparation
      |
      +--> pinned highlight.js asset
      +--> pinned Mermaid asset
      +--> pinned MathJax asset
      +--> pinned stylesheet assets

bootstrap
      |
      +--> compiled native ThirdParty packages
      +--> build tools
```

The downloaded files must be version-pinned and verified. Until that preparation step is
committed and usable from a fresh checkout, the currently vendored copies remain required
for a working repository.

## Client-side printing

Printing belongs to the browser client. The server supplies rendered HTML, local browser
assets and print CSS. `Docs/js/docu_client.js` waits for fonts, MathJax
and Mermaid rendering before invoking the browser print dialog.

This keeps screen and print rendering on the same client-side pipeline and avoids a
separate server-side PDF renderer.
