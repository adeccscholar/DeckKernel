# First connection test

[TOC|First connection test]

## Motivation

This is the first end-to-end DeckKernel learning project. It connects a real external
data source, parsing, typed C++ structures, the adecc database abstraction and PostgreSQL.
The point is not only to get a successful program run, but to make the path from network
data to persistent relational data visible and understandable.


This directory contains the first end-to-end DeckKernel infrastructure test.
It validates the complete path from Scryfall bulk data through the adecc database
abstraction into PostgreSQL and back into the text-grid abstraction.

The PostgreSQL server must be prepared first. See:

[PostgreSQL setup](../preparation/POSTGRESQL_SETUP.md)

The setup SQL must be executed while connected to database `DeckKernel`. PostgreSQL
schemas are database-local; a `deckkernel_test` schema created in the default
`postgres` database is not visible to this test. The setup guide and setup script both
check this explicitly.

## From git pull to program start

Run all commands from the DeckKernel repository root in a C++Builder Developer
Command Prompt.

### 1. Update the repository

```cmd
git pull
```

### 2. Prepare the project with the bootstrap

Run the project bootstrap before configuring this example:

```cmd
cmake -P bootstrap\Bootstrap.cmake
```

The bootstrap itself is documented separately. This guide does not duplicate its
package, tool or documentation-asset preparation rules:

[DeckKernel bootstrap](../preparation/BOOTSTRAP.md)

### 3. Configure Debug

```cmd
cmake -S test\firstconnect -B test\firstconnect\build\Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

### 4. Build Debug

```cmd
cmake --build test\firstconnect\build\Debug
```

### 5. Install Debug

Do not start the executable directly from the build directory. The install step
creates one flat runnable directory containing the executable and its runtime DLLs.
The default install prefix is the repository-local apps/<Configuration> directory.
The complete apps directory is intentionally excluded from Git.

For Ninja this project is single-configuration. The CMake build type is now bound
directly to the BuildEngine ThirdParty configuration, so a Debug build can only
resolve and install DLLs from ThirdParty/.../bin/win64/Debug, while Release uses
the corresponding Release directories. If an older build tree was configured with
the wrong build type, delete that build directory and configure it again.

```cmd
cmake --install test\firstconnect\build\Debug
```

### 6. Start Debug

```cmd
apps\Debug\deckkernel_scryfall_postgres_test.exe
```

## Release build

Configure Release:

```cmd
cmake -S test\firstconnect -B test\firstconnect\build\Release -G Ninja -DCMAKE_BUILD_TYPE=Release
```

Build Release:

```cmd
cmake --build test\firstconnect\build\Release
```

Install Release:

```cmd
cmake --install test\firstconnect\build\Release
```

Start it with:

```cmd
apps\Release\deckkernel_scryfall_postgres_test.exe
```

## What the test does

The executable is structured into four explicit, independently timed processes:

1. **Load** resolves Scryfall `default_cards` metadata and downloads or reuses the same-day bulk file.
2. **Parse** reads the bulk file and creates the typed Scryfall card and set data.
3. **Store** ensures the test tables exist, truncates them and writes the parsed data through adecc output sinks.
4. **Evaluate** reads the newest 20 cards through the typed database range and sends them through both text-grid paths.

The PostgreSQL connection uses integrated Windows SSPI authentication.

The query deliberately aliases the two selected `name` columns as
`card_name` and `set_name`. The PostgreSQL adapter resolves result fields by
column name; selecting both columns simply as `name` made both tuple elements
resolve to the first result column.

## Expected PostgreSQL defaults

```text
host            localhost
port            5432
database        DeckKernel
database role   deckkernel_user
integrated      true
sslmode         prefer
gssencmode      disable
krbsrvname      postgres
```

Environment overrides remain available:

```text
DECKKERNEL_PGHOST
DECKKERNEL_PGPORT
DECKKERNEL_PGDATABASE
DECKKERNEL_PGUSER
DECKKERNEL_PGPASSWORD
DECKKERNEL_PG_INTEGRATED
DECKKERNEL_PGSSLMODE
DECKKERNEL_PG_GSSENCMODE
DECKKERNEL_PG_GSSLIB
DECKKERNEL_PG_KRBSRVNAME
```

## Files

- `scryfall_postgres_test.cpp`: four timed processes: load, parse, store and evaluate.
- `scryfall_model.h`: persistent Scryfall data definitions, metadata, selectors and manipulators.
- `schema.sql`: equivalent test schema/table DDL for inspection or manual setup.
- `CMakeLists.txt`: standalone Debug/Release build and install entry point.
- `adecc\\postgre\\pqxx_database.h`: reusable PostgreSQL/libpqxx adapter.


## Why the example is written this way

This program is intentionally more explicit than a minimal smoke test. It is the first
functional example for other developers and therefore shows the technical boundaries
rather than hiding them.

The four processes make the data flow visible:

```text
Scryfall HTTPS
   |
   v
