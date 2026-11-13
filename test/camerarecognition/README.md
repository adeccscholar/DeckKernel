# CameraRecognitionTest: stage 0

This is a second, independent DeckKernel test program. It does NOT recognize a
specific printing yet: it detects a card-shaped quadrilateral, rectifies it,
waits for five stable frames, and emits one NEW-STABLE-CARD event. Removal for
45 consecutively absent frames rearms detection, including when the same card
is reinserted, and emits one CARD-NO-LONGER-DETECTED log event.

The test never calls Scryfall, never reads or writes the card database and
cannot interrupt the multi-day image download. Snapshot writing is opt-in.

## Build

The same bootstrapped BCC64X/CMake/Ninja toolchain as scantest is required.
OpenCV 5 moves `contourArea`, `approxPolyDP`, `arcLength`,
`isContourConvex`, `minAreaRect`, and `getPerspectiveTransform` from
`imgproc` into the **geometry** component. The test includes
`opencv2/geometry.hpp` and explicitly requires `geometry` from the
installed OpenCV 5 CMake package. No new or static OpenCV build is used.

    cmake -S test/camerarecognition -B test/camerarecognition/build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build test/camerarecognition/build/Release
    ctest --test-dir test/camerarecognition/build/Release --output-on-failure
    cmake --install test/camerarecognition/build/Release

Runtime dependencies are now staged immediately after linking, directly beside
the build-tree executable for CTest. The shared pinned runtime install helper
also copies the same dependency closure into apps/Release. This replaces the
optional directory-copy rule, which could silently skip missing DLLs.
The staging resolution is limited to the current BuildEngine packages and
the active BCC64X runtime; missing imports cause an explicit failure.

After switching to this CMake rule, perform a clean rebuild once to ensure
the POST_BUILD step executes even when the EXE was already up-to-date.

Usage:

    apps\Release\deckkernel_camera_recognition_test.exe --self-test
    apps\Release\deckkernel_camera_recognition_test.exe --sequence=testsamples\camera --save-events
    apps\Release\deckkernel_camera_recognition_test.exe --camera=0 --save-events

Live camera mode requires opencv_videoio to be exported by the installed
BuildEngine OpenCV package; existing scantest does not prove that component
is available. Without it the project still builds its replay and self-test.

## Camera signal diagnostics

A successful VideoCapture::isOpened() does **not** prove that real pixels
arrive. A USB/internal camera may report 1920x1080 at 1 FPS while returning
black placeholder frames. The camera LED may remain off.

Run these diagnostics before debugging card contours:

    apps\Release\deckkernel_camera_recognition_test.exe --probe-camera=0
    apps\Release\deckkernel_camera_recognition_test.exe --probe-camera=1

The probe tests DirectShow MJPG, DirectShow YUY2, Media Foundation auto
and Media Foundation MJPG, each at 640x480/30 FPS. It prints the actually
negotiated resolution/FOURCC, number of received frames, black/nonblack
frames, changed frames and maximum pixel intensity. A mode counts as
usable only when it returns nonblack pixels; no Scryfall or database
operations occur.

When the probe identifies a usable mode:

    apps\Release\deckkernel_camera_recognition_test.exe --camera=1 --backend=dshow --width=640 --height=480 --fps=30 --fourcc=mjpg

Other options: --backend=msmf, --fourcc=yuy2 or --fourcc=auto.

If every probe returns black frames, independently verify the same
physical device in Windows Camera, verify Windows camera permissions,
check the physical shutter, and close applications that hold the camera.
A camera that is opened by the driver but produces only black pixels
cannot be fixed by changing the card recognition algorithm. The program
now reports VIDEO SIGNAL ABSENT rather than silently processing black
placeholder frames.

Camera snapshots, if requested, go to .cache/camera-recognition or the folder
specified by DECKKERNEL_CAMERA_EVIDENCE_ROOT. Do not commit private photographs.

## Next recognition stages

1. Card presence and perspective normalization: now prototyped. Currently
   tuned to a single standard portrait card on a contrasting background.
2. Layout/orientation model: normal, older/newer frames, transform/modal DFC,
   split, adventure, flip, battle, panorama/oversized and borderless editions.
3. Extract art crop and/or OCR name, set symbol, collector number, language.
4. Candidate retrieval from existing database and scryfall_image_features:
   indexed dHash/ORB for art, followed by full-card image comparison.
5. Report separate outcomes: UNKNOWN, ORACLE_IDENTITY, PRINTING_ID,
   AMBIGUOUS and NOT_ENOUGH_EVIDENCE. Never label exact foil/etched finishes
   as certain if a single RGB frame cannot distinguish them.

## Existing data available now

scryfall_cards: Scryfall printing ID, Oracle ID, name, set, released date,
collector number, rarity, language and illustration ID. Card faces contain
names/layout/face ID and source URIs. Card image variants link to image assets.
scantest already computes versioned dHash and ORB descriptors for images it
has processed. Do not rescan/re-download images unnecessarily.

## Additional Scryfall metadata needed for exact-print classification

- frame, frame_effects, border_color, full_art
- finishes, promo, promo_types, image_status, highres_image
- printed_name and alternative face name per language
- printed_type_line, flavor_name, textless, per-face orientation/layout
- explicit side/front-back mapping for multi-faced cards

These fields are known Scryfall metadata but are not all currently stored by
FirstConnection. Add them through a separate staged schema change later, NOT
by rerunning --all during the existing image import.

## Ground-truth corpus

Collect at least 30-50 known physical cards, with Scryfall printing ID, face,
set code, collector number, language and layout. Include older frames, new
frames, multiple reprints with shared art, borderless, foil, tokens, split,
double-faced and rotated cards. For each, capture several angles/distances,
3 lighting situations, sleeves/reflections, hand occlusion, and no-card frames.

The synthetic CTest covers event rearming, not physical-world accuracy.
A native Windows/BCC64X runtime test with a real camera is still required.

## Independent native DirectShow hardware activation

The OpenCV 5.0 BuildEngine catalog enables WITH_DSHOW, disables WITH_MSMF
and videoio plug-ins. The existing OpenCV consumer smoke only checks whether
the DirectShow backend is registered: it never activates a physical webcam.
An OpenCV videoio backend that reports isOpened() can nevertheless deliver
1920x1080 black frames at ~1 FPS while the physical camera LED remains OFF.

Use a separate executable with *native Windows DirectShow COM* to determine
whether the Windows driver can activate the actual hardware independently of
OpenCV. No OpenCV DLL is linked into this executable.

    apps\Release\deckkernel_native_camera_probe.exe --list
    apps\Release\deckkernel_native_camera_probe.exe --device=0 --seconds=8

The enumeration reports the real DirectShow FriendlyName and DevicePath.
The device number is scoped to this enumeration, not a guaranteed match for
the opaque OpenCV CAP_DSHOW index. The probe builds a native capture graph
with a video renderer, calls IMediaControl::Run, and checks that the graph
enters RUNNING. Observe the **physical camera LED** and native preview.
RUNNING alone is not proof of visible pixels.

Interpretation:

- Native camera LED turns on and preview works: Windows hardware works;
  investigate BuildEngine's OpenCV 5.0 DSHOW/videoio integration, not card
  detection.
- Native graph cannot start or camera LED remains off: Windows driver,
  device selection, privacy permissions or hardware activation are still
  suspect. The HRESULT is printed to identify the exact failing COM call.
- If neither native enumeration nor the Windows Camera app sees the expected
  devices, the problem is below OpenCV.

This test is only a Windows hardware diagnostic. It does not write files,
access PostgreSQL, contact Scryfall, or change the running downloader.
It has not been compiled or run on the user's BCC64X system yet.

## Stabilized live view and event semantics

The Windows live preview now uses an off-screen GDI bitmap to paint each frame
in one BitBlt; WM_ERASEBKGND does not clear the client area between frames.
This addresses preview flicker independently of camera FPS.

The camera event state machine emits **one event per continuous card-presence
session**, not a new event whenever the dHash changes. Short gaps in contour
detection and image-content differences do not rearm recognition. To test a
second card, clear the view so no card contour is detected for at least 45
frames (about 1.5 s at 30 FPS), then insert the next card. This is an interim
robustness policy: replacing one card directly with another without an empty
scene is not yet recognized. Exact Scryfall card identification is also not yet
implemented; `edition=UNKNOWN` is expected.

