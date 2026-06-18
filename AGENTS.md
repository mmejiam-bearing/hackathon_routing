# AGENTS.md — Maritime Router

Authoritative ruleset for any agent (Claude, Copilot, Cursor, or similar) or
developer working in this repository.  Read this file before touching any code.

---

## Repo structure

This repo contains **three separate executables** and a shared library.
Never conflate them.

```
maritime-router/
├── lib/                     INTERFACE library — shared headers, no compiled sources
│   └── include/maritime/    All shared types: StaticGraph, WeatherBuffer, edge_weight, etc.
│
├── graph_builder/           Program 1: offline pipeline, run infrequently
│   └── src/                 Reads open-source datasets → writes graph.bin, flags.bin, snap.bin, cch_topo.bin
│
├── weather_etl/             Program 3: 6-hour scheduled job (no main — static lib + Python ETL)
│   └── src/                 Reads .npy arrays → writes weights.bin
│
├── router_server/           Program 2: long-running query server
│   └── src/                 Reads all artifacts → serves CCH route queries
│
└── tests/
    ├── unit/                Fast, no I/O, no binary artifacts
    └── integration/         Writes temp files, tests full round trips
```

### Which program owns what

| Concern | Program |
|---|---|
| Parsing GEBCO/GSHHG/ENC data | `graph_builder` only |
| CCH topology build (slow, minutes) | `graph_builder` only |
| Loading `.npy` weather arrays | `weather_etl` only |
| Computing CCH edge weights | `weather_etl` only |
| Writing `weights.bin` | `weather_etl` only |
| Serving route queries | `router_server` only |
| CCH metric customisation (fast, seconds) | `router_server` only |
| Atomic weather buffer swap | `router_server` only |

If a change touches code that crosses these boundaries, it is almost certainly wrong.

---

## Language and standard

- **C++23 exclusively.** No C++17/20 compat shims unless explicitly bridging RoutingKit.
- RoutingKit sources are compiled at C++17 (set in CMakeLists — do not change).
- All new code goes in the `maritime` namespace, or a sub-namespace
  (`maritime::graph_builder`, `maritime::weather_etl`, `maritime::router_server`).

---

## RAII contract — non-negotiable

Every resource must be owned by a type that manages its lifetime automatically.
Violating this is always wrong, even for "temporary" code.

### Rule of Zero (default)

Apply to every new type unless a resource forces otherwise.  A type whose
members are all standard library types (`vector`, `shared_ptr`, `span`, etc.)
gets correct lifecycle behaviour from the compiler for free.  Do not write
destructor, copy, or move functions for such types.

### Rule of Five (only when forced)

Apply when a type must own a resource that has no existing RAII wrapper — a
raw file descriptor, a `mmap` region, a CUDA handle, a C library opaque pointer.
When you must write Rule of Five:

- Delete copy constructor and copy assignment unconditionally.
- Implement move constructor and move assignment with the null-out pattern
  (set the source to a safe moved-from state so its destructor is a no-op).
- Document why Rule of Zero was impossible.

`MmapRegion` in `lib/include/maritime/mmap_region.hpp` is the canonical
example.  It is also the **only** existing Rule of Five type.  If you find
yourself writing a second one, check whether `unique_ptr` with a custom
deleter would work instead.

### Forbidden patterns

These are always wrong regardless of context:

```cpp
// WRONG — raw owning pointer
T* ptr = new T(...);

// WRONG — manual delete
delete ptr;

// WRONG — manual munmap without RAII wrapper
munmap(ptr, size);

// WRONG — raw FILE* without RAII
FILE* f = fopen(...);

// WRONG — exception-unsafe manual cleanup
T* p = new T;
do_something_that_might_throw();  // leak if this throws
delete p;
```

Correct replacements:

```cpp
auto ptr = std::make_unique<T>(...);           // unique ownership
auto ptr = std::make_shared<T>(...);           // shared ownership
MmapRegion region(path);                       // mmap via RAII wrapper
std::ifstream f(path, std::ios::binary);       // file via RAII
```

---

## Memory ownership — `shared_ptr` for shared weather buffers

The `WeatherManager` uses `std::atomic<std::shared_ptr<const WeatherBuffer>>`.
This is intentional and must not be changed to a raw pointer.

- `acquire()` increments the refcount atomically — no lock, no allocation.
- `update()` atomically swaps the pointer — no lock, no downtime.
- A query thread that holds a `shared_ptr` keeps the buffer alive for its
  entire lifetime even if `update()` fires mid-query.
- The `WeatherBuffer` destructor fires automatically when the last holder
  releases — no manual cleanup needed.

