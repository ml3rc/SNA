# cJSON (vendored)

Unmodified upstream source, release **v1.7.19**
(https://github.com/DaveGamble/cJSON/releases/tag/v1.7.19). MIT licensed,
see `cJSON-LICENSE`.

Built as its own CMake target without our `-Wall -Wextra`. No build-time
configuration needed: `ENABLE_LOCALES` is left undefined, so `cJSON.c` never
pulls in `<locale.h>` (the decimal point is always `'.'`), which matters on
this bare-metal target.

To update: replace `cJSON.c`/`cJSON.h` with a newer tagged release, check the
upstream `CHANGELOG.md` for breaking API changes, then rebuild and run the
host tests (see `../../../README.md`).
