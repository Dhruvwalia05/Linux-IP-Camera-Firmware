# ============================================================================
# Linux IP Camera Firmware — Build System
# ============================================================================
#
# Common usage:
#   make              Build firmware + all test binaries
#   make firmware     Build only the production firmware binary
#   make tests        Build only test binaries (no execution)
#   make run-tests    Build + run every component: custom tests → Valgrind
#   make clean        Remove build artifacts
#   make help         Show this message
#
# Per-component verification (build → custom tests → Memcheck → Helgrind):
#   make test-logger        4 Logger binaries
#   make test-signal        SignalHandler
#   make test-thread        ThreadManager (stress test runs custom-only)
#   make test-queue         Queue
#   make test-timer         TimerScheduler
#   make test-event         EventBus
#   make test-ipc           IPC
#   make test-watchdog      Watchdog
#   make test-config        Config
#   make test-network       Network
#   make test-firmware-app  FirmwareApp
#   make test-latest        Whatever ALIAS_LAST refers to below
#
# Each per-component target builds only what that component needs
# (including dependencies), runs the custom test binary natively, then
# runs Valgrind Memcheck and Helgrind on it. Nothing else is touched.
#
# ============================================================================

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -pthread -I.
LDFLAGS  := -pthread

BUILD_DIR      := build
SUPPRESSIONS   := framework/util/valgrind.supp

# Change this alias to the component you're currently working on.
# Then `make test-latest` runs the full verify cycle on just that component.
ALIAS_LAST := test-network

# ---------------------------------------------------------------------------
# Source groups (reused across targets)
# ---------------------------------------------------------------------------

LOGGER_SRC   := framework/logger/logger.cpp
SIGNAL_SRC   := framework/signal/signal_handler.cpp
THREAD_SRC   := framework/thread/thread_manager.cpp \
                framework/thread/stop_token.cpp
TIMER_SRC    := framework/timer/timer_scheduler.cpp
IPC_SRC      := framework/ipc/ipc_socket.cpp \
                framework/ipc/ipc_server.cpp \
                framework/ipc/ipc_client.cpp
WATCHDOG_SRC := framework/watchdog/watchdog.cpp

# --- Services ---
CONFIG_SRC   := services/config/config.cpp
NETWORK_SRC  := services/network/network.cpp

QUEUE_HEADERS := framework/queue/queue.h \
                 framework/queue/queue.tpp
EVENT_HEADERS := framework/event/event_bus.h

# ---------------------------------------------------------------------------
# Production binary
# ---------------------------------------------------------------------------

FIRMWARE_SRC := app/main.cpp \
                app/firmware_app.cpp \
                $(LOGGER_SRC) \
                $(SIGNAL_SRC)

FIRMWARE_BIN := $(BUILD_DIR)/firmware

# ---------------------------------------------------------------------------
# Test binary paths
# ---------------------------------------------------------------------------

TEST_LOGGER                 := $(BUILD_DIR)/test_logger
TEST_LOGGER_SHUTDOWN        := $(BUILD_DIR)/test_logger_shutdown
TEST_LOGGER_THREAD          := $(BUILD_DIR)/test_logger_thread
TEST_LOGGER_CONFIG          := $(BUILD_DIR)/test_logger_config
TEST_SIGNAL_HANDLER         := $(BUILD_DIR)/test_signal_handler
TEST_THREAD_MANAGER         := $(BUILD_DIR)/test_thread_manager
TEST_THREAD_MANAGER_STRESS  := $(BUILD_DIR)/test_thread_manager_stress
TEST_THREAD_MANAGER_SMALL   := $(BUILD_DIR)/test_thread_manager_stress_small
TEST_THREAD_MANAGER_TIMEOUT := $(BUILD_DIR)/test_thread_manager_timeout
TEST_QUEUE                  := $(BUILD_DIR)/test_queue
TEST_QUEUE_TIMEOUT          := $(BUILD_DIR)/test_queue_timeout
TEST_FIRMWARE_APP           := $(BUILD_DIR)/test_firmware_app
TEST_TIMER_SCHEDULER        := $(BUILD_DIR)/test_timer_scheduler
TEST_EVENT_BUS              := $(BUILD_DIR)/test_event_bus
TEST_IPC                    := $(BUILD_DIR)/test_ipc
TEST_WATCHDOG               := $(BUILD_DIR)/test_watchdog
TEST_CONFIG                 := $(BUILD_DIR)/test_config
TEST_NETWORK                := $(BUILD_DIR)/test_network