The synthetic self-test now includes alternating image-content jitter, brief
contour dropouts, and removal/reentry. Physical camera behavior remains to be
verified on the Windows BCC64X target.

## Card disappearance log

Each confirmed presentation emits `NEW-STABLE-CARD #N`. After the contour is
absent for 45 consecutive valid video frames, the same card session emits
`CARD-NO-LONGER-DETECTED #N fingerprint=...; absent-frames=45;
ready-for-next-card`. The message deliberately describes *detection*, not
proof of physical removal: occlusions, reflections and background changes can
also hide a contour. No message is emitted on an empty startup scene, on a
short dropout, or repeatedly while the view remains empty. Synthetic smoke
checks these transitions and reentry.

## Camera overlay and OCR

The live preview outlines the detected four-corner card candidate and
displays CARD CONTOUR or NO CARD CONTOUR. The geometry-based detector
does not yet determine card identity or printing.

At CMake configure time, DECKKERNEL_CAMERA_ENABLE_OCR=ON (default)
tries the pinned Tesseract 5.5 and Leptonica 1.87 package configuration.
Look for the "Tesseract OCR enabled" CMake message. If the installed
package is unavailable, the camera still builds without OCR.

OCR needs a local eng.traineddata in the directory passed to --tessdata.
The BuildEngine Tesseract OCR smoke specifies the pinned model. There
are no downloads or network/database writes:

    apps\Release\deckkernel_camera_recognition_test.exe --camera=4 --backend=dshow --width=640 --height=480 --fps=30 --fourcc=yuy2 --ocr --tessdata=D:\path\to\tessdata --save-events

After a NEW-STABLE-CARD event the title band is read with Tesseract
PSM_SINGLE_LINE and the rules-text box with PSM_AUTO. The console
receives [CAM-OCR] card #N name="..." and text="...". OCR may be
inaccurate because the current first-stage geometry does not yet
handle glare, rotation, split-card layouts, and alternate frames.
OCR runs synchronously at the event and may briefly delay the preview.
Edition remains UNKNOWN until a validated candidate match is implemented.

## Required pinned OCR deployment

DECKKERNEL_CAMERA_ENABLE_OCR=ON requires the exact pinned Leptonica 1.87 and
Tesseract 5.5 CMake exports, baseapi.h and eng.traineddata. Missing assets
fail configure rather than silently disabling OCR. Tesseract DLL imports are
staged and installed through the runtime closure helper. The verified English
model is installed in apps/Release/tessdata; with --ocr the executable uses
that directory automatically when --tessdata and TESSDATA_PREFIX are absent.
To intentionally disable OCR, configure with
-DDECKKERNEL_CAMERA_ENABLE_OCR=OFF. The optional argument --ocr then reports
that OCR support was disabled. Real BCC64X runtime behavior remains unverified.

## Original phone photo instead of recording the screen

If the available image is already on a phone, copy its original JPEG/PNG to
the PC. Pointing a webcam at the phone display adds reflections and moire.
For a single photo, use:

    apps\Release\deckkernel_camera_recognition_test.exe --image=D:\Fotos\karte.jpg --ocr --save-events

This bypasses Windows camera capture. The still image is decoded at its
original size and processed once. If a plausible card quadrilateral is found,
the crop is rectified and OCR reads its title and rules area. Otherwise,
OCR tries the original image (best when the picture is tightly cropped).
--save-events optionally writes the rectified crop into the local camera
evidence directory. No Scryfall requests and no PostgreSQL writes occur.

OCR now logs Tesseract's mean confidence separately; outputs below the
provisional confidence floor (45) or with fewer than 3 alphanumeric
characters are labeled UNREADABLE instead of presenting gibberish as a
valid name. OCR accepted as UNVERIFIED is still *not* a card identity.
This confidence filter cannot recover letters lost to overexposure or
reflections. Prefer an original photo or a local screenshot over an
optical recording of a phone display.

## Focus-aware OCR retries and offline artwork candidates

After an initial unreadable title, OCR retries on the still-present card.
New crops are inspected every 30 frames. An attempt is triggered when
Laplacian sharpness increases by 10 percent or at 90 frames regardless
of focus. There are at most eight attempts per card; the same card event
number is retained. A 45-frame disappearance resets the attempt budget.

The optional --artwork switch operates independently of --ocr and does not
require the scantest feature index. It directly reads already-downloaded
art_crop image bytes from deckkernel_test.scryfall_image_assets in
PostgreSQL, joins card face names and compares the illustration region via
dHash64. It never downloads images or writes to the database.

    apps\Release\deckkernel_camera_recognition_test.exe --camera=4 --backend=dshow --width=640 --height=480 --fps=30 --fourcc=yuy2 --ocr --artwork --artwork-limit=100

    apps\Release\deckkernel_camera_recognition_test.exe --image=D:\Fotos\karte.jpg --ocr --artwork

The candidate window is deliberately bounded (default 100 top-ranked indexed
artworks; 1..1000 configurable). It searches the full populated feature
catalog and then re-ranks the shortlisted assets with ORB. Reported
name, asset ID and hamming distance are unverified suggestions, never
proof of a particular Magic printing. Identical artworks may occur in
multiple editions. The first matcher is synchronous at a new-card event
and may momentarily slow camera preview; a future iteration should
separate the image matching from capture and use ORB verification.

## Indexed artwork search in a separate worker

The earlier prototype queried the first N image blobs synchronously during
camera capture. It is replaced with a single background worker that owns its
own PostgreSQL connection and ORB detector. The camera submits one cloned
rectified card per stable presentation into a single-slot bounded queue.
An outdated queued request is replaced, avoiding unlimited memory growth.
Capture, painting and OCR retries continue while SQL and ORB run.

The worker scans the entire version-1 dHash feature catalog (no first-100
asset restriction), orders references by Hamming distance using PostgreSQL
bit_count and selects the closest --artwork-limit candidates (default 100).
Only these candidates have their image_data decoded. OpenCV ORB and a
two-nearest-neighbor ratio test rerank the shortlisted artworks. This
requires the existing indexed features to be populated by scantest. Run
scantest --all before expecting broad artwork coverage; --follow can
continue indexing simultaneously with the camera. No Scryfall requests,
network image downloads, or model schema resets are performed.

The SQL shortlist currently orders all indexed 64-bit hash rows and
limits returned image blobs; this is **not** yet an approximate-nearest-
neighbor database index with sublinear lookup. It does avoid the costly
decode of the full catalog. The recorded match remains UNVERIFIED:
a positive artwork resemblance alone cannot identify a specific printing.
The worker is tested only after execution on the user's BCC64X/PostgreSQL
environment, and current preview graphics do not display its text result.

## Side-by-side live diagnostic preview

The Win32 preview shows two panels in one double-buffered window:

- LEFT: raw live camera with the detected four-corner outline.
- RIGHT: the **actual current rectified 480x672 card pixels** used by the
  OCR retry state machine and the queued artwork matching worker.
  When no contour is found the panel says NO CARD CONTOUR; it does not
  continue displaying an outdated card.

Press **R** while the preview has keyboard focus to switch the actual
rectified card processing orientation between 0 and 180 degrees. The
right-hand panel, OCR and artwork work with the same rotated pixels.
This is manual orientation correction, not automatic orientation detection;
90/270-degree and automatic upside-down recognition are still outstanding.

FindCard now runs only once per live frame, with its result shared by the
processor and preview instead of performing the same expensive contour
extraction twice. The new synthetic self-test verifies that two 180-degree
rotations reconstruct the original pixels and dHash.

## 2026-10-28: orientation, focus and mana diagnostics

The camera now requests continuous autofocus through OpenCV and logs whether
its backend accepts the setting. This is not a guarantee that the camera can
focus on a selected image region: OpenCV VideoCapture does not expose a
portable focus ROI. The stable-card trigger retains the sharpest rectified
frame in its stabilization window instead of blindly using the fifth frame.
A title-OCR comparison between 0 and 180 degrees may correct upside-down
cards before OCR, artwork matching, and evidence saving. An inconclusive OCR
comparison leaves the orientation unchanged; manual R remains available.
Cards presented sideways are normalized geometrically before this comparison.