C++23 guarantees `std::atomic<shared_ptr<T>>` is lock-free on x86-64 and
AArch64.  Do not add a mutex around it.

---

## Thread safety rules

| What | Rule |
|---|---|
| `StaticGraph` (mmap, read-only) | Safe to read from any thread, any time |
| `WeatherManager::acquire()` | Safe from any thread, atomic load |
| `WeatherManager::update()` | Safe from any thread, atomic store |
| `CchQueryState` / `QueryState` | **Per-thread only** — never share between threads |
| `RoutingEngine::route()` | Safe from any thread — uses thread_local query state |
| `QueryServer::serve_query()` | Safe from any thread |
| `WeightsWriter::write()` | Single-writer; do not call concurrently on same path |

`thread_local` query states are reconstructed automatically when the CCH metric
generation counter advances.  Do not cache or move them manually.

---

## Binary file formats — do not change without a version bump

All binary artifacts are read by memory-mapping them at startup.  Changing
the layout without bumping the version field will cause silent data corruption.

| File | Magic | Version field | Defined in |
|---|---|---|---|
| `graph.bin` | `0x4752414D` "MARG" | `GraphHeader::version` | `lib/include/maritime/static_graph.hpp` |
| `snap.bin` | `0x50414E53` "SNAP" | `SnapHeader::version` | `lib/include/maritime/static_graph.hpp` |
| `weights.bin` | `0x54484757` "WGHT" | `WeightsHeader::version` | `lib/include/maritime/weights_header.hpp` |

Rules for format changes:

1. Increment the version field in the header struct.
2. Add a version check in the reader (`StaticGraph` constructor or
   `WeightsLoader::load()`).
3. Update the offline `graph_builder` to write the new version.
4. Update `AGENTS.md` and `README.md`.
5. Never silently read an old format as if it were new.

---

## Weather grid — confirmed constants, do not guess

Confirmed from inspection of the actual `sigwh.npy` file:

```cpp
WX_NJ          = 721       // latitude points, 90N → 90S
WX_NI          = 1440      // longitude points, 0E → 359.75E
WX_N_POINTS    = 1,038,240 // total cells per timestep
WX_N_TIMESTEPS = 24        // hourly, 24-hour forecast horizon
WX_N_TOTAL     = 24,918,960 // total elements per variable
dtype          = float16   // _Float16, ~2 MB per variable per timestep
lon convention = 0 → 360   // NOT -180 → 180
NaN            = land mask  // redirect via snap table, never leave as NaN
```

**Known ETL double-write bug:** every `.npy` file on S3 contains the array
data written twice (file size = `2 × n_elements × 2` bytes).  `numpy.load()`
silently returns only the first copy.  All C++ loaders (`NpyLoader`,
`snap_table_builder`) read only the first `WX_N_POINTS` elements.
Do not fix this silently — the ETL fix requires a coordinated deploy.

**Index formula** (include the snap table, use the normalised longitude):

```cpp
if (lon < 0.f) lon += 360.f;
int lat_i = std::clamp((int)((90.f - lat) / 0.25f), 0, 720);
int lon_i = std::clamp((int)(lon / 0.25f), 0, 1439);
// redirect via snap table — snap_lat/snap_lon map land cells to nearest ocean
std::size_t idx = time_step * WX_N_POINTS + snap_lat[lat_i*1440+lon_i] * 1440 + snap_lon[...];
```

Never index the weather tensor without going through the snap table.

---

## RoutingKit dependency

RoutingKit is fetched by CMake `FetchContent` at build time.  It has no CMake
build system — the root `CMakeLists.txt` wraps its sources into
`routingkit_static`.

**Commit pin:** `ROUTINGKIT_COMMIT` in `CMakeLists.txt`.  There are no version
tags in RoutingKit.  To update: run `git ls-remote https://github.com/RoutingKit/RoutingKit HEAD`
and replace the hash.  Test the full build and integration tests before committing.

**License:** BSD-2-Clause.  No attribution required in binary distributions,
but the LICENSE file must be retained.

**Warnings:** RoutingKit sources are compiled with `-w` (suppress all warnings).
Do not remove this — the library uses patterns that produce warnings under C++23.

**CCH two-phase model:**

- Phase 1 (topology): `graph_builder` builds it once, saves via `CchPreprocessor::save()`.
- Phase 2 (metric): `router_server` loads the topology, then calls
  `CchIndex::customise(weights)` every 6 hours when new `weights.bin` arrives.
- Queries: `CchQueryState::run()` — per-thread, reconstructed on metric generation bump.