# Every test binary — used for `make tests` (build-all) only.
ALL_TESTS := \
    $(TEST_LOGGER) \
    $(TEST_LOGGER_SHUTDOWN) \
    $(TEST_LOGGER_THREAD) \
    $(TEST_LOGGER_CONFIG) \
    $(TEST_SIGNAL_HANDLER) \
    $(TEST_THREAD_MANAGER) \
    $(TEST_THREAD_MANAGER_STRESS) \
    $(TEST_THREAD_MANAGER_SMALL) \
    $(TEST_THREAD_MANAGER_TIMEOUT) \
    $(TEST_QUEUE) \
    $(TEST_QUEUE_TIMEOUT) \
    $(TEST_FIRMWARE_APP) \
    $(TEST_TIMER_SCHEDULER) \
    $(TEST_EVENT_BUS) \
    $(TEST_IPC) \
    $(TEST_WATCHDOG) \
    $(TEST_CONFIG) \
    $(TEST_NETWORK)

# ---------------------------------------------------------------------------
# Top-level targets
# ---------------------------------------------------------------------------

.PHONY: all firmware tests run-tests clean help test-latest \
        test-logger test-signal test-thread test-queue \
        test-timer test-event test-ipc test-watchdog test-config \
        test-network test-firmware-app

all: firmware tests

firmware: $(FIRMWARE_BIN)

tests: $(ALL_TESTS)

# ---------------------------------------------------------------------------
# Build directory
# ---------------------------------------------------------------------------

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# ---------------------------------------------------------------------------
# Production binary
# ---------------------------------------------------------------------------

