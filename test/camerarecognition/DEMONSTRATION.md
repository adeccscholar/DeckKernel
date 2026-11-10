# Camera recognition: architecture and live demonstration

Status: implementation prepared on 2026-11-10. Windows/BCC64X compilation,
real-camera results and accuracy calibration are not verified.

## Non-negotiable invariants

- Once a physical card is acquired, global contour search is disabled.
  Artwork inside that card is not a competing physical card.
- All visual recognition and optional OCR consume the same immutable
  frozen image of the current physical card.
- Oracle identity, physical card tracking and printing selection belong
  to different owners. Manual confirmation is not proof of optical uniqueness.

## Responsibility split

| Component | Ownership |
|---|---|
| StableCardTrigger + PhysicalCardTracker | One physical object, session ID, four corners, sparse optical flow, RANSAC homography, removal |
| CardCapture | Best original-resolution card sample, immutable frozen card and lower-margin crop |
| CardIdentity | Oracle name/ID, distinct printing candidates, selected printing, manual confirmation |
| CardImageMatcher | Async PostgreSQL dHash/ORB search, full-card region comparisons, DB reference loading |
| CameraDashboard | Two image panels, state indication, arrow keys and Enter; no image classification |

## Phases

~~~mermaid
stateDiagram-v2
    [*] --> Searching
    Searching --> Acquiring: whole-card candidate
    Acquiring --> Tracking: stable physical card
    Tracking --> Focusing: locked four corners
    Focusing --> Frozen: best frame selected
    Frozen --> Artwork: immutable image
    Artwork --> Printing: artwork match or full-card fallback
    Printing --> PrintingOcr: visually ambiguous, OCR available
    Printing --> Selecting: ambiguous, no OCR
    Printing --> Unresolved: insufficient evidence
    PrintingOcr --> Selecting: OCR corroboration or inconclusive
    Selecting --> Selecting: Left/Right
    Selecting --> Confirmed: Enter
    Tracking --> Searching: confirmed removal
    Focusing --> Searching: confirmed removal
    Frozen --> Searching: confirmed removal
    Artwork --> Searching: confirmed removal
    Printing --> Searching: confirmed removal
    PrintingOcr --> Searching: confirmed removal
    Selecting --> Searching: confirmed removal
    Confirmed --> Searching: confirmed removal
    Unresolved --> Searching: confirmed removal
~~~

### 01 SEARCHING: global detection

FindCard performs a global outer-card contour search only without
an active physical object. The current card must not be replaced by
an inner artwork rectangle while a lock exists.

### 02 ACQUIRING: stable physical presentation

StableCardTrigger confirms the original outer contour and emits
exactly one presentation event. The event number guards against
late background-worker results from earlier cards.

### 03 TRACKING: follow the physical card

PhysicalCardTracker distributes Shi-Tomasi features over the complete
card: borders, header, artwork, type bar and lower print margin.
Lucas-Kanade optical flow moves those features across frames, and
a RANSAC homography predicts new outer corners. OpenCV 5.0.0 provides
findHomography, projective transforms, contour geometry and related
functions through its geometry module; goodFeaturesToTrack is declared
by the features module. No private RANSAC solver is necessary. Tracking failure
temporarily counts as absence; no competing global contour is selected.
A persistent absence releases the card after debounce.

### 04 FOCUSING: choose source quality

The camera requests continuous autofocus where the backend supports
it. The image-space quality selector independently evaluates up to
18 registered images or two seconds of sampling. Sharpness, clipping
and darkness matter. Enlarging a blurry crop never creates real detail.

### 05 FROZEN: immutable capture

CardCapture freezes the best full-resolution rectified source and
retains its original lower 15 percent as an independent OCR crop.
No subsequent camera frame silently overwrites it.

### 06 ARTWORK: Oracle identification

At 0 and 180 degrees, the async matcher checks indexed art_crop
features using dHash64 and ORB. When artwork is unrecognized,
it performs complete-card fallback searches at both orientations.
The Oracle name is displayed on the left.

### 07 PRINTING: distinguish the actual output