[CAM-MANA] reports a provisional HSV color histogram from the upper-right
mana region. This is a diagnostic color-candidate signal only; it does NOT
recognize individual mana icons, colorless/white/black costs, hybrid or
Phyrexian symbols. Such symbols require a validated segmented glyph/template
matcher and reference-card tests. No supported runtime with the Windows
BCC64X camera hardware has been tested for this change yet.

## SDL2/Skia dashboard (2026-10-28)

The camera application now has a separate `CameraDashboard.h` backed by
SDL2 window/events/textures and a Skia raster canvas. If both installed
CMake targets `SDL2::SDL2` and `Skia::skia` are available, it replaces
the original Win32 preview for live camera mode. Otherwise the old preview
remains the fallback.

The screen is arranged as a left raw camera panel, a right geometrically
rectified card panel, and a lower group of fields (OCR name, OCR rules text,
mana diagnostics, recognition status, verified identity). R rotates the
current card 180 degrees; Escape closes the dashboard.

An OCR-readable title is shown as *unverified*. The positive recognition
label `Found: <card name>` must only be emitted after the asynchronous
artwork identification result has been validated against the card catalog;
that positive UI handoff is not yet implemented. Mana fields currently
report only unverified hue diagnostics. The right-hand live preview shows
the geometrically corrected frame and may differ from a later auto-rotated
event crop. Neither Windows/BCC64X compilation nor physical GUI testing
has yet been performed for this change.

## 2026-11-10: artwork-first, orientation-aware printing candidates

The `--artwork` option now uses `CardImageMatcher.h`, a single-slot
asynchronous worker and the existing `deckkernel_test.scryfall_image_features`
version-1 feature index. This section supersedes the earlier art-only
matching descriptions above; no Scryfall request or model reset is made.

Recognition order for each stable presentation:

1. Perspective-normalize the camera card, retaining the best available
   camera pixels and the existing manual/OCR orientation selection.
2. Search stored `art_crop` features at orientation 0 degrees, ranking a
   bounded dHash shortlist by persisted ORB descriptors. A hash-only
   nearest neighbour is not sufficient for a positive artwork candidate.
3. If no artwork candidate passes the provisional thresholds, rotate the
   normalized image 180 degrees and repeat the artwork search.
4. When artwork is provisionally recognized, find full-card candidates
   from related image variants: same art asset, or a corresponding
   illustration ID. Consider `normal`, `border_crop` and `large` images
   already indexed by scantest. Join image variants to Scryfall printing
   IDs, sets, collector numbers, languages and faces.
5. When neither artwork orientation passes, search all available indexed
   full-card variants at 0 and 180 degrees as a fallback.
6. Display the asynchronous outcome for the current card presentation
   in the recognition/edition dashboard fields, ignoring stale results
   from earlier events. The console logs each search stage.

Only stored `dhash64-orb` / version 1 descriptors are used. The worker
reads the ORB byte arrays directly from PostgreSQL, without decoding
the original stored reference image blobs and without recomputing
reference ORB features. The dHash shortlist SQL is still a full scan
and sort of eligible bit hashes; it is not a sublinear ANN index.

The stage returns **artwork candidate**, **printing candidate**,
**ambiguous** or **unknown**. All physical-printing matches remain
**UNVERIFIED** until calibrated against a labelled physical-card
corpus. Equal artwork may identify an illustration shared across
printings, and identical or almost identical full-card renderings
cannot be used to claim a unique edition or a foil finish.
No confidence percentage is asserted.

The acceptance and ambiguity thresholds in `CardImageMatcher.h` are
initial engineering thresholds, not measured accuracy limits. Standard
artwork ROIs are provisional; split, adventure, full-art and other
layouts need explicit layout-specific validation. Benchmark both
recognition stages with known physical printings and confirm the
compiled application on Windows/BCC64X before treating the output
as validated.

### Inspect the actual downloaded and indexed variants

The last known import counts do not prove how many version-1 feature
rows were persisted. Run this read-only SQL query against the existing
database; it counts unique assets per variant and avoids treating
shared URLs across printings as separate downloaded images:

```sql
SELECT v.variant,
       COUNT(DISTINCT v.asset_id) AS known_assets,
       COUNT(DISTINCT v.asset_id) FILTER (
          WHERE a.image_data IS NOT NULL
       ) AS downloaded_assets,
       COUNT(DISTINCT v.asset_id) FILTER (
          WHERE f.algorithm = 'dhash64-orb'
            AND f.algorithm_version = 1
            AND f.perceptual_hash IS NOT NULL
       ) AS indexed_assets
FROM deckkernel_test.scryfall_card_image_variants AS v
JOIN deckkernel_test.scryfall_image_assets AS a
  ON a.id = v.asset_id
LEFT JOIN deckkernel_test.scryfall_image_features AS f
  ON f.asset_id = a.id
GROUP BY v.variant
ORDER BY v.variant;
```

Run `deckkernel_card_scantest.exe --all --batch-size=32` to fill
missing feature rows without re-importing images.

## 2026-11-10: captured-card focus and explicit live status

The live camera now starts the indexed artwork/printing matcher by default
(if built with `DECKKERNEL_CAMERA_ENABLE_ARTWORK=ON`). Use `--no-artwork`
to disable matching deliberately. Standalone `--image` and `--sequence`
modes retain explicit `--artwork` opt-in.

After four consistent contour observations, the stable-card state
machine locks one physical card. Changes in illumination, image-content
hashes, OCR or artwork cannot restart a scan of a new card. Only a
confirmed absence of 45 consecutive observations releases the card.
A replacement placed without removing the previous card is intentionally
not distinguished until the removal interval has occurred.

Once captured, the **card is the large primary panel** while live
camera presence-monitoring remains visible in a small panel. The same
rectified capture persists for the whole card session. OCR metadata no
longer rewrites the lower text fields on successive frames. Detection
quality and contour counters are replaced with stable explanations.
The UI reports the sequence through asynchronous status events:
`CARD CAPTURED`, `SEARCHING ARTWORK - orientation 0/180 deg`,
`ARTWORK FOUND - checking concrete printing`, or
`ARTWORK NOT FOUND - full card 0/180 deg`. Final statuses are retained
until the physical card is removed.

The worker diagnoses missing PostgreSQL image data, missing persisted
version-1 feature index, empty ORB descriptors and missing SHA-256 metadata.
A missing SHA-256 is a storage-integrity observation, **not** proof of a
visual-recognition error; dHash and ORB are the matching features.
PostgreSQL connection and query errors are reported to the active
camera presentation instead of leaving the display indefinitely pending.

The self-test now verifies that altered card artwork inside a continuously
present contour cannot produce another presentation. The direct
swap-without-removal scenario is intentionally replaced by an explicit
remove-then-present lifecycle.

This change has been checked structurally against the current repository,
but has **not** been compiled and run on the BCC64X/Windows camera machine
or measured against labelled physical card samples.

## 2026-11-10: focus settling and projective card alignment

A stable card presentation no longer immediately starts artwork index
lookup. The camera first retains the captured high-resolution perspective
rectification and observes up to 18 further valid samples, bounded by
a 2-second settling window. OpenCV scores the card pixels using
Laplacian sharpness, clipping, darkness and exposure. The best sampled
full-resolution card crop is retained. Missing contours cannot replace
it and the time bound avoids indefinite focus delays.

OpenCV requests continuous hardware autofocus when supported by the
camera backend. Focusing on a card-specific region is not guaranteed:
CAP_PROP_AUTOFOCUS is a driver request, not a portable ROI autofocus API.
The sampling stage selects the best observed frame, independently of
driver support. It does not apply synthetic image sharpening.

Perspective normalization now admits real convex quadrilaterals with
unequal opposite edges, even when the minimum-area bounding rectangle
has an atypical aspect ratio. Its vertices are sorted around the
centroid before applying a four-point homography. The rotated-box
fallback remains limited to approximately frontoparallel cards;
a trapezoidal view is not replaced by a guessed rectangle.

A synthetic trapezoidal card was added to the geometric smoke test.
The DB image remains separate from the captured input, and its
original card metadata is displayed without requiring OCR.

This has not been compiled or tested with the user's Windows/BCC64X
camera. Curved cards, partially hidden corners, heavy glare and severe
perspective distortion require dedicated physical-sample tests.

## 2026-11-10: separated physical, capture, and identity state

The camera recognizer now has three independently owned concepts:

