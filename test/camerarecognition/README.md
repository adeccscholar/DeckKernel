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
