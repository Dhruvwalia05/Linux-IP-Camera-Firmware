````markdown
# Linux IP Camera Firmware

A production-oriented **Linux IP camera firmware architecture**
implemented from scratch in C++17, structured the way real embedded
Linux products are structured — as one coherent firmware system, not
a collection of demos.

**Status:** Phase 1 (Foundation) complete.

**Next:** Phase 2 (Core Runtime).

**Not yet implemented:** video capture, encoding, RTSP, MQTT, OTA, security.

---

## Table of contents

- [What this is](#what-this-is)
- [What this is not](#what-this-is-not)
- [Goals](#goals)
- [Current status](#current-status)
- [Architecture](#architecture)
- [Building](#building)
- [Running the firmware](#running-the-firmware)
- [Testing](#testing)
- [Component overview](#component-overview)
- [Design philosophy](#design-philosophy)
- [Development conventions](#development-conventions)
- [Repository layout](#repository-layout)
- [Roadmap](#roadmap)
- [License](#license)

---

## What this is

A single embedded Linux firmware application, built incrementally,
component by component. The project is being written with the
engineering discipline a real product demands: lifecycle, ownership,
concurrency, failure handling, memory safety, and race detection are
treated as first-class concerns, not afterthoughts.

The finished system will eventually:

- Capture video from a Linux V4L2 device
- Encode to H.264/H.265
- Stream over RTSP to standard clients (VLC, ffmpeg)
- Publish telemetry and accept commands over MQTT with TLS
- Accept signed OTA updates with anti-rollback
- Model a Secure Boot trust chain
- Recover from camera, network and disk failures

See [Roadmap](#roadmap) for what is currently implemented versus planned.

## What this is not

- **Not a tutorial.** The code does not sacrifice correctness for brevity.
- **Not a bare-metal or RTOS firmware.** This targets Linux and uses
  pthreads, sockets, signals, V4L2, and Linux device interfaces.
- **Not finished.** Phase 1 of 6 is complete. The Roadmap is a
  multi-month plan; every unchecked item is genuinely unimplemented.

---

## Goals

**Engineering**

1. Build a firmware architecture that resembles a real product.
2. Understand *why* each abstraction exists — who owns it, how it
   fails, how it shuts down.
3. Reach production-grade quality in concurrency, lifecycle, failure
   handling, memory safety, and race detection.
4. Test every important error path, not just the happy path.

**Career**

Demonstrate practical embedded Linux engineering capability:
C++17, multithreading, systems programming, camera/V4L2, networking,
and eventually security and OTA.

---

## Current status

**Phase 1 — Foundation: COMPLETE**

| Component      | Status | Purpose                                  |
| -------------- | :----: | ---------------------------------------- |
| Logger         |  Done  | Thread-safe logging with level filtering |
| SignalHandler  |  Done  | Cooperative SIGINT/SIGTERM shutdown      |
| ThreadManager  |  Done  | Worker thread lifecycle                  |
| Queue          |  Done  | Bounded producer/consumer FIFO           |
| FirmwareApp    |  Done  | Application state machine                |

**Phase 2 — Core Runtime: NEXT**

TimerScheduler · Event · IPC · Watchdog

Phases 3–6 (Services, Security, OTA, Integration) are planned and
documented in [`docs/ROADMAP.md`](docs/ROADMAP.md). None of their
code exists yet.

---

## Architecture

Dependency direction is strictly downward. Lower layers never know
about higher ones.

```text
Application main.cpp · FirmwareApp
│
▼
Services Camera · Network · MQTT · RTSP · OTA · ...
│
▼
Framework Logger · ThreadManager · Queue · Timer · ...
│
▼
Platform/Linux sockets · signals · threads · V4L2 · timers
│
▼
Linux
````

**Rule:** framework components are domain-agnostic. A `Queue` knows
nothing about MQTT. A `TimerScheduler` knows nothing about camera
frames. Service behavior lives in services.

---

## Building

**Requirements**

* Linux (x86_64 or aarch64)
* `g++` 13+ with C++17
* `make`
* POSIX threads
* Optional: `valgrind` 3.20+

**Commands**

```bash
make              # build production binary + all test binaries
make firmware     # production binary only
make tests        # test binaries only
make clean        # remove build/
make help         # list targets
```

The production binary is written to `build/firmware`.

---

## Running the firmware

```bash
./build/firmware
```

Current behavior:

```text
Construct → Initialize → Run loop → Shutdown → exit
```

The application initializes Logger and SignalHandler, enters its run
loop, and exits cleanly on Ctrl+C or SIGTERM.

As Phase 2 and 3 components land, the startup and shutdown sequence
will become progressively richer.

---

## Testing

```bash
make run-tests    # build + run all tests; output shown only on failure
make valgrind     # Memcheck + Helgrind on the five key test binaries
```

Every component has a standalone test binary with its own main(),
so failures are isolated and reproducible.

| Component     | Test binary                   | Coverage                                  |
| ------------- | ----------------------------- | ----------------------------------------- |
| Logger        | `test_logger`                 | Lifecycle, level dispatch, output         |
| Logger        | `test_logger_shutdown`        | Shutdown sequencing, re-init, concurrency |
| Logger        | `test_logger_thread`          | 4 threads × 100 messages                  |
| Logger        | `test_logger_config`          | Configuration, filtering, output routing  |
| SignalHandler | `test_signal_handler`         | SIGINT/SIGTERM, restore, RAII             |
| ThreadManager | `test_thread_manager`         | Lifecycle and concurrency checks          |
| ThreadManager | `test_thread_manager_stress`  | 1000-iteration stress across 4 scenarios  |
| ThreadManager | `test_thread_manager_timeout` | Join(timeout), naming, truncation         |
| Queue         | `test_queue`                  | 81 checks: FIFO, capacity, shutdown modes |
| Queue         | `test_queue_timeout`          | 33 checks: PushFor / PopFor               |
| FirmwareApp   | `test_firmware_app`           | State machine, idempotency, RAII, threads |

Valgrind status: all completed components are clean under both
Memcheck (`--leak-check=full`) and Helgrind (`--tool=helgrind`).

Where C++11 `std::atomic` performs lock-free handoff between threads,
the code uses explicit annotations (`ANNOTATE_HAPPENS_BEFORE` /
`ANNOTATE_HAPPENS_AFTER`) rather than suppressions. These are
no-ops in production builds and live in
`framework/util/helgrind_annotations.h`.

The 1000-iteration ThreadManager stress test runs natively.
A reduced-iteration variant (`-DSTRESS_ITERATIONS=20`) runs under
Helgrind for bounded analysis runtime.

---

## Component overview

### Logger — framework/logger/

Single shared logging facility. Thread-safe via one internal mutex.

* Four levels: DEBUG, INFO, WARN, ERROR
* Millisecond timestamps, kernel thread ID (matches top -H)
* Runtime minimum-level filter
* Output stream selectable: stdout (program output) or stderr
* Configurable via LoggerConfig
* IsInitialized, SetMinLevel, GetMinLevel accessors

### SignalHandler — framework/signal/

Process-wide SIGINT / SIGTERM handling for cooperative shutdown.

* Installs handlers via sigaction(2)
* Captures and restores previous handlers — safe when composed with
  other signal-handling subsystems
* Explicit State { NOT_INSTALLED, INSTALLED }
* RAII: destructor calls Shutdown() if still installed
* Double Initialize() rejected; Shutdown() without Initialize()
  is a safe no-op

### ThreadManager — framework/thread/

Lifecycle management for a single worker thread.

* Start / Stop / Join / Join(timeout)
* Workers receive a StopToken& and exit cooperatively — no forced
  thread termination
* Named threads via pthread_setname_np, visible in top -H,
  htop, gdb info threads
* Join(timeout) returns JOIN_TIMEOUT if the worker ignores its
  stop request; caller decides what to do next
* Destructor emits a diagnostic on stderr and calls
  std::terminate() if a worker is still running — ownership is
  explicit, not silently hidden by a blocking destructor

### Queue — framework/queue/

Bounded, thread-safe FIFO for producer/consumer decoupling.

* Push (non-blocking) / PushFor (timed)
* Pop (blocking) / PopFor (timed)
* Two shutdown modes:

  * IMMEDIATE — discard queued items, wake consumers
  * DRAIN — stop accepting producers, finish existing items,
    become terminal when empty
* Move-only payloads (Push(T&&))
* Internal state machine: RUNNING → DRAINING → SHUTDOWN
* Two condition variables (not_empty, not_full); producers and
  consumers do not contend on the same CV

### FirmwareApp — app/

Application lifecycle entry point. main.cpp is deliberately tiny.

* Explicit state machine:
  UNINITIALIZED → INITIALIZED → RUNNING → SHUTDOWN
* Initialize() rejects double-init and init-after-shutdown; failures
  roll back cleanly
* Run() blocks until a signal or RequestShutdown()
* Shutdown() idempotent, transition-safe (state.exchange), tears
  down subsystems in reverse order of initialization
* Destructor is a safety net for the "forgot to call Shutdown()" case

---

## Design philosophy

* **Cooperative cancellation.** No thread is forcibly killed. Workers
  observe a StopToken and exit on their own terms.

* **Explicit ownership.** A service owns its ThreadManager and its
  Queue. Shutdown order is visible in code, not implied by
  destruction order.

* **Failure is a first-class concern.** Every component has an
  error enum. Every error path is tested.

* **Framework stays generic.** No framework component knows about
  domain concepts (camera, MQTT, RTSP).

* **Tests precede confidence.** A component is not "done" until it
  survives functional, lifecycle, concurrency, stress, Memcheck and
  Helgrind checks.

* **Honest concurrency analysis.** Where C++11 atomics perform
  lock-free handoff, we annotate explicitly rather than suppress.

* **No hidden blocking in destructors.** std::terminate() is
  preferred over silently joining a thread whose stop request was
  ignored.

---

## Development conventions

* **Language:** C++17 (`-std=c++17`)
* **Warnings:** `-Wall -Wextra -Werror` — a clean build has zero warnings
* **Threading:** `std::thread`; raw pthreads only where the platform
  API requires it (thread naming)
* **Tests:** one standalone binary per component, own main()
* **Valgrind:** Memcheck and Helgrind clean before commit
* **Commits:** one logical change per commit; message explains why,
  not just what
* **README:** updated in the same push as any change that adds or
  removes a top-level component, or changes the build/test commands

---

## Repository layout

```text
Linux-IP-Camera-Firmware/
├── app/                Application layer
├── framework/          Generic framework components
│   ├── logger/
│   ├── signal/
│   ├── thread/
│   ├── queue/
│   ├── timer/          (empty — Phase 2)
│   ├── event/          (empty — Phase 2)
│   ├── ipc/            (empty — Phase 2)
│   ├── watchdog/       (empty — Phase 2)
│   └── util/           Helgrind annotations
├── services/           Domain services (empty — Phase 3)
├── platform/linux/     Platform-specific code (empty)
├── configs/            Runtime configuration (empty)
├── scripts/            Utility scripts (empty)
├── tests/              One directory per component
├── docs/               Design documents and roadmap
├── Makefile
├── .gitignore
└── README.md
```

---

### Phase 1 — Foundation

**Status: COMPLETE**

* Logger
* SignalHandler
* ThreadManager
* Queue
* FirmwareApp

### Phase 2 — Core Runtime

**Status: NEXT**

* TimerScheduler
* Event
* IPC
* Watchdog

### Phase 3 — Services

* Config
* Network
* MQTT
* Camera
* Motion
* RTSP

### Phase 4 — Security

* Crypto
* Secure storage
* Firmware verify
* Secure Boot model
* Anti-rollback

### Phase 5 — OTA

* Download
* Verification
* A/B update
* Rollback

### Phase 6 — Integration

* Lifecycle wiring
* Failure injection
* Soak testing
* Documentation

---

## License

This project is currently under development.

```
```