- Physical card: the first stable outer contour starts a session. Sparse
  optical flow with region-bounded RANSAC homography follows the whole
  physical card, with feature points distributed over its surface.
  While this lock exists, the live camera no longer globally searches
  for another rectangle inside the card.
- Card capture: a quality-selection window retains the best registered
  high-resolution card image and freezes it once. It also saves a separate
  copy of the same frame's lower 15 percent for printing OCR. Recognition
  and navigation can never silently replace these pixels.
- Card identity: an Oracle name/ID is separate from a ranked set of
  distinct Scryfall printing IDs. The user can browse candidate printings
  with Left/Right and confirm one with Enter.

The post-artwork comparison now considers the normal/large full-card
renderings and two local image regions: set-symbol neighborhood and
lower printed strip. When the image evidence is ambiguous, optional
Tesseract reads only the frozen lower strip as a final escalation,
checking its text against known set codes and collector numbers.
OCR never creates a candidate or claims optical certainty.

The dashboard labels each phase, keeps the camera capture on the
left with its Oracle name, and displays the DB reference on the right
with set and collector number. The printing reference changes on
Left/Right; the locked physical image stays fixed.

See [DEMONSTRATION.md](DEMONSTRATION.md) for the ten stages, invariants,
state machine, keyboard controls, example walkthrough and specific
regression tests. This section supersedes previous descriptions of a
global contour scan running on every frame.

The source now contains PHASE comments at stage boundaries to support
step-by-step teaching. The stage-specific implementation is not yet
validated by a Windows/BCC64X build and physical camera tests. Similarity
thresholds and layout-specific OCR positions remain provisional.

## 2026-11-10: improve the baseline before using OpenCV 5.1

The current on-camera visual hit rate was reported as below 10 percent.
This is an observed baseline reported during development, **not** a
verified benchmark. No improvement in accuracy is claimed until the
same labelled physical cards are replayed and measured.

Changes made using the **existing OpenCV 5.0.0 modules only**:

1. Live camera defaults now request 3840x2160 at 30 fps using MJPG.
   The actual driver-negotiated resolution/FourCC remains authoritative;
   command-line parameters may override the default. The previous
   640x480/auto default discarded most of the potential source detail.
2. PHASE 04 scores the sharpness of title and type-line regions before
   downscaling; the illustration-only sharpness is no longer sufficient.
   It also tracks the **actual number of native camera pixels spanning
   the card**, independently of the size of the perspective-rectified
   output. Upsampling is never accepted as proof of source detail.
3. The capture waits for at least 18 observations and two seconds,
   and only freezes if the best registered frame passes provisional
   photographic quality checks. A time limit does NOT force an unsharp
   capture into the image matcher.
4. After six seconds without acceptable source evidence, the existing
   physical-card session remains in FOCUSING and shows
   **FOCUS INSUFFICIENT**, actual native card width and text-detail
   sharpness. It keeps collecting registered images from the same card
   while asking the operator to move closer or improve camera focus.
   Removal cancels that session. No Scryfall search is started on blur.
5. Artwork search compares both 0 and 180 degree orientations, with two
   plausible modern-frame artwork crops per orientation, before
   accepting an artwork winner. The dHash shortlist was enlarged from
   100 to at least 400 so the correct asset is less likely to be
   discarded before ORB matching. ORB descriptors still come from the
   unchanged version-1 PostgreSQL feature index.
6. A reference-supported 180 degree decision also updates the frozen
   **display copy** and printing-margin OCR input. The original frozen
   CardCapture pixels remain unchanged.

The scoring and thresholds are **provisional**. A test card below the
native-pixel gate is a capture problem, not proof of database or index
failure. The application now prints `[CAM-QUALITY]` diagnostics for
native card dimensions, rectified dimensions, illustration sharpness,
printed detail sharpness, exposure, clipping, and matching eligibility.
The `--self-test` suite additionally compares sharp and blurred
synthetic printing text and checks that upscaling cannot pass the
native-pixel test.

**BuildEngine/OpenCV 5.1 future scope (not enabled here):**
the newer OpenCV 5.1 version is already maintained by BuildEngine.
Keep `photo` (potential multiframe denoising) and `calib`
(camera intrinsic calibration and lens distortion) on the module
evaluation list; verify the exact OpenCV 5.1 module outputs and their
bcc64x consumer smoke tests before updating DeckKernel's CMake
dependencies. Neither module can restore information missing from
a physically out-of-focus image. The present DeckKernel configuration
remains pinned to OpenCV 5.0.0 with its existing required modules.

**Validation still required:** BCC64X build, the existing CTest smoke
suite, camera-mode negotiation, and a reproducible labelled set of
upright/inverted cards across multiple printings. Record: capture
source-card width, accepted/rejected frame quality, artwork top-k
recall, printing top-k recall, false matches and per-stage latency.

## 2026-11-10: restore camera acquisition after excessive layout filtering

A real camera log showed repeated SEARCHING/ACQUIRING transitions at
3840x2160 MJPG 30 fps without a Presented event. The camera mode and
autofocus requests had succeeded; failure occurred **before** focus
selection, dHash/ORB lookup, or printing comparison.

Root cause in the preceding source was twofold:

1. `FindCard(..., boRequireStandardLayout=true)` had a hard
   `if (!layout.boRecognized) continue` rejection. Blurred, reflected,
   rotated, or uncommon internal layouts therefore discarded otherwise
   geometrically plausible physical-card boundaries.
2. `StableCardTrigger` required four immediately consecutive,
   closely aligned detections. One missed contour reset the entire
   acquiring sequence. The dashboard reported SEARCHING immediately,
   causing visible state flicker.

The geometry check is now authoritative for **acquisition**. The
standard-card layout provides a bounded preference and tentative 0/180
orientation only; an unknown internal layout must not prevent a card
from becoming a tracked physical object. The matcher tests both
orientations later. Candidate frame layouts are evaluated only when
their geometry could plausibly beat the current best candidate.

The live acquisition now tolerates up to three consecutive missing
contour observations and permits moderate geometric changes while
stabilizing. The active physical-card tracking and removal policy are
unchanged. The UI stays in ACQUIRING during tolerated gaps.

The maximum allowable contour area was raised from 20% to 85% of the
image, since moving a card close enough for printing-detail sharpness
must not automatically exclude it from detection.

The self-test includes a geometrically valid physical card without
the expected internal standard layout. This tests that layout
recognition is no longer a hidden precondition for first acquisition.

**Verification required:** recompile with BCC64X and run the self-test,
then the same camera invocation. Confirm that the log proceeds from
ACQUIRING to TRACKING/FOCUSING. The earlier low artwork match rate must
only be reassessed after reliable physical-card capture is restored.

## 2026-11-10: distinguish a physical card from a mouse and its inner panels

The observed camera behavior was the reverse of the desired classification:
the detector sometimes selected the mouse, card artwork, or rules text box,
while struggling with the physical outer card. The contour detector was
ranking rectangles by area and a weak aspect-ratio bonus. In particular,
a fallback minimum-area rotated rectangle accepted objects occupying just
68 percent of that box, including rounded mouse-like shapes.

Geometry-first corrections using the **existing OpenCV 5.0.0** modules:

- Candidate outer shape: enforce a broad, perspective-tolerant physical
  card ratio, with a tighter range for an axis-aligned rotated-box fallback.
  A near-landscape inner artwork and very wide type/rules text panels no
  longer compete with a complete portrait card solely by image area.
- Nonrectangular contours: rotated-box recovery now requires at least
  86 percent fill. True quadrilaterals are additionally checked for
  reasonable perimeter approximation. These values are initial engineering
  guards, not validated camera thresholds.
- Strongly oblique real quadrilaterals can remain candidates when their
  opposing edges differ, indicating genuine perspective rather than a
  flat landscape painting.
- Structural support: a physical card can contain several distinctly
  nested rectangular regions. Such regions contribute to candidate
  ranking as evidence for their *outer container* rather than becoming
  alternative physical cards themselves. Internal layout remains optional
  and must not veto an otherwise geometrically valid card.
- Nested rectangular panels are precomputed once per contour pass to
  avoid repeated quadratic contour approximation in the 4K camera loop.

The synthetic `--self-test` now includes an oval mouse and isolated
artwork/rules panels, which must not produce a physical-card event.
The earlier standard-frame, upside-down and projective geometry tests
remain in place.

