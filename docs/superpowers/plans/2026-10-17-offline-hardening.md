# Offline hardening without a second reader

User scope: complete useful offline work, retain hardware checks in the panel.
No new system installation, re-pairing, threshold change or backend switch.

- [x] Reproduce and fix custom build-directory setup, including spaces, missing
  artifacts, argument validation and mocked service restoration.
- [x] Reproduce malformed baseline acceptance; require exact-size finite values,
  regular non-symlink files, selected-directory lookup and unique temporary writes.
- [x] Reproduce predictable credential temporary-file overwrite; use unique private
  temporary files, checked writes/path bounds and reject symlink destination directory.
- [x] Distinguish absent optional capture configuration from malformed/unreadable data.
- [x] Refuse automatic pairing when credentials exist, are partial or inaccessible.
- [x] Run complete offline driver/workbench suites and ASan/UBSan tests.
- [x] Refresh local native libfprint compile prototype; document its tested revision.
- [x] Publish reviewed driver, update workbench pin, prepare the next GUI checkpoint.

Not replaced by offline tests: cancellation/reopen on the actual reader, suspend,
biometric accuracy, fresh pairing on another machine and upstream acceptance.
Per-device state migration and async operation orchestration remain separate changes;
this patch does not claim they are solved by stronger file validation.


Outcome: driver v0.1.2 published; the installed system module remains v0.1.1.
Synthetic regression coverage and sanitizer checks passed; the native prototype
build passed. Review caught an overbroad cache-isolation comment, corrected to
refer only to directory fallback. No multi-device isolation is claimed.
