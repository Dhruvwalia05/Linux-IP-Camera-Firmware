```markdown
# Linux IP Camera Firmware

A production-oriented Linux IP camera firmware architecture written
from scratch in C++17 — structured the way real embedded Linux
products are structured, not as a collection of demos.

**Status:** Phase 1 complete · Phase 2 complete · Phase 3 next
**Language:** C++17 · **Build:** Make · **Analysis:** Valgrind (Memcheck + Helgrind)
**Tests:** 16 binaries · 349 checks · 100% pass · Memcheck & Helgrind clean

---

## Overview

This is a single embedded Linux firmware application, built
incrementally, component by component. The finished system will
capture video via V4L2, encode to H.264/H.265, serve RTSP streams,
publish telemetry over MQTT with TLS, and accept signed OTA updates
with anti-rollback.

The project's real subject is **engineering discipline**: lifecycle,
ownership, concurrency, failure handling, memory safety, and race
detection are treated as first-class concerns — not afterthoughts
added after "it works."

**Not a tutorial.** The code does not skip correctness for brevity.
**Not bare-metal.** It targets Linux; it uses pthreads, sockets,
signals, V4L2, and device interfaces.
**Not finished.** Phases 1 and 2 of 6 are complete; the roadmap
reflects a multi-month effort.

---

## Current Status

| Phase | Scope          | Status         |
| ----- | -------------- | -------------- |
| 1     | Foundation     | ✅ Complete    |
| 2     | Core Runtime   | ✅ Complete    |
| 3     | Services       | ⏳ Next        |
| 4     | Security       | ⏳ Planned     |
| 5     | OTA            | ⏳ Planned     |
| 6     | Integration    | ⏳ Planned     |

### Phase 1 — Foundation (complete)

| Component      | Purpose                                  |
| -------------- | ---------------------------------------- |
| Logger         | Thread-safe logging with level filtering |
| SignalHandler  | Cooperative SIGINT/SIGTERM shutdown      |
| ThreadManager  | Worker thread lifecycle management       |
| Queue          | Bounded producer/consumer FIFO           |
| FirmwareApp    | Application lifecycle / state machine    |

### Phase 2 — Core Runtime (complete)

| Component       | Purpose                                          |
| --------------- | ------------------------------------------------ |
| TimerScheduler  | One-shot and periodic timers                     |
| EventBus        | Synchronous in-process publish/subscribe         |
| IPC             | Unix domain socket message-passing               |
| Watchdog        | Health-aware `/dev/watchdog` integration         |

---

## Architecture

Dependency direction is strictly downward. Lower layers never know
about higher ones.

```
        Application        main.cpp · FirmwareApp
             │
             ▼
         Services          Camera · Network · MQTT · RTSP · OTA · ...
             │
             ▼
         Framework         Logger · ThreadManager · Queue
                           TimerScheduler · EventBus · IPC · Watchdog
             │
             ▼
        Platform/Linux     sockets · signals · threads · V4L2 · timers
             │
             ▼
           Linux
