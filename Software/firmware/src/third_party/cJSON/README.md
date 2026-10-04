# cJSON (vendored)

Unmodified upstream source, pinned to release **v1.7.19**
(https://github.com/DaveGamble/cJSON/releases/tag/v1.7.19), fetched
2026-10-01 directly from `raw.githubusercontent.com` — not reproduced from
memory. MIT licensed, see `cJSON-LICENSE`.

Replaces the hand-written `src/json_reader.c`/`src/json_writer.c` (removed)
used earlier in the project when the firmware toolchain wasn't yet working
and vendoring an unverifiable library was judged riskier than a small,
fully-reviewable scanner. That constraint no longer applies — the toolchain
builds and flashes real hardware now — so the project switched to a real,
widely-used library instead of continuing to maintain bespoke JSON code.

No build-time configuration needed: `ENABLE_LOCALES` is left undefined, so
`cJSON.c` never pulls in `<locale.h>` (decimal point is always `'.'`),
which matters on this bare-metal target.

To update: replace `cJSON.c`/`cJSON.h` with a newer tagged release from the
same URL pattern, re-run the full build + protocol test suite (see
`Software/firmware/README.md`), and check `CHANGELOG.md` upstream for any
breaking API changes.