Load
   |
   | local gzip/JSONL file
   v
Parse
   |
   | typed set/card tuples
   v
Store
   |
   | PostgreSQL tables
   v
Evaluate
   |
   v
typed database range -> text grid
```

The JSON bulk format repeats set data in every card record. During parsing the example
normalizes that external representation: sets are deduplicated by `set_id`, while every
card printing keeps `set_id` as its foreign key. The database therefore receives data
that already has a clear relational shape.

## RAII and raw pointers

Owned resources use RAII wherever the third-party API allows it:

- libcurl easy handles are owned by `std::unique_ptr` with a custom deleter;
- libcurl header lists are owned by a dedicated RAII class;
- gzip streams are owned by `std::unique_ptr` with a zlib `gzclose` deleter;
- files are owned by `std::ofstream` / `std::ifstream`;
- PostgreSQL connections and transactions are owned by the adapter and transaction guards.

Raw pointers remain only at C API boundaries. libcurl callback signatures require
`char*` and `void*`, `curl_easy_setopt` consumes C handles, `std::getenv` returns a
borrowed C string, and `curl_version_info` returns a library-owned borrowed structure.
The source comments mark these cases and state whether ownership is transferred. No
owning raw pointer is intentionally exposed by the example.

## Third-party responsibilities

- **curl**: HTTPS requests, redirects, response callbacks and Scryfall headers.
- **OpenSSL**: TLS backend used by curl. Peer and hostname verification stay enabled.
- **nlohmann/json**: JSON metadata and one-card-per-line JSONL parsing.
- **zlib**: direct streaming read of Scryfall's single gzip-compressed JSONL payload.
- **libpq/libpqxx**: PostgreSQL transport behind the adecc adapter.
- **libarchive**: not used by this test. The payload is a gzip stream, not an archive with
  multiple members, so using zlib directly keeps the first example smaller and clearer.

The source documents the relevant third-party calls at the call site, including callback
parameters, borrowed pointers and lifetime assumptions.


## Lesson source structure

The example is split so each technical step can be discussed independently:

    scryfall_load.cpp       HTTPS, curl/OpenSSL, local cache
            |
            v
    scryfall_parse.cpp      zlib, JSONL, normalization
            |
            v
    scryfall_database.cpp   PostgreSQL, transaction, output sinks
            |
            v
    scryfall_postgres_test.cpp
                            composition, timing, evaluation/grid output

The data model remains in scryfall_model.h. The Scryfall field and domain reference is
documented centrally in the
[Scryfall data model](../model/SCRYFALL_DATA_MODEL.md).


## All Cards and image-based scan preparation (October 2026)

The bulk loader now selects Scryfall **`all_cards`**, not `default_cards`.
It therefore imports all represented languages/printings. The plain-JSON array
reader parses one card object at a time rather than materializing the complete
multi-gigabyte JSON file. The structured staging collection still holds card
records until the existing store phase completes; this is not yet a fully
stream-to-database import.

Printing data includes `collector_number`, `lang`, and optional
`illustration_id`. Faces store their own optional illustration ID and
`normal`, `art_crop`, and `png` image URLs; for faces without an image
object the root card image fields are used when available.

After the normal Store phase, `scryfall_image_assets` is populated with
deduplicated artwork URLs. Its `image_data bytea` stores original JPEG bytes,
with SHA-256 and source URL; `scryfall_image_features` is reserved for the
feature extractor, versioned with `algorithm` and `algorithm_version`.
The model image tables are **disposable**: FirstConnection rebuilds them
and repopulates image bytes from the permanent URL cache in
\`deckkernel_cache.scryfall_images\`. Only the latter survives model rebuilds.
The image step uses libcurl and libpq binary parameters, not text-encoded image SQL.

`DECKKERNEL_IMAGE_LIMIT` controls new image downloads per run. The default
is **100** for a bounded first test; set **0** to process every pending image.
Already downloaded images are skipped. This can be a substantial download and
PostgreSQL storage workload. Requests are made sequentially and are not
parallelized against Scryfall's image CDN. Repeated builds do not download
images unless the executable is run.

The separately documented `test/scantest` is the next step for actual
OpenCV descriptors and image-based matching. **No native BCC64X run has yet
verified the new import or scanner paths.**


### Permanent URL cache and resettable image model

Before the first run, execute `test/postgresql_sspi_setup.sql` as PostgreSQL
administrator, following [PostgreSQL setup](../preparation/POSTGRESQL_SETUP.md).
This creates and grants ownership of the permanent image cache. A normal
FirstConnection login intentionally does **not** need database-level
`CREATE` privileges, and never attempts `CREATE SCHEMA`.


Two PostgreSQL schemas now have different lifecycles. The durable **`deckkernel_cache.scryfall_images`** table is keyed by the exact source URL and stores the untouched binary bytes, SHA-256, media type and cache time. It must **never be dropped** by FirstConnection. The old `scryfall_image_assets` records are migrated into it automatically before the legacy model table is reset. If the entire database is dropped, a separate backup of `deckkernel_cache` is required to preserve these bytes.

The disposable **`deckkernel_test`** model tables are rebuilt: `scryfall_image_assets` contains per-URL model copies, `scryfall_card_image_variants` links each card/face/variant to an asset, and `scryfall_image_features` holds regenerable, algorithm-versioned OpenCV features. Variant keys include `small`, `normal`, `large`, `png`, `art_crop` and `border_crop`. A card may have multiple variants, and many printings may share an image URL.

**Cache-first sequence:** FirstConnection imports all cards and their face variant URL references; it bulk-restores all locally available image bytes from `deckkernel_cache` into the model. Only model URLs still missing bytes become network candidates. New downloads are first written to the durable PostgreSQL URL cache and then to the model. The existing separate content-addressed disk cache serves as another offline recovery layer. `DECKKERNEL_IMAGE_LIMIT` limits only still-missing downloads, not DB-to-DB restoration.

Avoid **`DROP DATABASE DeckKernel`** unless the durable schema has been backed up. The routine model reset is scoped to `deckkernel_test` tables and leaves `deckkernel_cache` untouched.

### Persistent disk image cache

`src/scryfall/image_cache.{h,cpp}` stores original images in SHA-256 content-addressed blobs and maps source URLs to those blobs. Every hit verifies blob digest and image signature. The cache can repopulate PostgreSQL after rebuilding test tables or moving to a new database, and it is independent of OCR/ORB algorithm versions.

Set `DECKKERNEL_IMAGE_CACHE_ROOT` to a stable absolute folder shared by FirstConnection and future Scantest runs. The default is `.cache/scryfall/images` relative to the process working directory. The disk cache and PostgreSQL `bytea` are complementary; never delete the disk cache during routine builds. Requests remain serial with a 250 ms minimum gap for image fetches; local hits trigger no HTTP request. The loader prints cache hit and network-fetch counts. This is conservative etiquette, not a stated Scryfall CDN-specific hard limit.

Scryfall's API guidance asks clients not to exceed ten API requests per second. The public image CDN is outside that endpoint-specific request quota, but this does not authorize aggressive crawling. Stop on 429, honor Retry-After, and avoid repeated failed requests. Binary cache writes use temporary files and SHA-256 validation; Windows BCC64X runtime is not yet verified.


### PostgreSQL SQL adapter compatibility

FirstConnection performs schema preflight checks using ordinary scalar `SELECT CASE ... END` queries through `ExecuteOne<int>`. It deliberately does not submit PostgreSQL dollar-quoted `DO $tag$ ... $tag$` blocks to the framework's named-parameter translator, which rejects that syntax before execution. Existing model image bytes are migrated into the persistent cache with an ordinary `INSERT ... SELECT ... ON CONFLICT DO NOTHING` only when the previous model table exists. PostgreSQL administrative setup scripts may still use native `DO` blocks because they are executed directly through `psql`/pgAdmin, outside the application translator.


### Image persistence consistency (cache-first)

FirstConnection first restores **all** URLs already present in `deckkernel_cache.scryfall_images` into the resettable `deckkernel_test.scryfall_image_assets` table with one set-based `UPDATE ... FROM`; this step performs **no HTTP request** and is not limited by `DECKKERNEL_IMAGE_LIMIT`.

For a remaining model URL, the importer checks the persistent on-disk image cache and only then makes a rate-limited Scryfall image request if the local disk copy is missing. A newly acquired image is written in **one PostgreSQL transaction** by (1) `INSERT ... ON CONFLICT DO NOTHING` into the durable URL-keyed cache and (2) `INSERT ... SELECT ... ON CONFLICT DO UPDATE` into the disposable model. The model upsert copies the binary payload directly from the database cache. If either write fails, both database changes are rolled back. The model's existing `image_kind`, `illustration_id`, and card/face/variant links remain unchanged. Re-running the test with cached URLs does not redownload their images.

The limit `DECKKERNEL_IMAGE_LIMIT` counts only missing model image URLs considered for this pass; an empty durable cache and disk cache will still require an initial download of those images. This mechanism has not yet been verified with a native Windows BCC64X runtime and PostgreSQL test execution.


## Fast recovery after an image download failure

The 546,767-card initial import takes significant time. **Do not rerun the default full import to diagnose image HTTPS problems.** The existing executable now offers independent command modes, using the already stored `deckkernel_test` model, without executing Load, Parse, Store, or resetting any tables:

```cmd
apps\Release\deckkernel_scryfall_postgres_test.exe --image-probe
apps\Release\deckkernel_scryfall_postgres_test.exe --images-only
apps\Release\deckkernel_scryfall_postgres_test.exe --images-offline
```

- `--image-probe`: one unresolved image URL (after restoring all PostgreSQL cache hits), with at most one HTTPS attempt. Test this first.
- `--images-only` (**default, including no-argument launch**): restore all permanent PostgreSQL cache hits and process every remaining URL in bounded database pages, **without an external script**. Its default is unlimited; explicitly set `DECKKERNEL_IMAGE_LIMIT` for a bounded run.
- `--images-offline`: restore every available image from the PostgreSQL cache and local disk cache only; **never contact Scryfall**. Explicit `DECKKERNEL_IMAGE_LIMIT` can bound the disk-only pass.
- `--all` (explicit only): the original full Load/Parse/Store/import workflow; **this resets the model**. Its initial network image limit remains 100 unless explicitly configured.

The separate image downloader now matches the bulk downloader's libcurl configuration by requesting Windows native CA certificates through `CURLSSLOPT_NATIVE_CA` when available, with peer/host verification enabled. When required, set `DECKKERNEL_CURL_CA_BUNDLE` to an existing trusted PEM certificate-bundle file. **Never disable `CURLOPT_SSL_VERIFYPEER` or `CURLOPT_SSL_VERIFYHOST`.** It prints the active libcurl TLS backend and uses the CURL error buffer to reveal certificate issues. Certificate/TLS failures and HTTP 429/503 now immediately abort the image stage, preventing repeated doomed downloads. Compilation and runtime behavior still require verification on Windows/BCC64X.


### Unattended complete image import (no batch file)

Run the installed executable **directly** in the DeckKernel repository root:

```cmd
apps\Release\deckkernel_scryfall_postgres_test.exe --images-only
```

The program performs **Connect → PostgreSQL cache restoration → disk cache lookup → download on miss → transactional insert/upsert into both PostgreSQL schemas**, with **no Load, Parse, Store, DDL, or model-table reset**. With no `DECKKERNEL_IMAGE_LIMIT` set, this mode keeps processing until all known image URLs have been attempted. The model catalog is read in keyset pages of 512 records to avoid buffering all image URLs. Every 100 processed URLs it reports successes, misses, network requests, elapsed time, and estimated remaining work. Previously committed images survive interruption; restarting the same executable resumes from the still-null model image rows.

Temporary network and HTTP 429/500/502/503/504 errors are retried with exponential backoff and support for HTTP `Retry-After`. A URL that exhausts its retry allowance is recorded as failed while the importer continues; the process exits nonzero at the end if any URLs failed, so a later run can retry them. TLS certificate and local database/storage errors stop the process immediately; TLS verification stays enabled. During a long outage the retries may delay progress substantially. Use `--image-probe` for a one-image diagnostic and `--images-offline` for a strictly network-free recovery.

**Safety:** The no-argument default has intentionally changed from `--all` to `--images-only` so an ordinary start cannot erase the already imported model. A complete model rebuild is now opt-in with `--all`.


### Image-import performance profiling (October 2026)

The importer previously enforced a fixed 250 ms minimum interval between image-request starts, limiting it to at most four HTTP requests per second before network, filesystem and database overhead. At 3.2 million missing image URLs that alone implies roughly 222 hours of request-start pacing. A 300-hour estimate is therefore not evidence that PostgreSQL is the primary bottleneck.

The interval is now adjustable through `DECKKERNEL_IMAGE_INTERVAL_MS` (integer, 100–60000; default **250**). The default remains conservative. Changes affect **only newly started processes** and require no card import. Raise request rate cautiously and honor 429/503 and Retry-After. A useful first diagnostic is the existing 250 ms rate rather than a blind throughput increase.

Every 100 processed images, `[IMAGE-PERF]` now reports accumulated HTTPS transfer time and effective Mbit/s, PostgreSQL write/commit time and average ms/image, disk-cache lookup/write time, throttle waiting, retry waiting and received MiB. These timers are sequential stage measurements, not independent wall-clock percentages; HTTPS elapsed time includes transfers, TLS and latency. The aggregate download rate uses transferred image payload bytes, not the physical network interface counter. The ETA remains an approximation and will vary with image sizes, HTTP latency, cache hits and retries.

The two previous PostgreSQL statements plus explicit BEGIN/COMMIT are replaced by **one atomic SQL statement** with a writable CTE. It inserts the original bytes into `deckkernel_cache.scryfall_images` if missing and upserts the model image from the same authoritative cache data. Both effects commit together or neither does. The statement uses `INSERT ... RETURNING` to access newly inserted rows without a second table read in the same statement snapshot. This reduces client/server round-trips and synchronous commits per new image; it is **not** a guarantee of a proportional overall throughput increase, because the HTTP pacing may dominate.

The on-disk image cache, database keys, image bytes, existing cache, and default `--images-only` resumability are unchanged. **Do not restart an already-running overnight importer just for these instrumentation changes**: finish or safely interrupt it first; the next run will reuse its committed image data. These improvements still require native BCC64X compilation and validation on the intended database.


### Long-lived curl session and bounded disk cache (October 26, 2026)

FirstConnection image imports now create **one libcurl easy handle per image-import process**, not one per image. All sequential Scryfall image requests reuse its connection cache where the CDN permits HTTP keep-alive and TLS session reuse; TLS peer and host verification remain enabled. The existing pacing, retry, and Retry-After behavior is unchanged. The `[IMAGE-PERF]` line includes `new_connections` from libcurl's `CURLINFO_NUM_CONNECTS`, allowing connection reuse to be verified empirically. Reuse is negotiated with the CDN and is not guaranteed for every request.

With both PostgreSQL image schemas receiving the bytes atomically, **newly downloaded images stay in memory until the database commit succeeds**, so a successful import no longer produces a persistent disk-cache blob. If the database write fails, the importer attempts to preserve the downloaded image in the local cache as recovery evidence. An already existing local disk-cache hit is consumed and, only after the successful database commit, its URL reference is removed. Its SHA-256 blob is deleted only when no other indexed URL references remain. Disk-cache deletion failures are warnings and must never turn an already committed image into a database failure.

The local cache folder is expected to have a **single owning writer/importer process** during an image run. Deletion of shared content-addressed blobs must not be attempted from multiple concurrent importers. Cache files belonging to images already committed in an earlier run, but never visited by this version, are not removed proactively. For bulk cleanup of such historic files use a separate audited reconciliation against the durable PostgreSQL cache; do not blindly delete the cache tree.

The permanent `deckkernel_cache` schema remains the authoritative image store for rebuilding the disposable `deckkernel_test` model. Restarting `--images-only` still skips URLs populated in the model. No migration of previously downloaded PostgreSQL bytes is required. **The running process keeps its existing behavior until a newly compiled and installed executable is started.**
