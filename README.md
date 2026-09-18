# Linux IP Camera Firmware

A production-oriented **Linux IP camera firmware architecture** implemented from scratch in **C++17**, structured the way real embedded Linux products are structured — not as a collection of small demos, but as a single coherent firmware system.

> **Status:** Phase 1 — Foundation **COMPLETE**

> **Next:** Phase 2 — Core Runtime

---

## Table of Contents

* [What This Project Is](#what-this-project-is)
* [What This Project Is Not](#what-this-project-is-not)
* [Goals](#goals)
* [Current Status](#current-status)
* [Architecture](#architecture)
* [Building](#building)
* [Running the Firmware](#running-the-firmware)
* [Testing](#testing)
* [Component Overview](#component-overview)

  * [Logger](#logger)
  * [SignalHandler](#signalhandler)
  * [ThreadManager](#threadmanager)
  * [Queue](#queue)
  * [FirmwareApp](#firmwareapp)
* [Roadmap](#roadmap)
* [Design Philosophy](#design-philosophy)
* [Development Conventions](#development-conventions)
* [Repository Layout](#repository-layout)
* [License](#license)

---

# What This Project Is

This project is a single embedded Linux firmware application that will eventually provide the core functionality expected from a production IP camera.

The system is being developed incrementally, component by component, with an emphasis on **correctness, ownership, concurrency, lifecycle management, failure handling, and testability**.

The final firmware is intended to support:

* Video capture using Linux **V4L2**
* H.264/H.265 video encoding
* Live video streaming through **RTSP**
* Telemetry and device commands through **MQTT**
* TLS-secured network communication
* Signed firmware updates
* OTA firmware updates
* Anti-rollback protection
* Secure Boot trust-chain modeling
* Secure firmware verification
* Graceful recovery from:

  * Camera failures
  * Network failures
  * Disk/storage failures
  * Thread failures
  * Service failures

The project is deliberately being built as a **single coherent firmware architecture**, rather than as independent demonstrations of individual technologies.

---

# What This Project Is Not

### Not a tutorial

The code does not sacrifice correctness for brevity.

Where a design decision introduces complexity, the reason for that complexity is documented through:

* Code structure
* Comments
* Tests
* Commit history
* Design documentation

### Not bare-metal firmware

This project targets **embedded Linux**, not a bare-metal MCU or RTOS environment.

The implementation therefore uses Linux/POSIX facilities such as:

* `std::thread`
* pthread APIs where required by the platform
* V4L2
* Unix sockets
* TCP/IP sockets
* Signals
* File descriptors
* Timers
* Linux device interfaces

### Not finished

The project is being developed in multiple phases.

Anything marked `[ ]` in the roadmap is currently unimplemented.

The roadmap represents a multi-month engineering project rather than a collection of isolated coding exercises.

---

# Goals

## Engineering Goals

The primary goal is to build an architecture that resembles a real embedded Linux product.

The project focuses on:

1. Building a production-oriented firmware architecture.
2. Understanding **why** each abstraction exists.
3. Clearly defining ownership between components.
4. Designing explicit lifecycle states.
5. Handling failures deliberately.
6. Building safe concurrent components.
7. Testing every important error path.
8. Detecting memory errors and data races.
9. Building the complete system incrementally.

### Production-quality targets

The project aims to achieve production-grade behavior in:

* Concurrency

  * Race conditions
  * Deadlocks
  * Wake-ups
  * Cancellation
  * Thread lifecycle
* Lifecycle management

  * Initialization order
  * Shutdown order
  * Idempotency
  * RAII
* Failure handling

  * Explicit error states
  * Error propagation
  * Recovery paths
  * Failure injection
* Memory safety

  * Leak detection
  * Invalid access detection
  * Resource ownership
* Observability

  * Structured logging
  * Thread names
  * Diagnostics
* Testing

  * Functional tests
  * Lifecycle tests
  * Concurrency tests
  * Stress tests
  * Memcheck
  * Helgrind

The project is intended to work with the standard Linux development and debugging toolchain:

```text
g++
make
gdb
valgrind
strace
perf
htop
```

---

## Career Goals

This project is also intended to demonstrate practical embedded Linux engineering capability for roles involving:

* Embedded Linux
* C/C++
* Camera firmware
* Linux systems programming
* Networking
* Multithreading
* Security
* OTA systems
* Device firmware
* Systems engineering

The target is not simply to have a working repository.

The repository should be understandable to a senior engineer and defensible in an interview.

For every major component, the project should be able to answer:

> Why does this component exist?

> Who owns it?

> What happens when it fails?

> How does it shut down?

> What happens when multiple threads access it?

> How was its correctness verified?

---

# Current Status

## Phase 1 — Foundation

**Status: COMPLETE**

| Component     | Status | Purpose                             |
| ------------- | -----: | ----------------------------------- |
| Logger        | ✅ Done | Thread-safe logging infrastructure  |
| SignalHandler | ✅ Done | Cooperative process shutdown        |
| ThreadManager | ✅ Done | Worker thread lifecycle management  |
| Queue         | ✅ Done | Bounded producer/consumer queue     |
| FirmwareApp   | ✅ Done | Application lifecycle/state machine |

---

## Phase 2 — Core Runtime

**Status: NEXT**

| Component      | Status | Purpose                                   |
| -------------- | -----: | ----------------------------------------- |
| TimerScheduler |      ⬜ | Shared timer/scheduler infrastructure     |
| Event          |      ⬜ | In-process publish/subscribe event system |
| IPC            |      ⬜ | Inter-process communication               |
| Watchdog       |      ⬜ | Linux kernel watchdog integration         |

---

## Phase 3 — Services

| Component | Status |
| --------- | -----: |
| Config    |      ⬜ |
| Network   |      ⬜ |
| MQTT      |      ⬜ |
| Camera    |      ⬜ |
| Motion    |      ⬜ |
| RTSP      |      ⬜ |

---

## Phase 4 — Security

| Component             | Status |
| --------------------- | -----: |
| Crypto                |      ⬜ |
| Secure Storage        |      ⬜ |
| Firmware Verification |      ⬜ |
| Secure Boot Model     |      ⬜ |
| Anti-Rollback         |      ⬜ |

---

## Phase 5 — OTA

| Component             | Status |
| --------------------- | -----: |
| Firmware Download     |      ⬜ |
| Firmware Verification |      ⬜ |
| A/B Update            |      ⬜ |
| Automatic Rollback    |      ⬜ |

---

## Phase 6 — Integration

| Component                       | Status |
| ------------------------------- | -----: |
| Lifecycle Wiring                |      ⬜ |
| Failure Injection               |      ⬜ |
| Soak Testing                    |      ⬜ |
| Full Architecture Documentation |      ⬜ |

---

# Architecture

The architecture follows a strict **downward dependency direction**.

Higher layers may depend on lower layers, but lower layers must never depend on higher-level domain concepts.

```text
┌─────────────────────────────────────────────┐
│                  Application                │
│                                             │
│       main.cpp · FirmwareApp                │
└───────────────────────┬─────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────┐
│                  Services                   │
│                                             │
│ Camera · Network · MQTT · RTSP              │
│ OTA · Motion · Config · Security             │
└───────────────────────┬─────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────┐
│                  Framework                  │
│                                             │
│ Logger · ThreadManager · Queue              │
│ Timer · Event · IPC · Watchdog              │
└───────────────────────┬─────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────┐
│              Platform — Linux              │
│                                             │
│ File Descriptors · Sockets · Signals        │
│ Threads · Timers · Linux Device APIs        │
└───────────────────────┬─────────────────────┘
                        │
                        ▼
                    ┌───────┐
                    │ Linux │
                    └───────┘
```

## Dependency Rule

The fundamental architecture rule is:

> **Framework components are domain-agnostic.**

For example:

```text
Queue
  └── knows how to store and synchronize objects

Queue
  └── does NOT know about MQTT

TimerScheduler
  └── knows how to schedule callbacks

TimerScheduler
  └── does NOT know about camera frames

Network
  └── knows about networking

Network
  └── does NOT define application lifecycle
```

Any behavior specific to a domain belongs inside the corresponding service.

This keeps the framework reusable and prevents domain logic from leaking into infrastructure components.

---

# Building

## Requirements

The project currently requires:

* Linux
* x86_64 or AArch64
* `g++` 13 or newer
* C++17 support
* `make`
* POSIX threads
* Optional: Valgrind 3.20+

Verify the compiler:

```bash
g++ --version
```

Verify Make:

```bash
make --version
```

Verify Valgrind:

```bash
valgrind --version
```

---

## Build Everything

Build the production firmware and all test binaries:

```bash
make
```

---

## Build Firmware Only

```bash
make firmware
```

The production binary is generated at:

```text
build/firmware
```

---

## Build Tests Only

```bash
make tests
```

---

## Clean Build Artifacts

```bash
make clean
```

---

## Show Available Make Targets

```bash
make help
```

---

# Running the Firmware

After building:

```bash
./build/firmware
```

The current application performs the basic application lifecycle:

```text
Construct
    │
    ▼
Initialize
    │
    ▼
Run
    │
    ├── SIGINT / SIGTERM
    │
    └── RequestShutdown()
    │
    ▼
Shutdown
```

At the current stage, the firmware initializes the foundation components and enters its run loop.

The application exits cleanly when:

```text
Ctrl+C
```

or:

```text
SIGTERM
```

is received.

As additional runtime components and services are implemented, the startup and shutdown sequence will become progressively richer.

---

# Testing

Testing is treated as a first-class part of the architecture.

A component is not considered complete merely because its normal execution path works.

The intended verification process includes:

```text
Functional Testing
        │
        ▼
Lifecycle Testing
        │
        ▼
Error-path Testing
        │
        ▼
Concurrency Testing
        │
        ▼
Stress Testing
        │
        ▼
Memcheck
        │
        ▼
Helgrind
```

---

## Running All Tests

Build and run the complete test suite:

```bash
make run-tests
```

The test system is configured so that normal successful output is minimized and failures remain visible.

---

## Valgrind

Run memory and concurrency analysis:

```bash
make valgrind
```

The Valgrind target runs:

* Memcheck
* Helgrind

against the key test binaries.

---

# Test Suite

Every major component has a standalone test binary with its own `main()`.

This keeps failures:

* Isolated
* Reproducible
* Easy to debug
* Easy to execute independently

| Component     | Test Binary                   | Coverage                                       |
| ------------- | ----------------------------- | ---------------------------------------------- |
| Logger        | `test_logger`                 | Lifecycle, level dispatch, output              |
| Logger        | `test_logger_shutdown`        | Shutdown sequencing, re-init, concurrency      |
| Logger        | `test_logger_thread`          | 4 threads × 100 messages                       |
| Logger        | `test_logger_config`          | Configuration, level filtering, output routing |
| SignalHandler | `test_signal_handler`         | SIGINT/SIGTERM, restore, RAII                  |
| ThreadManager | `test_thread_manager`         | Lifecycle and concurrency checks               |
| ThreadManager | `test_thread_manager_stress`  | 1000-iteration stress testing                  |
| ThreadManager | `test_thread_manager_timeout` | Join timeout, naming, truncation               |
| Queue         | `test_queue`                  | 81 checks: FIFO, capacity, shutdown            |
| Queue         | `test_queue_timeout`          | 33 checks: `PushFor` / `PopFor`                |
| FirmwareApp   | `test_firmware_app`           | State machine, idempotency, RAII, threads      |

---

# Valgrind Status

The completed Phase 1 components have been validated under:

## Memcheck

Configuration:

```text
--leak-check=full
--error-exitcode=1
```

Current result:

```text
0 bytes in 0 blocks
0 errors
```

---

## Helgrind

Configuration:

```text
--tool=helgrind
--error-exitcode=1
```

Current result:

```text
0 errors from 0 contexts
```

---

## Helgrind Annotations

Some C++11 `std::atomic` operations are used for lock-free synchronization and handoff between threads.

Where required, the project uses explicit Helgrind annotations:

```cpp
ANNOTATE_HAPPENS_BEFORE(...)
ANNOTATE_HAPPENS_AFTER(...)
```

These annotations are contained in:

```text
framework/util/helgrind_annotations.h
```

They are:

* Active only when Valgrind/Helgrind support is available
* No-ops in normal production builds
* Documented rather than hidden through suppression files

The intention is to make concurrency assumptions explicit instead of simply suppressing race reports.

---

## Stress Testing

The native ThreadManager stress test performs:

```text
1000 iterations
4 concurrent scenarios
```

A reduced iteration count is used for Helgrind execution to keep race-detection runtime bounded:

```text
-DSTRESS_ITERATIONS=20
```

The native stress test therefore provides high-volume execution coverage, while the reduced Helgrind variant provides practical race analysis.

---

# Component Overview

# Logger

Location:

```text
framework/logger/
```

The Logger is the shared logging facility used by the firmware.

It provides thread-safe logging through an internal synchronization mechanism.

## Features

### Log levels

```text
DEBUG
INFO
WARN
ERROR
```

### Timestamps

Logs contain millisecond-resolution timestamps.

### Thread identification

The logger records the Linux kernel thread ID.

This allows logs to be correlated with tools such as:

```bash
top -H
htop
gdb
```

### Runtime log filtering

The minimum logging level can be configured at runtime.

This allows the same binary to be used in development and production environments with different verbosity levels.

### Output routing

The logger supports configurable output streams:

```text
stdout
stderr
```

`stderr` can be used for integration with service managers such as systemd/journald.

### Configuration

The logger uses a dedicated configuration structure:

```text
LoggerConfig
```

The public API is designed to remain additive and stable as the project evolves.

### Lifecycle

The logger exposes lifecycle-aware behavior including:

```text
Initialize()
Shutdown()
IsInitialized()
```

and rejects invalid lifecycle operations using explicit error codes.

---

# SignalHandler

Location:

```text
framework/signal/
```

The SignalHandler provides process-wide handling of:

```text
SIGINT
SIGTERM
```

Its primary purpose is **cooperative shutdown**.

Instead of allowing signals to terminate the process immediately, the signal handler communicates the shutdown request to the application.

## Features

* Uses `sigaction(2)`
* Installs SIGINT/SIGTERM handlers
* Stores previous signal handlers
* Restores previous handlers during shutdown
* Explicit lifecycle state
* RAII support

State:

```text
NOT_INSTALLED
INSTALLED
```

Invalid lifecycle operations are handled explicitly.

For example:

```text
Initialize()
Initialize()    → rejected
Shutdown()
Shutdown()     → safe no-op
```

The destructor automatically calls `Shutdown()` if the handler remains installed.

---

# ThreadManager

Location:

```text
framework/thread/
```

ThreadManager provides lifecycle management for individual worker threads.

The design emphasizes **cooperative cancellation**.

## Lifecycle

The primary operations are:

```text
Start()
Stop()
Join()
Join(timeout)
```

Workers receive a stop token and are expected to observe it and terminate themselves.

The project deliberately avoids forced thread termination.

---

## Cooperative Cancellation

The intended worker lifecycle is:

```text
Start
  │
  ▼
Running
  │
  │ Stop requested
  ▼
Worker observes StopToken
  │
  ▼
Worker exits
  │
  ▼
Join
```

This avoids unsafe forced termination of threads that may currently own:

* Mutexes
* File descriptors
* Memory
* Queues
* Other resources

---

## Thread Naming

Threads can be named using:

```text
pthread_setname_np()
```

This makes worker threads easier to identify in:

```text
top -H
htop
gdb
```

and during system debugging.

---

## Timed Join

The ThreadManager supports:

```text
Join(timeout)
```

If the worker does not terminate within the requested timeout:

```text
JOIN_TIMEOUT
```

is returned.

The caller is then responsible for deciding what to do next.

This avoids hiding a potentially stuck worker inside a blocking destructor.

---

## Destructor Behavior

The ThreadManager does **not** silently block forever in its destructor.

If a worker remains active when the ThreadManager is destroyed, the implementation emits a diagnostic and terminates the process.

The principle is:

> **Thread ownership must be explicit.**

A forgotten running thread should be treated as a programming/lifecycle error rather than silently hidden by a blocking destructor.

---

# Queue

Location:

```text
framework/queue/
```

Queue is a bounded, thread-safe FIFO used to decouple producers and consumers.

It is designed as generic framework infrastructure and has no knowledge of services such as:

* Camera
* MQTT
* RTSP
* Network

---

## Operations

### Producer operations

```text
Push()
PushFor()
```

`Push()` provides non-blocking insertion.

`PushFor()` provides timed insertion.

### Consumer operations

```text
Pop()
PopFor()
```

`Pop()` waits until an item becomes available or the queue reaches a terminal state.

`PopFor()` provides timed waiting.

---

## Move-only Payloads

The queue supports move-only objects through:

```cpp
Push(T&&)
```

This allows efficient transfer of ownership without unnecessary copies.

---

# Queue Shutdown

The queue supports two shutdown modes.

## IMMEDIATE

```text
IMMEDIATE
```

Behavior:

```text
Discard queued items
        │
        ▼
Wake waiting consumers
        │
        ▼
Terminal shutdown
```

This is useful when pending work is no longer relevant.

---

## DRAIN

```text
DRAIN
```

Behavior:

```text
Stop accepting producers
        │
        ▼
Process existing items
        │
        ▼
Queue becomes empty
        │
        ▼
Terminal shutdown
```

This is useful when queued work must be completed before shutdown.

---

## Queue State Machine

```text
             ┌──────────────┐
             │    RUNNING   │
             └──────┬───────┘
                    │
             Shutdown(DRAIN)
                    │
                    ▼
             ┌──────────────┐
             │   DRAINING   │
             └──────┬───────┘
                    │
               Queue empty
                    │
                    ▼
             ┌──────────────┐
             │   SHUTDOWN   │
             └──────────────┘
```

Immediate shutdown transitions directly toward the terminal state while discarding pending items.

---

## Condition Variables

The Queue uses separate condition variables for:

```text
not_empty
not_full
```

This allows producer and consumer wake-ups to be managed independently and avoids unnecessary contention between the two classes of operations.

---

# FirmwareApp

Location:

```text
app/
```

FirmwareApp is the main application lifecycle controller.

The `main.cpp` file is intentionally small.

The intended application structure is:

```cpp
FirmwareApp app;

app.Initialize();
app.Run();
app.Shutdown();
```

The application itself owns the lifecycle of the firmware subsystems.

---

## State Machine

FirmwareApp uses an explicit state machine:

```text
UNINITIALIZED
      │
      │ Initialize()
      ▼
INITIALIZED
      │
      │ Run()
      ▼
RUNNING
      │
      │ Shutdown()
      ▼
SHUTDOWN
```

Invalid transitions are rejected.

For example:

```text
Initialize()
Initialize()       → rejected

Shutdown()
Shutdown()         → safe/idempotent

Initialize()
Shutdown()
Initialize()       → rejected
```

---

## Shutdown

The application can receive a shutdown request through:

```text
RequestShutdown()
```

The main run loop then exits cooperatively.

Shutdown operations are designed to be:

* Idempotent
* Transition-safe
* Explicit
* Ordered

Subsystems are destroyed/shut down in reverse order of initialization.

---

## Failure Rollback

If initialization of a subsystem fails, previously initialized components are rolled back cleanly.

Conceptually:

```text
Initialize A
      │
      ▼
Initialize B
      │
      ▼
Initialize C
      │
      X Failure
      │
      ▼
Shutdown B
      │
      ▼
Shutdown A
```

This prevents partially initialized firmware from continuing into the run state.

---

# Roadmap

## Phase 1 — Foundation

**Status: COMPLETE**

Implemented:

* Logger
* SignalHandler
* ThreadManager
* Queue
* FirmwareApp

Focus:

```text
Lifecycle
Concurrency
Ownership
Shutdown
Testing
Memory Safety
Race Detection
```

---

# Phase 2 — Core Runtime

**Status: NEXT**

## TimerScheduler

A shared timer scheduler providing:

* One-shot timers
* Periodic timers
* Fixed-rate execution
* Fixed-delay execution
* Cooperative cancellation
* Shared scheduler thread

The scheduler will remain generic and independent of application domains.

---

## Event

An in-process synchronous event bus for communication between services.

Potential uses include:

```text
Network connected
Network disconnected
Camera failure
OTA available
OTA verification failed
Shutdown requested
Configuration changed
```

The Event framework will remain domain-agnostic.

---

## IPC

Inter-process communication infrastructure.

Potential mechanisms include:

```text
Unix domain sockets
Shared memory
```

The exact mechanism will depend on the communication requirements of the service.

---

## Watchdog

Linux kernel watchdog integration through:

```text
/dev/watchdog
```

The watchdog architecture will introduce service-level health/kick discipline.

The goal is not simply to periodically kick the watchdog, but to ensure that the system only reports itself healthy when the required services are actually functioning.

---

# Phase 3 — Services

## Config

Responsibilities:

* Configuration loading
* Configuration validation
* Default values
* Runtime configuration management

---

## Network

Responsibilities:

* TCP communication
* TLS
* Connection management
* Reconnection
* Exponential backoff
* Network state reporting

---

## MQTT

Responsibilities:

* MQTT connection
* Topic management
* QoS
* TLS
* Persistent messaging where required
* Command reception
* Telemetry publishing

---

## Camera

Responsibilities:

* V4L2 device discovery
* Camera initialization
* Format negotiation
* Frame capture
* Buffer management
* Capture thread
* Camera failure detection
* Camera recovery

---

## Motion

Responsibilities:

* Frame-difference processing
* Decimated processing stream
* Motion event generation

The initial implementation is intended to remain lightweight and architecture-focused rather than attempting to reproduce a production computer-vision stack.

---

## RTSP

Responsibilities:

* RTSP server
* Client session management
* RTP streaming
* Camera frame integration

The initial protocol implementation will cover:

```text
OPTIONS
DESCRIBE
SETUP
PLAY
TEARDOWN
```

with the architecture designed around standard RTSP/RTP behavior.

---

# Phase 4 — Security

## Crypto

A generic cryptographic abstraction for operations such as:

* Hashing
* Signature verification
* Cryptographic key handling
* Secure storage integration

---

## Secure Storage

Abstraction for storing security-sensitive material.

The implementation is intended to separate:

```text
Application
      │
      ▼
Secure Storage API
      │
      ▼
Platform-specific secure storage
```

This allows platform-specific storage mechanisms to remain outside the application logic.

---

## Firmware Verification

Firmware images will be verified using:

```text
Image Hash
     +
Digital Signature
     =
Verified Firmware
```

Verification must occur before an image is accepted for installation.

---

## Secure Boot Model

The project will model a secure boot trust chain:

```text
Boot ROM
   │
   ▼
Bootloader
   │
   ▼
Kernel / Firmware Image
   │
   ▼
Application
```

Each stage establishes trust in the next stage.

The implementation will focus on understanding and demonstrating the architecture rather than claiming that a software model alone provides hardware-rooted secure boot.

---

## Anti-Rollback

The firmware will enforce monotonic firmware versions.

Conceptually:

```text
Current Version = 5

Accept:
Version 6
Version 7

Reject:
Version 4
Version 3
```

The implementation will model a hardware-backed monotonic counter where appropriate.

---

# Phase 5 — OTA

## Firmware Download

Responsibilities:

* Secure firmware download
* Download integrity
* Resume/retry behavior
* Storage management

---

## Verification

Downloaded firmware must pass:

```text
Integrity Check
        +
Signature Verification
        +
Version Check
```

before installation.

---

## A/B Update

The firmware will use an A/B partition model:

```text
┌─────────────┐
│ Partition A │ ← Currently running
└─────────────┘

┌─────────────┐
│ Partition B │ ← New firmware
└─────────────┘
```

A new firmware version can be written to the inactive partition without destroying the currently running image.

---

## Automatic Rollback

The boot process will track firmware health.

Conceptually:

```text
Install New Firmware
        │
        ▼
Boot New Version
        │
        ▼
Health Check
     /     \
   PASS    FAIL
    │        │
    ▼        ▼
 Commit    Rollback
```

---

# Phase 6 — Integration

The final phase will integrate all framework components and services.

Planned work:

### Lifecycle wiring

Connect all services to the FirmwareApp lifecycle.

```text
Initialize
    │
    ├── Config
    ├── Network
    ├── Camera
    ├── MQTT
    ├── RTSP
    ├── Motion
    └── OTA
```

Shutdown occurs in reverse dependency order.

---

## Failure Injection

The system will deliberately simulate failures such as:

```text
Thread failure
Network disconnect
Camera disconnect
Disk failure
MQTT disconnect
Corrupted firmware image
Invalid signature
Failed boot
Service timeout
```

The purpose is to verify recovery behavior rather than only testing the happy path.

---

## Soak Testing

Long-running tests will be used to identify:

* Memory leaks
* Resource leaks
* Thread leaks
* Deadlocks
* Queue starvation
* Reconnection problems
* Timer problems
* Gradual performance degradation

---

## Documentation

The final architecture documentation will describe:

* Component responsibilities
* Dependency relationships
* Thread ownership
* Queue ownership
* Lifecycle transitions
* Failure handling
* Security architecture
* OTA architecture
* Boot flow
* Recovery mechanisms

---

# Design Philosophy

## 1. Cooperative Cancellation

Threads are never forcibly killed.

Workers observe a stop request and terminate themselves.

```text
Stop Request
     │
     ▼
StopToken
     │
     ▼
Worker observes request
     │
     ▼
Worker exits safely
```

This prevents resources from being abandoned while a thread is terminated.

---

## 2. Explicit Ownership

Ownership must be visible in the architecture.

For example:

```text
FirmwareApp
    │
    ├── owns Logger
    ├── owns SignalHandler
    ├── owns ThreadManager
    └── owns Services
```

A service should clearly own the framework resources it depends on.

Shutdown order should be visible in code rather than being an accidental consequence of object destruction order.

---

## 3. Failure Is a First-Class Concern

Components expose explicit error states.

The design does not assume:

```text
Initialize() → always succeeds
```

Instead:

```text
Initialize()
     │
 ┌───┴────┐
 ▼        ▼
SUCCESS  FAILURE
          │
          ▼
       Recovery /
       Rollback
```

Error paths are part of the normal design process.

---

## 4. Framework Stays Generic

Framework components do not know about domain concepts.

For example:

```text
Queue
```

must not contain:

```cpp
if (mqttMessage)
```

or:

```cpp
if (cameraFrame)
```

Instead, those concepts belong to:

```text
services/mqtt/
services/camera/
```

This keeps framework code reusable.

---

## 5. Tests Precede Confidence

A component is not considered complete simply because it compiles.

The intended definition of "done" is:

```text
Functional Tests
      +
Lifecycle Tests
      +
Error-path Tests
      +
Concurrency Tests
      +
Stress Tests
      +
Memcheck
      +
Helgrind
```

Only then should a component be considered ready for integration.

---

## 6. Honest Concurrency Analysis

Helgrind annotations are used when necessary to document synchronization relationships.

The project avoids using suppressions simply to hide race reports.

The intention is:

> If the synchronization is intentional, document it.

> If the synchronization is wrong, fix it.

The annotations are inert in production builds.

---

## 7. No Hidden Blocking in Destructors

Destructors should not silently wait forever for a worker thread that ignored its stop request.

Instead, ownership violations should be visible.

This makes lifecycle problems easier to detect during development and testing.

---

# Development Conventions

## Language

```text
C++17
```

Build flag:

```bash
-std=c++17
```

---

## Compiler Warnings

The project treats warnings as errors:

```bash
-Wall
-Wextra
-Werror
```

A clean build therefore requires:

```text
0 warnings
```

---

## Threading

Primary threading abstraction:

```cpp
std::thread
```

pthread APIs are used only when Linux-specific functionality requires them, such as thread naming.

---

## Testing

Every major component has a standalone test binary with its own `main()`.

Benefits:

* Isolated failures
* Easy debugging
* Independent execution
* Simple Valgrind integration

---

## Memory Analysis

Every completed component is expected to pass:

```text
Valgrind Memcheck
```

with no memory errors or leaks.

---

## Race Detection

Every completed concurrent component is expected to be analyzed using:

```text
Valgrind Helgrind
```

Where annotations are required, they must be documented.

---

## Commits

Commits should represent **one logical change**.

Commit messages should explain:

```text
Why the change exists
```

rather than merely:

```text
What files changed
```

Example:

```text
Improve Queue shutdown semantics
```

is preferable to:

```text
Updated queue.cpp
```

---

## README Maintenance

The README should be updated in the same change whenever:

* A top-level component is added
* A top-level component is removed
* Build commands change
* Test commands change
* Architecture changes
* Project status changes

This keeps the repository documentation synchronized with the actual implementation.

---

# Repository Layout

```text
Linux-IP-Camera-Firmware/
│
├── app/
│   ├── main.cpp
│   ├── firmware_app.h
│   └── firmware_app.cpp
│
├── framework/
│   ├── logger/
│   │   ├── logger.h
│   │   └── logger.cpp
│   │
│   ├── signal/
│   │   ├── signal_handler.h
│   │   └── signal_handler.cpp
│   │
│   ├── thread/
│   │   ├── thread_manager.h
│   │   └── thread_manager.cpp
│   │
│   ├── queue/
│   │   ├── queue.h
│   │   └── queue.tpp
│   │
│   ├── timer/
│   │   └── # Phase 2
│   │
│   ├── event/
│   │   └── # Phase 2
│   │
│   ├── ipc/
│   │   └── # Phase 2
│   │
│   ├── watchdog/
│   │   └── # Phase 2
│   │
│   └── util/
│       └── helgrind_annotations.h
│
├── services/
│   ├── camera/
│   ├── network/
│   ├── mqtt/
│   ├── rtsp/
│   ├── ota/
│   ├── security/
│   ├── motion/
│   └── config/
│
├── platform/
│   └── linux/
│       └── # Platform-specific wrappers
│
├── configs/
│   └── # Runtime configuration
│
├── scripts/
│   └── # Utility scripts
│
├── tests/
│   ├── logger/
│   ├── signal/
│   ├── thread/
│   ├── queue/
│   └── firmware_app/
│
├── docs/
│   └── # Design documents
│
├── build/
│   └── # Generated build artifacts
│
├── Makefile
├── .gitignore
└── README.md
```

---

# Current Implementation Summary

At the completion of **Phase 1 — Foundation**, the project has established the core runtime foundation required for the remaining firmware architecture.

The current foundation provides:

```text
┌──────────────────────────────────────────┐
│              FirmwareApp                 │
│                                          │
│     Application lifecycle/state          │
└────────────────────┬─────────────────────┘
                     │
          ┌──────────┼──────────┐
          │          │          │
          ▼          ▼          ▼
      Logger   SignalHandler  ThreadManager
                                │
                                ▼
                              Queue
```

These components establish the fundamental rules for:

* Logging
* Shutdown
* Thread ownership
* Cooperative cancellation
* Producer/consumer communication
* Lifecycle management
* Error handling
* Resource cleanup
* Concurrency testing

Phase 2 will build on this foundation by introducing:

```text
TimerScheduler
Event
IPC
Watchdog
```

After that, the framework will be used to implement the actual camera firmware services.

---

# Long-Term Architecture

The final system is intended to evolve toward:

```text
                         FirmwareApp
                              │
             ┌────────────────┼────────────────┐
             │                │                │
             ▼                ▼                ▼
          Config           Network           Camera
             │                │                │
             │                ▼                ▼
             │              MQTT            Motion
             │                │                │
             └────────────────┼────────────────┘
                              │
                              ▼
                            RTSP
                              │
                              ▼
                             OTA
                              │
                              ▼
                           Security
                              │
                              ▼
                         Secure Boot
```

All services will rely on the generic framework layer rather than implementing their own independent lifecycle, synchronization, timer, queue, logging, and shutdown mechanisms.

---

# Final Project Objective

The ultimate objective is to build a complete **production-oriented Linux IP camera firmware architecture** from the ground up.

The completed system should demonstrate practical understanding of:

```text
C++17
   │
   ├── Object ownership
   ├── RAII
   ├── Multithreading
   ├── Synchronization
   ├── IPC
   └── Error handling
        │
        ▼
Embedded Linux
   │
   ├── V4L2
   ├── Sockets
   ├── Signals
   ├── Timers
   ├── Device interfaces
   └── Linux debugging
        │
        ▼
Networking
   │
   ├── TCP
   ├── TLS
   ├── MQTT
   └── RTSP/RTP
        │
        ▼
Security
   │
   ├── Cryptography
   ├── Secure Storage
   ├── Secure Boot
   ├── Firmware Verification
   └── Anti-Rollback
        │
        ▼
OTA
   │
   ├── Signed Firmware
   ├── A/B Updates
   └── Automatic Rollback
        │
        ▼
Production Engineering
   │
   ├── Failure Injection
   ├── Stress Testing
   ├── Soak Testing
   ├── Memcheck
   └── Helgrind
```

The goal is not merely to make a camera application work.

The goal is to understand and implement the **system engineering behind production embedded Linux firmware**.

---

# License

This project is currently under development.

License information will be added when the project is formally released.