Use the existing `--verbose` switch to print `[CAM-DETECT]` once per
two-second camera interval. It reports shape/aspect rejection counts,
plausible quadrilaterals, layout assessments, chosen card ratio,
nested panel count, and selected area percentage. The counters refer
to the last observed frame of the interval, **not** all frames combined.

Build and live-camera behavior are **not yet verified** on Windows/BCC64X.
If the physical border is entirely invisible, inner-rectangle evidence
alone does not determine its exact four corners: a future constrained
outer-border reconstruction should require direct full-frame evidence,
rather than inventing an outer quadrilateral and falsely locking a card.

## 2026-11-11: fix the FOCUSING-to-ARTWORK deadlock

Real user screenshots showed two readable cards (Suncorched Desert and
Timbercrown Pathway) held in `04 FOCUSING` with no DB reference search,
and an unrelated computer mouse captured as a physical card. The
physical-card lock and the visual matcher were not themselves the
explanation for the missing Scryfall lookup.

Root-cause fixes in the existing OpenCV 5.0.0 implementation:

- `EvaluateFocus()` previously offered a new candidate only if the
  heuristic standard-frame recognizer returned `boRecognized` and
  `!boRotate180`. This failed even on readable card frames whose
  orientation was reported as uncertain, so the valid-sample counter
  stayed near one and the quality selector could not improve.
  Registered physical-card frames now compete by image quality without
  an unrelated internal-layout veto.
- `CardCapture::Ready()` previously required an uncalibrated printed
  detail sharpness threshold. It could remain false forever on a
  clearly visible card, and even an eventual freeze could skip artwork
  matching because of the same threshold. Focus settling now lasts
  at least 1.5 seconds and eight observations if possible, with a
  **three-second maximum**. Matching always starts on the best captured
  image, regardless of the warning-level quality estimate.
- A real visual quality failure is logged, and can reduce confidence;
  it no longer prevents the DB/index comparison or reports a false
  "no match" merely because the processing stage was never entered.
- The outer physical-card contour must be supported by image edges
  on at least three of four polygon sides. This rejects an arbitrary
  rotated bounding rectangle around an oval mouse where the rectangle
  does not follow the object boundary. Segment clipping protects
  contours near the camera border. This is an initial geometric guard
  that needs camera-sample validation.

Do not change the pinned OpenCV dependency, database index format, or
the separation of physical tracking, immutable capture and Scryfall
identity for these fixes. The initial card remains locked until physical
removal, and image search runs only once for the frozen session.

**Verification pending:** run the BCC64X self-test and camera demo. A
successful acquisition should now log `05 FROZEN` followed by
`06 ARTWORK` and a new `[CAM-ART]` search, even if
`[CAM-QUALITY]` reports `usable=false`. Confirm separately that the
mouse is rejected while ordinary cards still acquire.

## 2026-11-11: stop locking artwork as the physical card

A real camera screenshot showed a crop containing only the illustration,
stretched into a portrait-sized frozen input and then submitted to artwork
matching. This confirmed that the physical-object detector was still
equating a rectangular contour with an entire card. Additional aspect,
area, fill or confidence adjustments had not solved that semantic error.

The **live standard-card acquisition path** now inspects the entire
rectified candidate before creating a physical-card session. It requires
two distinct, widely supported horizontal frame transitions: the upper
title-to-artwork transition and the lower artwork-to-type/text transition.
The check is applied to both 0 and 180-degree orientations. This is not
OCR and does not determine the Oracle or printing identity. A landscape
artwork rotated or stretched into a portrait cannot satisfy the missing
name and text frame evidence merely because its rectangular boundary
looks card-like.

Geometric candidate rejection is separate from the quality of the
eventual camera capture. Once a real complete card is acquired, focus
settling and the image matcher continue as previously separated phases.
The diagnostic `[CAM-DETECT]` line now includes
`full-frame-rejected`, enabling a specific distinction between missing
outer geometry and a partial-card/artwork candidate.

The synthetic tests now explicitly reject an unstructured card-shaped
rectangle and portrait artwork without title/type framing, while keeping
the standard-card and inverted-standard-card fixtures.

**Scope and limitations:** this strict structural acquisition criterion
is for framed standard cards. Full-art, split, textless, adventure and
other nonstandard front designs need separate, validated layout detectors,
not an unconditional bypass. Current checks are deliberately conservative
and have not yet been calibrated on recorded original 4K frames. The UI
screenshot is insufficient to calculate camera-space corner accuracy.
A BCC64X build and the existing `--self-test` and actual camera replay
must validate both false positives and missed real cards before claiming
improved recognition reliability.

## 2026-11-11: isolate the illustration and expose matcher progress

A screenshot of a correctly frozen *Desert's Hold* card showed the UI still
at ARTWORK and no reference image. Unlike earlier failures, **physical
capture succeeded**. A screenshot alone cannot prove whether the worker
was still establishing its PostgreSQL connection, executing a slow dHash
query, evaluating ORB descriptors or finding no acceptable match.

The matching worker is now instrumented instead of adding another
physical-contour heuristic:

- It publishes CONNECTING TO POSTGRESQL before the first connection,
  logs connection duration and sets a five-second PostgreSQL connection
  timeout (overridable with DECKKERNEL_PG_CONNECT_TIMEOUT).
- Each SQL statement has a 12-second PostgreSQL statement timeout.
  Query failures become visible RECOGNITION ERROR results instead of
  leaving the UI indefinitely at an ambiguous ARTWORK stage.
- Both the art_crop lookup and full-card lookup now log the database
  shortlist size, SQL elapsed time, ORB elapsed time and best match
  evidence. The complete worker run also reports its elapsed time.
- The artwork window is no longer limited to the old fixed 19..52 or
  22..50 percent vertical region: on conventional frames the algorithm
  measures the top and bottom illustration separator lines and crops
  between them. When the boundaries cannot be found, a broader fallback
  window is used. Both windows and both 180-degree orientations are
  evaluated before proceeding to complete-card printing matching.
- With --save-events the application now writes the **exact frozen image
  submitted to the matcher** as event-NNNNNN-frozen-search-input.png
  and each evaluated artwork crop as
  event-NNNNNN-artwork-{0,180}-window-{1,2}.png. This makes the
  quality and coverage of the actual search input reproducible.

The ordered identification stages remain: isolate artwork, search the
existing Scryfall art_crop index, retrieve possible Oracle/printing
identities, compare complete normal/large printing images, optionally
corroborate an ambiguous printing through frozen lower-margin OCR, then
allow arrow-key selection and Enter. No new source or index version is
introduced; OpenCV remains 5.0.

**Verification pending:** BCC64X compilation, CTest, and the actual
Windows camera/DB run. The PostgreSQL timeout can expose a previously
hidden slow-query problem; it is **not** an SQL optimization or a
measured improvement in recognition accuracy. Inspect CAM-DB,
CAM-ART-CROP, CAM-ART-QUERY, CAM-PRINT-QUERY, CAM-MATCH, and the saved
crops to distinguish the remaining failure modes.

## 2026-11-11: three-event evidence and low-contrast ORB correction

A user-supplied ZIP contained 449 saved event files, including exact
frozen search inputs and four artwork crops each for recent events
000001, 000002 and 000003. Those three recent captures show the same
illustration at varying 0/180 orientation and with significant blur/
contrast and lighting changes. The recorded camera log for event 000003
contained a complete 500x700 frozen image with 478 pixels of actual
source-card width; PostgreSQL had 2,774,873 downloaded/indexed images,
but artwork ORB had only 1-2 accepted pairs and no candidate passed the
minimum twelve-match acceptance threshold.

A local OpenCV reproduction using the archived camera crops found just
5-8 ORB keypoints on the correctly oriented, unprocessed image inputs.
Under those circumstances twelve true one-to-one matches are
mathematically impossible. CLAHE local contrast normalization with
clipLimit 2.0 and an 8x8 tile grid produced hundreds of query keypoints
(on event 000003, measured artwork crop 6 -> 425). A *camera-to-camera*
comparison of events 000002 and 000003 yielded 2 raw ORB matches and
35 when **both** were contrast normalized. This is a diagnostic
experiment, **not** proof that the user's PostgreSQL reference images
will match, and it is not a recognition-accuracy benchmark.