$(FIRMWARE_BIN): $(FIRMWARE_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(FIRMWARE_SRC) $(LDFLAGS) -o $@

# ===========================================================================
# Test build rules
# ===========================================================================

# --- Logger ---
$(TEST_LOGGER): tests/logger/test_logger.cpp $(LOGGER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(TEST_LOGGER_SHUTDOWN): tests/logger/test_logger_shutdown.cpp $(LOGGER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(TEST_LOGGER_THREAD): tests/logger/test_logger_thread.cpp $(LOGGER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(TEST_LOGGER_CONFIG): tests/logger/test_logger_config.cpp $(LOGGER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

# --- SignalHandler ---
$(TEST_SIGNAL_HANDLER): tests/signal/test_signal_handler.cpp $(SIGNAL_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

# --- ThreadManager ---
$(TEST_THREAD_MANAGER): tests/thread/test_thread_manager.cpp $(THREAD_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(TEST_THREAD_MANAGER_STRESS): tests/thread/test_thread_manager_stress.cpp $(THREAD_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(TEST_THREAD_MANAGER_SMALL): tests/thread/test_thread_manager_stress.cpp $(THREAD_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -DSTRESS_ITERATIONS=20 -DCONCURRENT_THREADS=4 $^ $(LDFLAGS) -o $@

$(TEST_THREAD_MANAGER_TIMEOUT): tests/thread/test_thread_manager_timeout.cpp $(THREAD_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

# --- Queue (header-only) ---
$(TEST_QUEUE): tests/queue/test_queue.cpp $(QUEUE_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< $(LDFLAGS) -o $@

$(TEST_QUEUE_TIMEOUT): tests/queue/test_queue_timeout.cpp $(QUEUE_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< $(LDFLAGS) -o $@

# --- FirmwareApp (links Logger + SignalHandler) ---
$(TEST_FIRMWARE_APP): tests/firmware_app/test_firmware_app.cpp \
                      app/firmware_app.cpp \
                      $(LOGGER_SRC) \
                      $(SIGNAL_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

# --- TimerScheduler ---
$(TEST_TIMER_SCHEDULER): tests/timer/test_timer_scheduler.cpp \
                         $(TIMER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

# --- EventBus (header-only) ---
$(TEST_EVENT_BUS): tests/event/test_event_bus.cpp \
                   $(EVENT_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< $(LDFLAGS) -o $@

# --- IPC ---
$(TEST_IPC): tests/ipc/test_ipc.cpp $(IPC_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

# --- Watchdog (links Logger + ThreadManager) ---
$(TEST_WATCHDOG): tests/watchdog/test_watchdog.cpp \
                  $(WATCHDOG_SRC) \
                  $(THREAD_SRC) \
                  $(LOGGER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

# --- Config (links Logger) ---
$(TEST_CONFIG): tests/config/test_config.cpp \
                $(CONFIG_SRC) \
                $(LOGGER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

# --- Network (links Logger + ThreadManager; Queue and EventBus are header-only) ---
$(TEST_NETWORK): tests/network/test_network.cpp \
                 $(NETWORK_SRC) \
                 $(THREAD_SRC) \
                 $(LOGGER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

# ===========================================================================
# Verification macros
# ===========================================================================
#
# Each per-component target invokes one of these for each binary it owns.
#
# TEST_ONLY_ONE      — run custom test only (used for the big stress test)
# FULL_VERIFY_ONE    — run custom, then Memcheck, then Helgrind
#
# Both macros stop on first failure and dump the captured output.
# On success they print a single short line per phase.
# ===========================================================================

# --- Custom test only ---
define TEST_ONLY_ONE
	@name=$(notdir $(1)); \
	log=/tmp/$$name.log; \
	printf "  [TEST]      %-35s ... " "$$name"; \
	if ./$(1) > $$log 2>&1; then \
	    printf "PASS\n"; \
	else \
	    printf "FAIL\n"; \
	    sed 's/^/    /' $$log; \
	    exit 1; \
	fi
endef

# --- Custom test → Memcheck → Helgrind ---
define FULL_VERIFY_ONE
	@name=$(notdir $(1)); \
	log=/tmp/$$name.log; \
	printf "  [TEST]      %-35s ... " "$$name"; \
	if ./$(1) > $$log 2>&1; then \
	    printf "PASS\n"; \
	else \
	    printf "FAIL\n"; \
	    sed 's/^/    /' $$log; \
	    exit 1; \
	fi; \
	mem_log=/tmp/$$name.mem.log; \
	printf "  [Memcheck]  %-35s ... " "$$name"; \
	if valgrind --leak-check=full --error-exitcode=1 \
	    --suppressions=$(SUPPRESSIONS) \
	    ./$(1) > $$mem_log 2>&1; then \
	    printf "PASS\n"; \
	else \
	    printf "FAIL\n"; \
	    tail -25 $$mem_log | sed 's/^/    /'; \
	    exit 1; \
	fi; \
	hg_log=/tmp/$$name.hg.log; \
	printf "  [Helgrind]  %-35s ... " "$$name"; \
	if valgrind --tool=helgrind --error-exitcode=1 \
	    --suppressions=$(SUPPRESSIONS) \
	    ./$(1) > $$hg_log 2>&1; then \
	    printf "PASS\n"; \
	else \
	    printf "FAIL\n"; \
	    tail -25 $$hg_log | sed 's/^/    /'; \
	    exit 1; \
	fi
endef

# ===========================================================================
# Per-component verification targets
# ===========================================================================

test-logger: $(TEST_LOGGER) $(TEST_LOGGER_SHUTDOWN) $(TEST_LOGGER_THREAD) $(TEST_LOGGER_CONFIG)
	@echo ""
	@echo "=== Logger ==="
	$(call FULL_VERIFY_ONE,$(TEST_LOGGER))
	$(call FULL_VERIFY_ONE,$(TEST_LOGGER_SHUTDOWN))
	$(call FULL_VERIFY_ONE,$(TEST_LOGGER_THREAD))
	$(call FULL_VERIFY_ONE,$(TEST_LOGGER_CONFIG))

test-signal: $(TEST_SIGNAL_HANDLER)
	@echo ""
	@echo "=== SignalHandler ==="
	$(call FULL_VERIFY_ONE,$(TEST_SIGNAL_HANDLER))

test-thread: $(TEST_THREAD_MANAGER) $(TEST_THREAD_MANAGER_STRESS) $(TEST_THREAD_MANAGER_SMALL) $(TEST_THREAD_MANAGER_TIMEOUT)
	@echo ""
	@echo "=== ThreadManager ==="
	$(call FULL_VERIFY_ONE,$(TEST_THREAD_MANAGER))
	$(call TEST_ONLY_ONE,$(TEST_THREAD_MANAGER_STRESS))
	$(call FULL_VERIFY_ONE,$(TEST_THREAD_MANAGER_SMALL))
	$(call FULL_VERIFY_ONE,$(TEST_THREAD_MANAGER_TIMEOUT))

test-queue: $(TEST_QUEUE) $(TEST_QUEUE_TIMEOUT)
	@echo ""
	@echo "=== Queue ==="
	$(call FULL_VERIFY_ONE,$(TEST_QUEUE))
	$(call FULL_VERIFY_ONE,$(TEST_QUEUE_TIMEOUT))

test-timer: $(TEST_TIMER_SCHEDULER)
	@echo ""
	@echo "=== TimerScheduler ==="
	$(call FULL_VERIFY_ONE,$(TEST_TIMER_SCHEDULER))

test-event: $(TEST_EVENT_BUS)
	@echo ""
	@echo "=== EventBus ==="
	$(call FULL_VERIFY_ONE,$(TEST_EVENT_BUS))

test-ipc: $(TEST_IPC)
	@echo ""
	@echo "=== IPC ==="
	$(call FULL_VERIFY_ONE,$(TEST_IPC))

test-watchdog: $(TEST_WATCHDOG)
	@echo ""
	@echo "=== Watchdog ==="
	$(call FULL_VERIFY_ONE,$(TEST_WATCHDOG))

test-config: $(TEST_CONFIG)
	@echo ""
	@echo "=== Config ==="
	$(call FULL_VERIFY_ONE,$(TEST_CONFIG))

test-network: $(TEST_NETWORK)
	@echo ""
	@echo "=== Network ==="
	$(call FULL_VERIFY_ONE,$(TEST_NETWORK))

test-firmware-app: $(TEST_FIRMWARE_APP)
	@echo ""
	@echo "=== FirmwareApp ==="
	$(call FULL_VERIFY_ONE,$(TEST_FIRMWARE_APP))

test-latest: $(ALIAS_LAST)
	@echo ""
	@echo "=== test-latest → $(ALIAS_LAST) ==="

# ===========================================================================
# run-tests — every component in sequence
# ===========================================================================
#
# Each prerequisite is a per-component target; make runs them in the
# order listed. Each target itself runs custom + Memcheck + Helgrind on
# its own binaries. On the first FAIL, make stops.

run-tests: test-logger \
           test-signal \
           test-thread \
           test-queue \
           test-timer \
           test-event \
           test-ipc \
           test-watchdog \
           test-config \
           test-network \
           test-firmware-app
	@echo ""
	@echo "========================================="
	@echo "  ALL COMPONENTS PASSED (custom + valgrind)"
	@echo "========================================="

# ---------------------------------------------------------------------------
# clean
# ---------------------------------------------------------------------------

clean:
	rm -rf $(BUILD_DIR)

# ---------------------------------------------------------------------------
# help
# ---------------------------------------------------------------------------

help:
	@echo "Linux IP Camera Firmware — build targets"
	@echo ""
	@echo "  Build"
	@echo "    make                 Build firmware + all tests"
	@echo "    make firmware        Build only the production binary"
	@echo "    make tests           Build only the test binaries"
	@echo "    make clean           Remove build artifacts"
	@echo ""
	@echo "  Test (all — custom + Memcheck + Helgrind per component)"
	@echo "    make run-tests       Run every component in sequence"
	@echo ""
	@echo "  Test (single component — build, custom, Memcheck, Helgrind)"
	@echo "    make test-logger"
	@echo "    make test-signal"
	@echo "    make test-thread"
	@echo "    make test-queue"
	@echo "    make test-timer"
	@echo "    make test-event"
	@echo "    make test-ipc"
	@echo "    make test-watchdog"
	@echo "    make test-config"
	@echo "    make test-network"
	@echo "    make test-firmware-app"
	@echo "    make test-latest     (currently: $(ALIAS_LAST))"
	@echo ""
	@echo "  make help            Show this message"