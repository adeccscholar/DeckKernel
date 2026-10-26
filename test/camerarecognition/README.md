# CameraRecognitionTest: stage 0

This is a second, independent DeckKernel test program. It does NOT recognize a
specific printing yet: it detects a card-shaped quadrilateral, rectifies it,
waits for five stable frames, and emits one NEW-STABLE-CARD event. Removal for
six frames rearms detection, including when the same card is reinserted.

The test never calls Scryfall, never reads or writes the card database and
cannot interrupt the multi-day image download. Snapshot writing is opt-in.

## Build

The same bootstrapped BCC64X/CMake/Ninja toolchain as scantest is required.

    cmake -S test/camerarecognition -B test/camerarecognition/build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build test/camerarecognition/build/Release
    ctest --test-dir test/camerarecognition/build/Release --output-on-failure
    cmake --install test/camerarecognition/build/Release

Usage:

    apps\Release\deckkernel_camera_recognition_test.exe --self-test
    apps\Release\deckkernel_camera_recognition_test.exe --sequence=testsamples\camera --save-events
    apps\Release\deckkernel_camera_recognition_test.exe --camera=0 --save-events

Live camera mode requires opencv_videoio to be exported by the installed
BuildEngine OpenCV package; existing scantest does not prove that component
is available. Without it the project still builds its replay and self-test.

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
