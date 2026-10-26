# DeckKernel ImageViewer

Standalone native Windows C++23 viewer and image-verification program.
It reads **already downloaded** Scryfall pictures from PostgreSQL; it makes no
Scryfall requests and never updates either database schema. It can run while
FirstConnection keeps downloading.

## Build and install

Use the same BCC64X/Ninja environment and BuildEngine-managed ThirdParty packages:

    cmake -S test/imageviewer -B test/imageviewer/build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build test/imageviewer/build/Release
    cmake --install test/imageviewer/build/Release

Launch from the repository root:

    apps\Release\deckkernel_image_viewer.exe

## Viewer

The left panel lists 60 downloaded images, ordered by the asset ID.
Select a row to see the image, name/set/face when linked, variant, original URL,
image dimensions and byte count, SHA-256 validation and media signature.
Previous/Next change pages, Refresh or F5 requests the latest committed data,
and Go jumps to an exact asset ID. Up/Down work in the list.

The viewer loads only one image blob into memory at a time and uses Windows
Imaging Component to decode JPEG, PNG and WebP. Bad hashes or undecodable
images are reported; originals remain unchanged.

## Read-only audit

    apps\Release\deckkernel_image_viewer.exe --check=1000

Options: --check checks up to 100 available images; --check=0 processes all
currently available images. It validates SHA-256, image signature and WIC
decoding, logging a summary. Never writes to PostgreSQL.

## Database

Connection environment variables are the same as FirstConnection:
DECKKERNEL_PGHOST, DECKKERNEL_PGPORT, DECKKERNEL_PGDATABASE,
DECKKERNEL_PGUSER, DECKKERNEL_PGPASSWORD, DECKKERNEL_PGSSLMODE,
and DECKKERNEL_PG_GSSENCMODE. Defaults: localhost:5432,
DeckKernel, deckkernel_user, prefer, disable.

This program sets default_transaction_read_only=on and runs only SELECT queries.
It uses the already populated deckkernel_test.scryfall_image_assets and optional
card/set/variant links; it never resets the model or connects to Scryfall.