The matcher evaluates indexed normal/large/border_crop references
linked to Scryfall printings. It compares the whole card plus two
local regions: the printed lower strip and set-symbol neighborhood.
The right side shows the DB reference with set and collector number.
Different image sizes of the same printing are deduplicated.

The shortlist and region-score weights are provisional. Candidate
truncation, indistinguishable printings and weak visual evidence
must never be presented as proven unique physical printing identity.

### 08 PRINTING OCR: optional last escalation

Only if several visual candidates remain, a background Tesseract
worker reads the frozen lower margin. Its result is compared
only against known candidate set codes and collector numbers.
Both must match, OCR confidence must clear its provisional threshold,
and exactly one candidate may satisfy them. OCR never invents a
printing or discards alternatives from the database.

### 09 SELECT PRINTING: user choice

Left/Right changes only the selected right-side DB image and printing
metadata. The left frozen card and its Oracle identity never change.
Press Enter to confirm; the manual selection stays distinct from
automatically established evidence.

### 10 CONFIRMED or UNRESOLVED

A manual confirmation is recorded for the current physical session.
Without enough evidence, DeckKernel reports unresolved rather than
claiming a unique printing. Remove the physical card to reset every
session model.

## Live demonstration sequence

| Step | Action | Explain |
|---|---|---|
| 1 | Show an empty surface | SEARCHING permits global discovery |
| 2 | Place a card | ACQUIRING creates one physical presentation |
| 3 | Translate and gently tilt it | TRACKING follows the whole object and corrects perspective |
| 4 | Hold position | FOCUSING chooses the best source sample |
| 5 | Wait for freeze | The left image becomes immutable |
| 6 | Watch artwork and printing stages | Different levels of certainty |
| 7 | Present a card reused in different sets | Full-card local regions narrow candidate printings |
| 8 | Observe optional bottom-margin OCR | Printed evidence checks only known candidates |
| 9 | Press Left / Right | Only the DB reference on the right changes |
| 10 | Press Enter | Manual printing confirmation |
| 11 | Remove the card | All three owners reset; next card may be acquired |

Use log tags [CAM-PHASE], [CAM-TRACK], [CAM-FOCUS],
[CAM-ART], [CAM-PRINT], [CAM-PRINT-OCR], [CAM-REFERENCE]
to narrate the phases and actual evidence.

## Build and verification

The camera target needs the pinned OpenCV video module for optical flow,
in addition to core, imgproc, imgcodecs, geometry and features. No calib3d
module is required by the camera target.
Tesseract remains separately optional at build time.

Execute the existing --self-test target first. Then validate real
physical cards: a flat standard card, perspective skew, slight motion,
glare, poorly focused lower text, two near-identical printings,
missing DB reference bytes, and actual card removal. Record accuracy
and false-positive printing rate before tuning thresholds.

The current tracker can lose texture-poor or rapidly moving cards.
Planar homography does not correct curved or hidden card surfaces.
Fixed print-margin positions are optimized for conventional layouts.
A matching OCR token is corroboration, not a calibrated probability.

## Physical-card tracking fallback and UI consistency

Sparse optical-flow tracking requires features spread across multiple
rows and columns of the complete card. If a texture-poor card cannot
provide that distribution, the tracker retains the same physical lock
using local card/background contrast near the original quadrilateral.
This does not search globally, identify a new rectangle, or claim to
track large movements without sufficient image evidence. Removal is
still debounced.

Optical-flow and feature extraction operate on a region surrounding
the locked card, not a repeated full-frame 4K contour search.
When the active capture is frozen, the SDL card texture is reused until
its underlying image changes.

Printing selection requests carry monotonically increasing request
numbers. Late PostgreSQL responses from older arrow-key selections
are discarded. The previous DB image is cleared when another printing
is selected, so it cannot appear under the new printing's set/number.
A pending reference download is still allowed to finish after Enter
for the confirmed printing, without changing the confirmed status.

The manual R rotation control cannot modify a frozen capture. The
recognizer already checks both 0 and 180 degree orientations.