The camera matcher now applies CLAHE to the QUERY when fewer than 40
raw ORB descriptors were found, while keeping the pre-existing
version-1 PostgreSQL reference index untouched. ORB ratio-test matches
are counted using a one-to-one reference-descriptor constraint: many
camera features mapping to the same stored descriptor are no longer
counted as independent evidence. Thresholds are unchanged and
remain provisional pending reference-image experiments. dHash
shortlisting, artwork cropping and printing lookup remain separate.

To test the *exact same database-recognition input* without a camera
or a second contour detector, run:

~~~bat
apps\Release\deckkernel_camera_recognition_test.exe --frozen-image=.cache\camera-recognition\event-000003-frozen-search-input.png --save-events
~~~

The direct replay uses the existing PostgreSQL index and prints each
worker stage to the console, reports terminal status, asset ID, card
name, set and collector number, and returns exit status 0 on an asset,
2 when unresolved, or 3 when no terminal result is received within
100 seconds. With --save-events it exports evaluated artwork crops
under .cache/camera-recognition/replay and the DB reference image
if one is returned. It does not alter the source images or feature
index.

A next controlled experiment must compare normalized camera and actual
Scryfall art_crop references, then measure whether the correct image
is present among the dHash top-400 shortlist. If it is absent, modify
the candidate retrieval strategy instead of reducing ORB acceptance
thresholds. Do not claim identification success on the basis of extra
CLAHE keypoints alone.

Source review and local image experiments were completed. The BCC64X
compilation, actual database replay and camera recognition rate remain
**unverified**.

## 2026-11-11: illustration-first printing query and preserved scan margins

The November 10 camera log (three real captures) demonstrated that
**artwork recognition worked** for all three close-up cards:
77, 39 and 49 ORB matches, respectively, at 806x1128, 952x1332
and 874x1223 frozen canonical resolutions. The following printing
query then failed on each card with PostgreSQL's 12-second
`statement_timeout`. These are observed logs, not hypothetical
image-score failures.

### Printing lookup: filter card faces first

The old full-card SQL filtered matching art printings inside a
correlated EXISTS after joining the global multi-million-row image
feature index and ordering by perceptual distance. The filter did not
guarantee a small query plan, and all three real examples timed out.

The restricted path now materializes the **related (card_id,face_id)**
rows from the matched art_crop asset or the asset's illustration_id
(using the pre-existing illustration index), then joins those rows to
normal/large/border_crop variants and their feature rows. Only those
relevant variants are ordered by full-card dHash distance. The global
fallback query remains a separate branch for unrecognized artwork.

When a known artwork supplies candidates but full-card ORB/dHash
similarity is insufficient, the known linked printings are **not
discarded**: up to 32 distinct candidate printings are exposed, clearly
marked unverified and available for left/right and Enter selection.
If multiple remain, the optional OCR phase can compare collector/set
information. This does not claim a visually confirmed printing.

Neither schema nor stored feature/index data is changed by this
camera-client update. The 12-second statement timeout remains in
place as an error boundary; the new query plan must be verified with
the user's PostgreSQL instance and may require EXPLAIN ANALYZE
before claiming performance improvement.

### Original-pixel overscan for the collector-number strip

An inspection of the archived `event-00000{1,2,3}-frozen-search-input.png`
showed that the perspective warp was clipped to the detected inner
frame; the collector-number/footer strip was partly or fully
absent. OCR cannot reconstruct information outside that crop.

Every selected source frame now yields **two synchronized views** from
the same camera image and exact same four physical corners:

1. **Canonical crop:** unchanged perspective and dimensions, used
   for existing artwork and full-card dHash/ORB matching.
2. **Overscan crop:** an additional six percent on each side,
   obtained by moving the output destination quadrilateral inward
   while sampling real pixels from outside the detected contour.
   Used for the large left display and the lower-margin OCR.

The extended OCR margin is isolated from 86 to 99 percent of the
overscan image height; this concentrates the final OCR pass on the
collector/set strip rather than the larger rules text field or table
background. The image remains unscaled original source evidence. The original canonical
image is still saved as `event-NNNNNN-frozen-search-input.png`;
`--save-events` now also saves
`event-NNNNNN-frozen-overscan.png`.

A synthetic regression verifies that a printed strip outside the
canonical detected frame is recovered in the overscan and is absent
from the unchanged canonical crop.

**Limitations:** the overscan only recovers pixels already present in
the real 3840x2160 source frame. It cannot recover clipped sensor
content, severe perspective occlusion, missing detail from bad focus,
or a border more than six percent beyond the selected contour.
The physical card still needs sufficient camera coverage for OCR.

No additional OpenCV 5.1 modules are enabled; camera remains pinned
to the existing OpenCV 5.0 components. Future `photo` and `calib`
module evaluation is unchanged.

**Verification pending:** BCC64X build, synthetic self-test, real
PostgreSQL printing-query duration, preview of `frozen-overscan.png`,
and OCR success on known printed collector/set fields.

## 2026-11-13: GUI settings, JSON persistence and physical card orientation

DeckKernel uses a Windows GUI-subsystem executable and retains the SDL2/Skia
scan dashboard. On the first start without command-line arguments, the camera
settings dialog opens if the JSON settings file is missing or invalid. On
subsequent starts it opens the configured camera directly. Use `--settings`
to open the dialog explicitly. The dialog lists named DirectShow devices,
requested resolution and frame rate, saved event images, verbose logging, and
the **direction toward which the physical card's upper edge points**.

Settings are persisted in
`.cache/camera-recognition/camera-settings.json` (schema version 1). The
previous INI configuration and regional scan cropping are obsolete. A
missing/incomplete configuration opens the dialog; canceling does not save
defaults. Camera mode negotiation is still subject to the selected hardware.

Orientation choices are **Automatic**, **Top**, **Bottom**, **Left**, and
**Right**. These are CARD orientations, **not camera-image regions**. Every
mode searches the full image. Automatic retains the previously validated
portrait-card and uncertain 0/180 detection logic. Top and Bottom constrain
upright portrait geometry to ±15 degrees; Bottom explicitly uses 180-degree
normalization. Left and Right rotate the initial camera observation by 90
degrees into the same well-tested portrait detector, then transform all
four corners back into original camera coordinates for the optical-flow
tracker and the perspective warp. The selected direction supplies the known
card orientation, rather than inferring it from imperfect printed separator
lines. No standard-card artwork or printing-matching SQL was modified.
Horizontal scanning may incur an additional full-frame rotation; no
performance claim is made before benchmarks.

The optional `--orientation=auto|top|bottom|left|right` command-line
argument overrides the direction for explicit camera runs. A no-argument
start uses the JSON preferences without displaying the dialog. Existing
non-GUI self-test and offline replay commands still work without opening
settings.

When enabled, `--verbose` appends diagnostics (including async matching
logs) to `.cache/camera-recognition/verbose.log`. Windows message boxes
report startup errors that would otherwise be invisible in a GUI process.

Synthetic regression cases now cover all four physical card-top directions,
including source-space corner conversion. A BCC64X build and Windows camera
smoke test remain pending.

## 2026-11-11: identification regression and targeted rollback

The experimental shared dHash shortlist and split-face matching fallback
caused a significant identification regression in live testing. The four
normal artwork crops were competing for a single global shortlist; a strong
candidate for one crop could be excluded by other crops before ORB ranking.
The matcher has therefore been restored to its previous per-window SQL
shortlist and early-stop behavior, as used before the experiment.

The physical card detector (including the borderless silhouette fallback,
nested panels and outer-frame recovery), fixed-focus default, and camera
session handling are unchanged. Split-face identification remains unresolved
and requires a separately validated approach; do not treat the reverted
experimental split search as active. No local BCC64X build was run.

## 2026-11-11: outer-frame recovery after card candidate selection

Before rectifying a selected card, the contour detector now searches outside
the candidate rectangle for stronger evidence of the physical card boundary.
A narrow band of up to ten percent of the card's corresponding dimensions
is examined along each side. At least two independently supported, mostly
parallel exterior edge segments must be found before the quadrilateral is
expanded; otherwise the original corners are preserved.

The original 15-degree outer-card orientation constraint is unchanged.
The recovered corners, rather than the inner printed frame, feed subsequent
high-resolution OCR rectification and artwork feature matching. The
`outer-corrected` CAM-DETECT field indicates when an expanded boundary
was accepted. Synthetic regression checks an outer border with a
right-side extension, modestly convex left/right edges, and the absence
of unsupported expansion.

