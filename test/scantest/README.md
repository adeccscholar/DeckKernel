# DeckKernel artwork scan test

This test is the first offline recognition-index preparation for the continuous camera scanner. It uses **OpenCV 5.0 DLL modules** (`core`, `imgproc`, `imgcodecs`, `features`) from BuildEngine and the populated PostgreSQL `deckkernel_test.scryfall_image_assets` table from FirstConnection.

It computes two signals from real saved JPEG artwork bytes: a deterministic 64-bit difference hash (`dhash64-orb`, version 1) and OpenCV ORB descriptors plus a keypoint count. It persists these in `scryfall_image_features`, skipping already indexed records; `DECKKERNEL_SCAN_INDEX_LIMIT=100` by default, `0` means all outstanding assets.

This is **an index builder**, not yet a live camera consumer. The next test will evaluate a fresh camera crop against a bounded candidate set and use `oracle_id`, `illustration_id`, set and collector-number relations for edition selection. A future VCL camera UI and oneTBB pipeline will be introduced only after benchmarking.

The existing DeckKernel bootstrap manifest predates OpenCV and does not currently guarantee that its CMake package is provisioned. Configure is intentionally strict (`find_package(OpenCV 5.0 CONFIG REQUIRED ...)`): we must integrate the BuildEngine OpenCV package into DeckKernel's pinned bootstrap first. No MSVC or static fallback.

**Windows BCC64X build/runtime has not yet been executed.**

## Concurrent offline feature indexing

The importer and indexer are independent programs. Run the Scryfall
image importer normally and then, in another terminal, run:

    apps\Release\deckkernel_card_scantest.exe --follow --batch-size=32 --poll-seconds=5

Only committed PostgreSQL image_data rows are used. No network downloads
or re-imports are triggered. The existing version-1 dHash64 and ORB
descriptors are incrementally upserted into scryfall_image_features;
already current version-1 entries are skipped. Use Ctrl+C to stop the
indexer independently of the image downloader.

Without --follow the program exits after existing pending images have been
processed. --limit=N caps successful images (0 means unlimited).
DECKKERNEL_SCAN_INDEX_LIMIT defaults to 100 in one-shot mode, 0 in
--follow mode. --batch-size=N limits the bytes queried per batch; each image
is decoded and indexed sequentially within the batch. Concurrent means
running alongside the importer, not multiple CPU workers or multiple
indexer instances.

    cmake -S test/scantest -B test/scantest/build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build test/scantest/build/Release
    cmake --install test/scantest/build/Release

This feature does not modify the schema or the image importer's state.