```

**Rule:** framework components are domain-agnostic. A `Queue` knows
nothing about MQTT. A `TimerScheduler` knows nothing about camera
frames. Service behavior lives in services.

---

## Building

**Requirements**

- Linux (x86_64 or aarch64)
- `g++` 13+ with C++17
- `make`
- POSIX threads
- Optional: `valgrind` 3.20+

**Commands**

```bash
make              # build production binary + all test binaries
make firmware     # production binary only
make tests        # test binaries only (no execution)
make clean        # remove build/
make help         # list targets
```

The production binary is written to `build/firmware`.

---

## Running

```bash
./build/firmware
```

Current behavior: initializes Logger and SignalHandler, enters the
run loop, and exits cleanly on **Ctrl+C** or **SIGTERM**. As Phase 3
services land, the startup sequence will become progressively richer.

---

## Testing

Two kinds of targets:

**Per component** — build the component's binaries, run them natively,
then run Memcheck and Helgrind on each. One command per component:

```bash
make test-logger        # 4 binaries
make test-signal
make test-thread        # 4 binaries
make test-queue         # 2 binaries
make test-timer
make test-event
make test-ipc
make test-watchdog
make test-firmware-app
```

Each target builds only what it needs, including dependencies. For
example, `make test-watchdog` compiles `test_watchdog.cpp` plus
`watchdog.cpp`, `thread_manager.cpp`, `stop_token.cpp`, and
`logger.cpp`, then runs the full test → Memcheck → Helgrind cycle.
Nothing else in the project is touched.

**Full suite** — every component in sequence:

```bash
make run-tests
```

Fails fast on the first broken component and prints the diagnosis.

### Test suite

| Binary                              | Coverage                                          |
| ----------------------------------- | ------------------------------------------------- |
| `test_logger`                       | Lifecycle, level dispatch, output                 |
| `test_logger_shutdown`              | Shutdown sequencing, re-init, concurrency         |
| `test_logger_thread`                | 4 threads × 100 messages                          |
| `test_logger_config`                | Configuration, filtering, output routing          |
| `test_signal_handler`               | SIGINT/SIGTERM, handler restore, RAII             |
| `test_thread_manager`               | 25 lifecycle and concurrency checks               |
| `test_thread_manager_stress`        | 1000-iteration stress (custom-only, no Valgrind)  |
| `test_thread_manager_stress_small`  | 20-iteration variant — Valgrind-clean             |
| `test_thread_manager_timeout`       | `Join(timeout)`, thread naming, truncation        |
| `test_queue`                        | 81 checks: FIFO, capacity, shutdown modes         |
| `test_queue_timeout`                | 33 checks: `PushFor` / `PopFor`                   |
| `test_firmware_app`                 | State machine, idempotency, RAII, threads         |
| `test_timer_scheduler`              | 40 checks: lifecycle, periodic, cancel, throws    |
| `test_event_bus`                    | 38 checks: delivery, ordering, RAII, concurrency  |
| `test_ipc`                          | 63 checks: framing, timeouts, stale-socket recovery |
| `test_watchdog`                     | 43 checks: device, health aggregation, lifecycle  |

### Valgrind

Every component passes both tools:

- **Memcheck** (`--leak-check=full`) — `0 bytes in 0 blocks`, `0 errors`
- **Helgrind** (`--tool=helgrind`) — `0 errors from 0 contexts`

Where C++11 `std::atomic` performs lock-free handoff, the code uses
explicit annotations (`ANNOTATE_HAPPENS_BEFORE` /
`ANNOTATE_HAPPENS_AFTER`) in `framework/util/helgrind_annotations.h`.
These are no-ops in production builds and only active when
`<valgrind/helgrind.h>` is available at compile time.

A small set of documented suppressions in
`framework/util/valgrind.supp` silences glibc's internal condvar
wake-chain signaling — behavior that Helgrind cannot model. These
suppressions anchor on glibc frames and never match project code.

The 1000-iteration ThreadManager stress test runs natively only.
A reduced-iteration build (`-DSTRESS_ITERATIONS=20`) runs under
Helgrind for bounded race-analysis runtime.

---

## Component Overview

### Logger — `framework/logger/`

Single shared logging facility. Four levels
(`DEBUG` / `INFO` / `WARN` / `ERROR`), millisecond timestamps, kernel
thread IDs (matching `top -H`), runtime level filter, configurable
output (`stdout` / `stderr`). API is additive via `LoggerConfig`.
Thread-safe via one internal mutex.

### SignalHandler — `framework/signal/`

Process-wide `SIGINT` / `SIGTERM` handling for cooperative shutdown.
Installs via `sigaction(2)`, captures and restores previous handlers,
explicit `State` enum, RAII destructor. Rejects double `Initialize()`;
safe `Shutdown()` without `Initialize()`.

### ThreadManager — `framework/thread/`

Lifecycle for a single worker thread. `Start` / `Stop` / `Join` /
`Join(timeout)`. Workers receive a `StopToken&` and exit
cooperatively. Named threads via `pthread_setname_np`. Destructor
emits a diagnostic and calls `std::terminate()` if a worker is still
running — ownership must be explicit, not hidden by a blocking
destructor.

### Queue — `framework/queue/`

Bounded, thread-safe FIFO. `Push` (non-blocking) / `PushFor` (timed),
`Pop` (blocking) / `PopFor` (timed). Two shutdown modes: `IMMEDIATE`
(discard) and `DRAIN` (finish existing items). Move-only payloads
supported. Internal state machine:
`RUNNING → DRAINING → SHUTDOWN`. Two condition variables so producers
and consumers do not contend on the same CV.

### FirmwareApp — `app/`

Lifecycle entry point. `main.cpp` is deliberately tiny. Explicit state
machine: `UNINITIALIZED → INITIALIZED → RUNNING → SHUTDOWN`.
`Initialize()` rejects double-init and init-after-shutdown; failures
roll back cleanly. `Run()` blocks until a signal or
`RequestShutdown()`. `Shutdown()` is idempotent and transition-safe
(`state.exchange`); subsystems tear down in reverse init order.
Destructor is a safety net.

### TimerScheduler — `framework/timer/`

Shared scheduler thread managing up to 64 timers. `ScheduleOneShot` and
`SchedulePeriodic` with `FIXED_DELAY` (period from callback return) or
`FIXED_RATE` (period from prior deadline, with catch-up cap). Callbacks
run outside the lock, so a callback may schedule or cancel without
deadlocking. Cancellation is cooperative: a `RUNNING` callback finishes
naturally and is not re-armed. Callback exceptions are caught, logged
to `stderr`, and cancel only the offending timer.

### EventBus — `framework/event/`

Synchronous, in-process, type-keyed publish/subscribe. Events are
identified by C++ type, not string topics — typos are compile errors.
`Subscribe` returns a RAII `Subscription` handle (marked
`[[nodiscard]]`) whose lifetime is the subscription's lifetime.
Delivery order equals subscription order. Subscriber exceptions are
caught and logged; remaining subscribers still receive the event.
Re-entrant `Publish` returns `REENTRANT_PUBLISH`. Capacity bounded to
128 total subscribers. Snapshot-based dispatch so callbacks may
`Cancel` without invalidating the in-flight iteration.

### IPC — `framework/ipc/`

Unix domain socket message-passing for single-machine communication.
Length-prefixed framing (4-byte big-endian header + payload), bounded
messages (1 MB), timed blocking Send/Recv with `poll` and absolute
deadlines. Server uses `flock`-based single-instance enforcement plus
probe-based stale-socket cleanup. `MSG_NOSIGNAL` on every send so
peer close produces `PEER_CLOSED` rather than `SIGPIPE`. Single-client
server; caller-supplied socket path.

### Watchdog — `framework/watchdog/`

Health-aware hardware watchdog integration. Services register named
health checkers before `Start()`. A monitor thread kicks the device
only when all checkers report healthy; unhealthy checks skip the kick
and let the hardware timer fire. Device access is abstracted behind
`IWatchdogDevice` so tests run against `NullWatchdogDevice` without
`/dev/watchdog`. Monitor kicks at `timeout / 2` to absorb scheduler
jitter. Clean shutdown writes the magic `'V'` handshake before close,
so the kernel does not fire the watchdog after the process exits.

---

## Roadmap

### Phase 3 — Services *(next)*

- **Config** — configuration loading and validation
- **Network** — TCP, TLS, reconnection with exponential backoff
- **MQTT** — client, topics, QoS, TLS, persistence
- **Camera** — V4L2 capture, format negotiation, error recovery
- **Motion** — frame-difference detection on a decimated stream
- **RTSP** — RFC 2326 server with RTP streaming

### Phase 4 — Security

- **Crypto** — hash, signature, key storage abstraction
- **Secure Storage** — platform-agnostic API over platform-specific storage
- **Firmware Verification** — image hash + signature check
- **Secure Boot model** — trust chain from bootloader through application
- **Anti-Rollback** — monotonic version enforcement

### Phase 5 — OTA

Signed firmware download · integrity + signature + version
verification · A/B partition install · health check + automatic rollback

### Phase 6 — Integration

Lifecycle wiring of all services · failure injection · soak testing ·
final architecture documentation

---

## Design Principles

1. **Cooperative cancellation.** No thread is forcibly killed. Workers
   observe a stop signal and exit on their own terms, so mutexes, file
   descriptors, and memory are never abandoned mid-operation.
2. **Explicit ownership.** A service owns its threads, queues, and
   file descriptors. Shutdown order is visible in code, not implied by
   destruction order. RAII handles (`Subscription`, file descriptors,
   `StopToken`) make ownership changes obvious.
3. **Failure is a first-class concern.** Every component has an error
   enum. Every error path is tested.
4. **Framework stays generic.** No framework component knows about
   domain concepts.
5. **Tests precede confidence.** A component is not "done" until it
   passes functional, lifecycle, concurrency, stress, Memcheck, and
   Helgrind checks.
6. **Honest concurrency analysis.** Where C++11 atomics do lock-free
   handoff, we annotate for Helgrind rather than suppress.
   Suppressions are reserved for genuine library-level false positives
   and are documented in-repo.
7. **No hidden blocking in destructors.** `std::terminate()` is
   preferred over silently joining a thread whose stop request was
   ignored.

---

## Conventions

- **Language:** C++17 (`-std=c++17`)
- **Warnings:** `-Wall -Wextra -Werror` — a clean build has zero warnings
- **Threading:** `std::thread`; raw pthreads only where the Linux API
  requires it (thread naming)
- **Tests:** one standalone binary per component, own `main()`
- **Verification:** every component must pass native tests, Memcheck,
  and Helgrind before commit
- **Commits:** one logical change per commit; messages explain *why*,
  not just *what*
- **README:** updated in the same push as any change that adds/removes
  a top-level component or changes build/test commands

---

## Repository Layout

```
Linux-IP-Camera-Firmware/
├── app/                Application layer (main.cpp, FirmwareApp)
├── framework/          Generic framework components
│   ├── logger/
│   ├── signal/
│   ├── thread/
│   ├── queue/
│   ├── timer/
│   ├── event/
│   ├── ipc/
│   ├── watchdog/
│   └── util/           Helgrind annotations, Valgrind suppressions
├── services/           Domain services (empty — Phase 3)
├── platform/linux/     Platform-specific code (empty)
├── configs/            Runtime configuration (empty)
├── scripts/            Utility scripts (empty)
├── tests/              One directory per component
├── Makefile
├── .gitignore
├── LICENSE
└── README.md
```