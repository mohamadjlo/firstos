# FirstOS In-System AppDev Platform: Verification Report

**Generated**: 2026-10-15 03:26:34
**Status**: PASS (6/6)
**Environment**: QEMU x86_64 (two boots sharing fs.img)
**Project under test**: `/Projects/AppT27576`

| Test ID | Check | Result | Evidence |
| :--- | :--- | :--- | :--- |
| APPDEV_01 | App Studio init | **PASS** | init line in boot log |
| APPDEV_02 | Project wizard | **PASS** | /Projects/AppT27576 |
| APPDEV_03 | In-system build | **PASS** | /Projects/AppT27576/build/app.elf |
| APPDEV_04 | Ring 3 run | **PASS** | process_spawn succeeded |
| APPDEV_05 | Stop running process | **PASS** | explicit STOP of live PID |
| APPDEV_06 | Persistence across reboot | **PASS** | project re-opened and built from fs.img after reboot |

Serial logs: `reports/appdev_logs/boot1_serial.log`, `reports/appdev_logs/boot2_serial.log`

## Known limitation
The "build" step in v1 validates the source and installs a pre-compiled template ELF
into `build/app.elf`; it does **not** compile the user's C source in-system yet.
