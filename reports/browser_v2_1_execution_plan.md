# FirstOS Browser Engine v2.1 — Execution & Verification Plan

## Objective
Elevate the FirstOS Web Browser Engine from v2.0 to **v2.1** through rigorous platform completion and hardening:
1. Conduct strict Truth Verification of v2.0 baseline against active build, QEMU, and test suites.
2. Upgrade Web Storage (`localStorage`) from volatile RAM-only to persistent, origin-scoped storage integrated with FirstOS VFS/disk image while keeping `sessionStorage` strictly session-scoped.
3. Enhance high-impact JavaScript and DOM APIs (`querySelectorAll`, `classList`, `createElement`, `appendChild`, `removeChild`, attribute management, basic `fetch`/XHR and microtask hooks).
4. Harden network and protocol handling (redirects, chunked transfer, headers/cookies).
5. Execute a 110-target web compatibility audit and document metrics, regressions, and final release assessment.

---

## Stages & Milestones

| Stage | Key Deliverable | Target Output Files |
|---|---|---|
| **Phase 1: Truth Verification** | Full rebuild, QEMU boot, regression suites execution, serial capture | `reports/browser_v2_1_truth_check.md`, `reports/browser_v2_1_build_results.md`, `reports/browser_v2_1_qemu_boot.log`, `reports/browser_v2_1_runtime.log` |
| **Phase 2: Persistent Storage** | VFS-backed origin-scoped storage, atomic serialization, reboot persistence, corrupted file recovery | `reports/browser_v2_1_storage_report.md`, `reports/browser_v2_1_storage_results.csv` |
| **Phase 3: JS/Web API Completion** | DOM tree mutation APIs (`createElement`, `appendChild`, `removeChild`), `classList`, `querySelectorAll`, `fetch`/XHR stubs | `reports/browser_v2_1_js_api_report.md`, `reports/browser_v2_1_js_results.csv` |
| **Phase 4: Network Hardening** | Redirect status handling (301, 302, 307, 308), chunked transfer decoding, basic cookie storage | `reports/browser_v2_1_network_report.md` |
| **Phase 5: Compatibility Campaign** | 110-target evaluation matrix with classification (REAL_SITE_PASS, REAL_SITE_PARTIAL, etc.) | `reports/web_compatibility_matrix_v2_1.csv`, `reports/web_compatibility_failures_v2_1.md`, `reports/web_compatibility_progress_v2_1.md`, `reports/web_compatibility_fixes_v2_1.md` |
| **Phase 6: Metrics & Telemetry** | Extended telemetry counters (`storage_writes`, `storage_reads`, `storage_persisted_after_reboot`, `js_errors`, etc.) | `reports/browser_v2_1_metrics.md` |
| **Phase 7: Release Assessment** | Quantitative evaluation of criteria, release version determination (v2.1 vs v2.0.1), and handoff dossier | `reports/browser_v2_1_release_assessment.md`, `reports/browser_v2_1_perplexity_handoff.md` |
