# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

### [0.1.1] - 2026-06-30

#### Changed
- Refactored internal data structures: replaced anonymous tuples with named structs (`Achievement`, `Locked`, `CollectionFlag`, `UnlockNotification`), extracted timing/animation constants into a `timing` namespace, and switched the renderer to RAII lifetime management. No behavior changes.

### [0.1.0] - 2025-10-20

#### Added
- Initial release.