---

## Testing

### Run all tests

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest --output-on-failure
```

### Unit tests

Located in `tests/unit/`.  Must:
- Run in < 1 second total.
- Require no binary artifacts, no network, no S3.
- Use `fs::temp_directory_path()` for any temporary files.
- Clean up in `TearDown()`.

### Integration tests

Located in `tests/integration/`.  Must:
- Write synthetic binary files to a temp directory, not to the project tree.
- Test the full serialise → deserialise round trip for each binary format.
- Clean up in `TearDown()`.

### Adding a new test

1. New unit test → add file in `tests/unit/`, add source to `maritime_unit_tests`
   target in `tests/CMakeLists.txt`.
2. New integration test → add file in `tests/integration/`, add source to
   `maritime_integration_tests` target.
3. Test names follow `TEST(SuiteName, DescriptiveCamelCase)`.
4. Every public function in `lib/` must have at least one unit test.
5. Every binary format must have a round-trip integration test.

---

## Code style

- **No raw owning pointers.** See RAII section above.
- **`[[nodiscard]]`** on every function that returns a value the caller must use.
- **`noexcept`** on move constructors, move assignments, and functions that
  cannot throw (e.g. accessors, bitwise operations).
- **`const`** on all member functions that do not mutate state.
- **Structured bindings** (`auto [a, b] = f()`) are preferred over `std::get<>`.
- **`std::span`** for non-owning array views — never raw pointer + length pairs.
- **`std::string_view`** for read-only string parameters in hot paths.
- **`if constexpr`** over SFINAE or `#ifdef` for compile-time dispatch.
- One class per header file.  Header files go in `lib/include/maritime/` (shared)
  or alongside their `.cpp` in the program's `src/` directory (private).
- Include order: own header first, then `maritime/` headers, then RoutingKit
  headers, then standard library headers, then POSIX headers.

---

## What agents must NOT do

- Do not add raw `new` / `delete` calls.
- Do not add `malloc` / `free` calls.
- Do not change the `cch_topo.bin` format without updating both
  `CchPreprocessor::save()` (graph_builder) and `CchIndex` load path
  (routing_engine.hpp).
- Do not read `.npy` files inside `router_server` — that is `weather_etl`'s job.
- Do not write `weights.bin` inside `graph_builder` — that is `weather_etl`'s job.
- Do not add CMake `target_compile_options` that remove `-march=native` —
  the `vcvtph2ps` instruction for float16 → float32 conversion requires it.
- Do not change `WX_NI`, `WX_NJ`, or `WX_N_TIMESTEPS` without verifying against
  the actual `.npy` files on S3.  These are confirmed constants, not assumptions.
- Do not add `std::mutex` to `WeatherManager` — it is intentionally lock-free.
- Do not suppress the ETL double-write in a way that changes the file format
  without a coordinated ETL deploy.

---

## Pre-merge validation checklist

Run through this in order before merging any branch that touches `lib/`,
`graph_builder/`, `weather_etl/`, or `router_server/`.

- [ ] **1. Remove build artifacts**
  ```bash
  rm -rf build/
  ```

- [ ] **2. Remove graph artifacts**
  ```bash
  rm -f data/artifacts/graph.bin data/artifacts/flags.bin \
        data/artifacts/snap.bin  data/artifacts/cch_topo.bin \
        data/artifacts/weights.bin
  ```

- [ ] **3. Recompile everything from scratch**
  ```bash
  cmake -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build --parallel
  ```

  - [ ] **3a. Add tests for any new public function before this step**
    - New function in `lib/` → add unit test in `tests/unit/`
    - New binary format → add round-trip integration test in `tests/integration/`
    - Update `tests/CMakeLists.txt` with the new source file
    - Test names: `TEST(SuiteName, DescriptiveCamelCase)`

- [ ] **4. Re-generate graph artifacts**
  ```bash
  ./build/graph_builder/maritime-graph-builder \
    --gebco data/gebco/... --gshhg data/gshhg/... \
    --out-dir data/artifacts/
  ```
  *(Adjust paths to actual dataset locations.)*

- [ ] **5. Run all tests**
  ```bash
  cd build && ctest --output-on-failure
  ```

- [ ] **6. Confirm all tests pass**
  - Zero failures reported by ctest.
  - No new compiler warnings introduced in `lib/`, `graph_builder/`,
    `weather_etl/`, or `router_server/` sources (RoutingKit warnings are
    suppressed by `-w` and are pre-existing — ignore them).
  - If any test fails: fix the root cause, do not skip or `DISABLED_` the test.
