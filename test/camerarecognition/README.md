# CameraRecognitionTest: stage 0

This is a second, independent DeckKernel test program. It does NOT recognize a
specific printing yet: it detects a card-shaped quadrilateral, rectifies it,
waits for five stable frames, and emits one NEW-STABLE-CARD event. Removal for
six frames rearms detection, including when the same card is reinserted.

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