This is an evidence-based local recovery, not an unconditional percentage
crop. It must still be validated against real 4K Brio acquisitions and
unconventional card layouts.

## 2026-11-11: externally configured fixed focus is the default

Live camera acquisition now preserves the focus settings established outside
DeckKernel, such as a manually selected fixed tabletop focus in Logi Tune.
Neither camera startup, the F hotkey, nor the next-card transition writes
`CAP_PROP_AUTOFOCUS` or `CAP_PROP_FOCUS` in the default mode. This avoids
restarting autofocus and camera-level table calibration on every card.

- Default: fixed/external focus; camera focus controls are not modified.
- `--auto-focus`: opt into the previous table-plane autofocus calibration
  and its continuous-autofocus fallback. `--table-focus` remains an alias.
- `--continuous-autofocus`: request ordinary continuous hardware autofocus.
- `--focus-position=N`: request an explicit driver-specific manual value;
  requires camera support and verification, and may fall back to autofocus.

Important: preserving an external focus setting does not prove that Windows
or the camera firmware retained it across device opening. Verify the actual
image sharpness after startup. The existing per-card best-frame selection and
image-quality diagnostics remain active. F only requests recalibration when
an explicit software-focus mode is enabled. Actual device validation with
the Logitech Brio is still required.

## 2026-11-11: nested card panels and mixed face orientations

Physical-card acquisition now retains the 15-degree upright requirement for
the **outer** card only. During each bounded Canny contour pass, it collects
rectangular inner panels whose long axes are parallel or perpendicular to
the outer card's long axis, within 5 degrees in either case. Diagonal inner
panels are not counted as structural evidence. A plausible physical
outer contour with at least two distinct, spatially separated nested panels
can supply structural evidence where the usual standard-card title/art/text
separator classifier fails. The outer card must still pass existing geometric
and supported-edge tests. The existing 0/180 image-matching fallback remains
unchanged.

The live CAM-DETECT trace now includes `nested-layouts` (candidate contours
accepted specifically through nested panels) and `rotated-panels` (nested
panels perpendicular to the outer card axis). A synthetic split-face
regression checks that the accepted rectangle is the outer physical card,
not one of its inside panels.

This is an alternative structural cue, not proof of card identity. Nonstandard
split, borderless and double-face layouts still require real camera sample
validation; no improvement in recognition accuracy is claimed without it.

## 2026-11-11: portrait-only tabletop acquisition

The camera's physical-card contour stage now checks the long axis of each
minimum-area rectangle before expensive edge coverage, nested-panel scoring,
or perspective rectification. The table workflow accepts cards positioned
within 15 degrees of the image's vertical axis, with the same rule for
180-degree upside-down cards. Sideways artwork-like rectangles are discarded
at this early geometric stage. The diagnostic counter
`orientation-rejected` records these candidates.

The measured long-axis direction is not the card's printed reading
direction. Unknown 0/180 orientation therefore stays on the existing
unrotated capture path, and indexed matching continues to compare both
orientations. No additional hard gate based on printed layout recognition
is introduced. The self-test covers 0 and 12 degrees as accepted and 24
and 90 degrees as rejected. These are synthetic geometry tests; real camera
validation is still needed, especially under projective distortion.

For a fixed tabletop camera with consistent illumination, a camera that
supports a verified manual focus position could hold that setting rather
than recalibrating for each card. The present hardware focus fallback is
unchanged, because programmable manual focus has not been verified for the
candidate device.

## 2026-11-11: adaptive frozen-frame selection

The capture stage now monitors printed-detail sharpness independently of the
overall image-quality score. It freezes after at least eight valid card
samples and 350 ms without a significant printed-detail improvement, provided
the selected frame passes the existing usability checks and the capture has
lasted at least 550 ms. It retains a three-second upper bound for weak or
unstable input, preserving the previous best frame rather than taking the
latest frame. A still-improving card is not frozen solely because 1.5 seconds
have elapsed. The verbose CAM-FOCUS line reports frame count, elapsed time,
printed-detail score, freeze reason (stabilized/deadline), and usability.

This change does not modify physical-card contour acceptance, the camera's
hardware autofocus API, or indexed artwork matching. It requires validation
with the actual BCC64X camera and frozen image evidence before recognizing a
quality improvement.

## 2026-11-11: focus the fixed horizontal tabletop before card search

New real camera logs and original saved frames separate *recognition* from
*optical acquisition*. On the same 3840x2160 DirectShow/MJPG device,
strongly focused captures had camera sharpness scores of about 346 and
685, whereas difficult table captures were around 25-30. In the latter
cases, the raw artwork ROI sometimes produced 0-5 ORB keypoints. CLAHE
can produce additional features but **cannot restore optical detail**.

A fixed overhead camera should have its expected *table plane* in focus
before physical-card contour detection. The camera now enables
`--auto-focus` explicitly (legacy table-plane calibration); the default
now preserves the camera's external fixed-focus setting without writing
autofocus properties. Continuous autofocus remains available with
`--continuous-autofocus`.

PHASE 00, `TABLE FOCUS`, precedes the physical-card state machine.
It first enables camera autofocus and observes the middle 30% by 60%
region of the actual original camera frame while autofocus settles.
The region is drawn as a rectangle in the live image during calibration.
Measurements require enough contrast, optical sharpness, illumination
and a stationary image; the algorithm avoids locking to low-texture
or moving input. It records the device-reported focus positions and
their corresponding measured ROI sharpness.

After 2-4 seconds of settling, if enough good measurements are
available, the best observed device focus position is applied using
`CAP_PROP_AUTOFOCUS=0` and `CAP_PROP_FOCUS`. Device acknowledgements
and the focus-position readback are verified. A separate 0.6-1.2s
verification compares sharpness against the calibration frames.
If any camera control is unsupported, the data is inadequate or
optical quality is significantly worse after focus locking, it
automatically restores continuous autofocus and reports the reason.
The hardware focus must NEVER be silently assumed to have changed.

**Important:** the highlighted software ROI is a *measurement* region.
OpenCV's portable VideoCapture API does not configure the camera
driver's internal autofocus ROI. This calibration works only if the
camera reports a usable manual focus position and permits it to be
locked. Some DirectShow devices support only continuous autofocus,
or give untrustworthy reported values. A blank, low-texture tabletop
is also insufficient for reliable software focus calibration.

Camera options:

~~~bat
apps\Release\deckkernel_camera_recognition_test.exe --camera=4 --fps=30 --fourcc=mjpg --table-focus --save-events --verbose

apps\Release\deckkernel_camera_recognition_test.exe --camera=4 --fps=30 --fourcc=mjpg --continuous-autofocus

apps\Release\deckkernel_camera_recognition_test.exe --camera=4 --fps=30 --fourcc=mjpg --focus-position=NN
~~~

The last command is an **explicit manually measured focus position** in
the device's own focus units. Never guess NN: use a value documented or
calibrated for the actual camera. The driver may reject it and the
program then falls back to continuous autofocus. `--focus-position`
takes precedence over table calibration when both are supplied.

Press **F** in the preview to rerun the configured focus procedure,
**only when no physical card is currently locked**. The stable card,
its frozen capture, printing candidates and database queries are
not changed by recalibration. If a locked card is present, the command
is ignored and logged. `[CAM-TABLE-FOCUS]` reports calibration steps,
selected device focus position, ROI sharpness, verification or fallback.

**Validation pending:** Windows/BCC64X build and hardware tests with the
specific user camera. Focus on the horizontal surface should improve
table-card detection only when the camera's manual focus control is
functional and the surface provides enough features for calibration.
Neither camera minimum focus distance nor numerical focus range is
established by the existing logs.

## 2026-11-11: explicit scan workflow and persistent collection list

The scanner now uses explicit user decisions rather than requiring
physical card removal before recognizing the next card.

**Scan lifecycle**

1. TABLE FOCUS runs before each new scan, using the configured hardware
   focus procedure or its documented continuous-AF fallback.
2. SEARCHING/ACQUIRING/TRACKING/FOCUSING identifies a complete physical
   card and selects a source image.
3. Once FROZEN, the physical detector and optical tracker are **paused**.
   The video device may remain open for the preview/UI, but no further
   card contours or alternative artworks are searched from new frames.
   The immutable frozen snapshot is the only input to the image matcher.
