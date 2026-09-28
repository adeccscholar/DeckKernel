# DeckKernel documentation server

The documentation server renders the Markdown files below `Docs/` directly from the repository.

It reuses the architecture already proven by the BuildEngine server:

- Boost.Asio / Boost.Beast for the HTTP listener;
- cmark-gfm for Markdown and GitHub-Flavored Markdown extensions;
- highlight.js for source-code highlighting;
- Mermaid for diagrams;
- MathJax for TeX/LaTeX formulas.

The DeckKernel addition is browser printing. Every rendered document has a **Print**
button that invokes the browser print dialog. Dedicated print CSS removes navigation and
presentation chrome, uses a white page and tries to keep source blocks, tables,
blockquotes and Mermaid diagrams together.

## Source structure

    src/docu_server/
       CMakeLists.txt
       main.cpp
       docu_server.h
       docu_server.cpp
       markdown_renderer.h
       markdown_renderer.cpp

## Reused BuildEngine approach

The Markdown renderer follows `BuildEngine-Common/src/MarkdownRenderer.cpp`: cmark-gfm
is loaded from the application directory and the GFM extensions `table`, `tasklist`,
`strikethrough`, `autolink` and `tagfilter` are enabled.

The browser-side versions follow the current BuildEngine server configuration:

| Component | Version | Purpose |
| --- | ---: | --- |
| Mermaid | 11.17.2 | diagrams in fenced `mermaid` blocks |
| highlight.js | 11.12.0 | syntax highlighting |
| MathJax | 3.2.2 | TeX/LaTeX formulas |

The DeckKernel bootstrap now provisions the same pinned browser assets below
`Cache/tools/web` using the versions, download locations and SHA-256 values already used
by BuildEngine. The server install step copies them into `apps/<Configuration>/web` and
serves them locally. After bootstrap, rendering therefore does not depend on a CDN.

The cmark-gfm parser is taken from the locked BuildEngine ThirdParty stack and its runtime
DLLs are installed beside the server executable.

## Build and install

Run from the repository root in a C++Builder Developer Command Prompt.

Debug:

    cmake -S src\docu_server -B src\docu_server\build\Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
    cmake --build src\docu_server\build\Debug
    cmake --install src\docu_server\build\Debug

The default installation is:

    apps\Debug\

Release:

    cmake -S src\docu_server -B src\docu_server\build\Release -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build src\docu_server\build\Release
    cmake --install src\docu_server\build\Release

The default installation is:

    apps\Release\

The whole `apps/` directory is intentionally excluded from Git. Re-run the repository
bootstrap after pulling the documentation-server changes so the pinned Mermaid,
highlight.js and MathJax assets are available.

## Start

From the repository root or directly from the app directory:

    apps\Debug\deckkernel_docu_server.exe

Because the executable resides in `apps/<Configuration>`, the default repository root is
derived from the executable location. `--root` can override it.

Default endpoint:

    http://127.0.0.1:8770/docs/

Options:

    --root <repository-root>
    --address <concrete IP address>
    --port <1..65535>

## Printing

Printing deliberately remains browser-based. Mermaid and MathJax are rendered to SVG in
the browser before printing, so the printout uses the same visual result as the screen
view. This avoids a second, divergent PDF rendering pipeline.

The print style:

- hides server navigation and the on-screen hero section;
- removes cards, shadows and colored page backgrounds;
- requests 15 mm page margins;
- avoids page breaks inside tables, code blocks, blockquotes and Mermaid diagrams where possible;
- prints external link targets as text.

## Security scope

The server is read-only and supports GET only. It binds to `127.0.0.1` by default.
Document paths reject parent traversal, backslashes and drive prefixes before filesystem
access. Binding to an unspecified or multicast address is rejected.