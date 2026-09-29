# DeckKernel documentation server

[TOC|Documentation server]

## Motivation

DeckKernel is a learning project. The documentation server is therefore itself a small
project to build and understand: it turns the repository-owned Markdown documentation
into a local website and gives beginners a visible result before they start with the
database and Scryfall examples.


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

The server serves browser-side assets from the single central `Docs/js` directory.
Debug and Release never receive separate copies.

At the current repository state, the third-party browser assets are still present in Git.
This is transitional. The intended model is to download and verify the pinned third-party
web assets during documentation preparation and keep only DeckKernel-owned browser code,
such as `docu_client.js`, under source control. The repository must not remove those
third-party files until the corresponding reproducible preparation step is committed and
available to a fresh checkout.

The native cmark-gfm parser remains part of the locked BuildEngine ThirdParty stack and
its runtime DLLs are installed beside the server executable.

## Build and install

Run from the repository root in a C++Builder Developer Command Prompt.

### Debug

Configure:

```cmd
cmake -S src\docu_server -B src\docu_server\build\Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

Build:

```cmd
cmake --build src\docu_server\build\Debug
```

Install:

```cmd
cmake --install src\docu_server\build\Debug
```

The default installation is:

    apps\Debug\

### Release

Configure:

```cmd
cmake -S src\docu_server -B src\docu_server\build\Release -G Ninja -DCMAKE_BUILD_TYPE=Release
```

Build:

```cmd
cmake --build src\docu_server\build\Release
```

Install:

```cmd
cmake --install src\docu_server\build\Release
```

The default installation is:

    apps\Release\

The whole `apps/` directory is intentionally excluded from Git. The browser-side
assets already come with the repository; the bootstrap is required only for native
ThirdParty packages and build tools.

## Start

From the repository root or directly from the app directory:

```cmd
apps\Debug\deckkernel_docu_server.exe
```

Because the executable resides in `apps/<Configuration>`, the default repository root is
derived from the executable location. `--root` can override it.

The default server configuration is read from:

```text
Docs\Documentation.xml
```

Current configuration:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<documentation>
   <server address="127.0.0.1" port="8770"/>
</documentation>
```

The default endpoint is therefore:

```text
http://127.0.0.1:8770/docs/
```

BuildEngine Server uses port `8765` by default. DeckKernel deliberately uses `8770` so
both documentation servers can run at the same time.

Command-line values remain invocation overrides:

```text
--config <file>
--root <repository-root>
--address <concrete IP address>
--port <1..65535>
```

The XML structure is intentionally minimal today. Future versions can add presentation
configuration such as selectable stylesheets or renderer-specific settings for Markdown,
syntax highlighting, MathJax, Mermaid and other browser components without changing the
command-line contract.

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

The complete server security boundary is documented in:

[Security boundary](../security/SECURITY.md)


The server is read-only and supports GET only. It binds to `127.0.0.1` by default.
Document paths reject parent traversal, backslashes and drive prefixes before filesystem
access. Binding to an unspecified or multicast address is rejected.

## Table of contents

DeckKernel supports the same Markdown table-of-contents directive as the BuildEngine
server:

```text
[TOC|Caption]
```

The directive is handled before cmark-gfm rendering. Headings below the directive are
collected, receive stable document-local anchors and are rendered as a nested table of
contents. Headings on the main level also receive a **Back to Caption** link.

The syntax is intentionally compatible with BuildEngine. The current implementation
recognizes the directive in uppercase as `[TOC|...]`.

## Copyable code blocks

The browser client adds a **Copy** button to fenced code blocks. Mermaid source is the
only exception because it is transformed into a rendered diagram.

For beginner-oriented build instructions, language `cmd` has a specific convention:
it contains exactly one command that can be pasted directly into the C++Builder
Developer Command Prompt.

For example:

```cmd
cmake --build src\docu_server\build\Debug
```

CMake commands in the build guides must use this one-command form. Configure, build,
install and start are therefore shown as separate blocks rather than as a multi-line
script.

Multi-line fenced blocks are also copyable. They are copied as one complete block, which
is useful for source files, configuration fragments, SQL, XML and longer scripts:

```xml
<example>
   <value>DeckKernel</value>
</example>
```

The copy behaviour is implemented in `Docs/js/docu_client.js`; no non-standard Markdown
syntax is required.

---