4. ARTWORK and PRINTING use the existing PostgreSQL index; OCR of the
   lower overscan band is a last resort for ambiguous printings.
5. If one or more printing candidates are available, the operator selects
   with **Left/Right** or the **mouse wheel**, and accepts using
   **Enter** or the **left mouse button**. A printing must be present;
   accepting before recognition finishes does not add an invented item.
6. An accepted card is appended once to the third, right-hand
   **COLLECTED CARDS** list using `Name (Set)`; the structured record
   also retains the printing ID, collector number and Oracle ID.
   Confirmation is an explicit operator selection, not stronger
   image-identification evidence. The list remains across scans.
7. **Escape** or the **right mouse button** discards the current scan
   without appending anything. Either operator decision resets the
   physical tracker, captured images, Oracle/printing candidates and
   diagnostics and begins TABLE FOCUS for the next scan.
   A reject during SEARCHING can also restart tabletop focus.
8. Closing the application uses the normal window close control. Escape
   is now reserved for scan discard and does not close the program.

The asynchronous matcher results are separated by monotonically
increasing physical card event numbers; explicit reset does not
restart numbering. A late DB selection response from a discarded
scan must not overwrite the next scan. Regression checks cover
the trigger reset and unique event numbering.

**Layout**: live/frozen camera on the left, selected Scryfall DB image
and printing details in the middle, accepted-card list on the right.
The list is in acceptance order and displays its most recent entries
when the available screen height is insufficient.

**No changes** are made to the PostgreSQL index schema, ORB feature
data, or Printing/Artwork acceptance thresholds in this interface
update. The BCC64X compilation failure from a duplicate local
`aBlurred` in the focus self-test is also corrected by giving the
table-focus metrics a unique variable name.

**Verification pending**: BCC64X compilation and the SDL/Win32 live
event loop must be exercised with keyboard, wheel and mouse. Verify
both one-candidate and ambiguous-printing cases, discard during
active asynchronous matching, successive cards added to the list and
the table-focus restart on each decision.

## 2026-11-11: tabletop detection regression and unrelated full-card result

Two overhead-camera screenshots show complete but small, inverted MTG
cards on a wooden tabletop. A third screenshot shows a captured inverted
card next to an unrelated DB printing ("Praetorhoof Behemoth"). The
diagnosis separates **physical object detection** from **identity
verification**; an apparently plausible whole-card match is not enough.

### Physical acquisition: broken outer outlines

Offline analysis of the downscaled provided screenshots (not the
original 3840x2160 frames) found that low-threshold Canny (15/40)
revealed card-shaped contours which had previously been rejected as
minAreaRect fallbacks. Some had only approximately 17-73% contour fill
despite strong straight-side evidence. This happens when the visible
physical border is broken into partial edge contours. An 86% area-fill
requirement is therefore not a reliable prerequisite for a physical card.

The minimum-rectangle recovery path now checks at least three strongly
supported sides for low-fill contours. The four-side, strongly
supported geometric fallback is allowed only at the expected physical
aspect ratio and above the specified area floor if the provisional
internal title/artwork/text classifier is inconclusive. Normal
standard-layout evidence is still preferred. A third, low-threshold
Canny contour pass runs only if standard and contrast-enhanced passes
produce no accepted card. The additional `[CAM-DETECT]` counters
`broken-outline` and `physical-outline` show where these paths
participate.

These rules deliberately do **not** accept every minAreaRect. A rounded
mouse with only tangent contacts should still fail the straight
side-coverage requirements. As with all current heuristics, this must
be validated against real tabletop frames and multiple non-card objects.

### Full-card identity: require independent artwork evidence

An image containing similar card borders and printed text can pass
global full-card dHash/ORB thresholds while depicting entirely different
artwork. Therefore, **a whole-card fallback cannot identify a card
solely from global full-card features**.

Up to twelve top full-card references are decoded from the already
indexed DB as before. Their isolated illustrations are compared to the
camera illustration using CLAHE-assisted query descriptors, ratio-tested
and unique-reference ORB matches. A full-card fallback now requires
both the previous whole-card acceptance floor and at least 12 unique
artwork correspondences. Candidates with independent artwork evidence
are ranked ahead of text/frame-only results; unverified candidates
cannot be presented as an identified printing or cause orientation
correction. Failed corroboration produces
`FULL-CARD UNRESOLVED - artwork uncorroborated`, with a
`[CAM-FULL-REJECT]` record. The per-reference
`[CAM-FULL-ART]` line reports full-card versus illustration matches.

The artwork threshold remains provisional and is not a reported
accuracy benchmark. If the camera image is blurry, the correct card
may also fail this test; an unresolved result is preferable to a
wrong identification. The pre-existing art_crop-first lookup and its
illustration-linked printing selection remain unchanged, as do the
PostgreSQL feature index and pinned OpenCV 5.0 runtime.

**Verification status:** the source was edited and structurally
checked. BCC64X compilation and real-time camera/database acceptance
tests are not performed in this environment. Use the same cards to
compare: card acquisition count, non-card false locks, artwork-first
top-k hits, wrong fallback identities and latencies.

## 2026-11-11: quiet fast live scanning and real last-resort OCR

A real test with `--camera=4 --fps=30 --fourcc=mjpg` produced a large
unconditional diagnostic stream even without `--verbose`. It showed
that a **discarded** image job continued executing SQL, ORB and a global
full-card fallback while the next camera scan was already underway.
One unrecognized card spent approximately 12.45 seconds in the worker.
On the same camera, manual focus locking repeatedly failed through
DirectShow (`focus lock not supported/read back`).

The ordinary live scan now uses these rules:

1. Console diagnostics and their formatting are disabled by default.
   Errors still go to stderr; the dashboard continues to show all user
   states. `--verbose` explicitly enables detailed output, but does
   **not** select a different recognition algorithm.
2. Source-image saving, intermediate crops and evidence files remain
   off unless `--save-events` is specified. No image-index cache,
   persistent PostgreSQL features or downloaded artwork are deleted.
3. A strong artwork match (at least 25 unique ORB correspondences and
   dHash distance no more than 20) stops the remaining art orientation
   and crop queries; printing lookup is then limited to the linked
   illustration and its actual printing variants.
4. Without a reliable artwork hit, the default path no longer performs
   the expensive, unreliable global full-card top-100 dHash/ORB fallback.
   For a controlled comparison, `--deep-search` explicitly enables
   that legacy fallback. `--verbose` remains independent.
5. Escape/right-click invalidates the active matcher event and drops
   pending tasks/results immediately. An in-progress PostgreSQL
   statement may complete, but subsequent SQL and ORB phases are
   skipped and obsolete results cannot update a newer card.
6. A camera that rejects manual focus locking is remembered for the
   current application session. Subsequent scans use continuous
   autofocus immediately instead of repeating several seconds of
   ineffective tabletop calibration.

**The previously missing OCR fallback is now connected.** After the
complete visual artwork search fails, and only then, a separate OCR
task reads the *title band* of the immutable frozen card in both 0 and
180 degree orientation. A result below 65 OCR confidence or shorter
than three characters remains unresolved. Otherwise the existing
PostgreSQL worker makes an **exact, case-insensitive card-face name**
query, using the stored Scryfall records. Matching names yield up to
32 unverified printing choices. The operator must still select the
correct printing with arrows or mouse wheel and explicitly confirm;
OCR text does not certify the card's set, printing, or identity.
If visual artwork recognition succeeds but several printings remain,
the separate lower-margin OCR still assists that narrower choice.
Asynchronous OCR results carry the original event ID and cannot
overwrite a later scan.

Suggested ordinary test:

~~~bat
apps\Release\deckkernel_camera_recognition_test.exe --camera=4 --fps=30 --fourcc=mjpg
~~~

For explicit analysis:

~~~bat
apps\Release\deckkernel_camera_recognition_test.exe --camera=4 --fps=30 --fourcc=mjpg --verbose --save-events --deep-search
~~~

**Verification pending:** source/branch consistency has been checked,
but Windows BCC64X compilation and real camera/database timing have
not yet been executed here. Benchmark separately: physical-card
acquisition, strong artwork response, no-visual-match OCR escalation,
exact-name DB result, acceptance/escape latency and printer candidate
loading. No new measured throughput or accuracy figure is claimed.
