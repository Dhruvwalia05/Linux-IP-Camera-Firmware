# ============================================================================
# Linux IP Camera Firmware — Build System
# ============================================================================
#
# Usage:
#   make              Build firmware binary + all test binaries
#   make firmware     Build only the production firmware binary
#   make tests        Build only test binaries
#   make run-tests    Build + run all tests (output shown on failure only)
#   make valgrind     Run Memcheck + Helgrind on selected tests
#   make clean        Remove build artifacts
#   make help         Show this message
#
# ============================================================================

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -pthread -I.
LDFLAGS  := -pthread

BUILD_DIR := build

# ---------------------------------------------------------------------------
# Source groups (reused across targets)
# ---------------------------------------------------------------------------

LOGGER_SRC := framework/logger/logger.cpp
SIGNAL_SRC := framework/signal/signal_handler.cpp
THREAD_SRC := framework/thread/thread_manager.cpp \
              framework/thread/stop_token.cpp
TIMER_SRC  := framework/timer/timer_scheduler.cpp

QUEUE_HEADERS := framework/queue/queue.h \
                 framework/queue/queue.tpp

# ---------------------------------------------------------------------------
# Production binary
# ---------------------------------------------------------------------------

FIRMWARE_SRC := app/main.cpp \
                app/firmware_app.cpp \
                $(LOGGER_SRC) \
                $(SIGNAL_SRC)

FIRMWARE_BIN := $(BUILD_DIR)/firmware

# ---------------------------------------------------------------------------
# Test binaries
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
    $(TEST_TIMER_SCHEDULER)

# Tests to run under Valgrind (skipping the big stress test)
VALGRIND_TARGETS := \
    $(TEST_QUEUE) \
    $(TEST_QUEUE_TIMEOUT) \
    $(TEST_THREAD_MANAGER_SMALL) \
    $(TEST_THREAD_MANAGER_TIMEOUT) \
    $(TEST_FIRMWARE_APP) \
    $(TEST_TIMER_SCHEDULER)

# ---------------------------------------------------------------------------
# Top-level targets
# ---------------------------------------------------------------------------

.PHONY: all firmware tests run-tests valgrind clean help

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

# ---------------------------------------------------------------------------
# Logger tests
# ---------------------------------------------------------------------------

$(TEST_LOGGER): tests/logger/test_logger.cpp $(LOGGER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(TEST_LOGGER_SHUTDOWN): tests/logger/test_logger_shutdown.cpp $(LOGGER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(TEST_LOGGER_THREAD): tests/logger/test_logger_thread.cpp $(LOGGER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(TEST_LOGGER_CONFIG): tests/logger/test_logger_config.cpp $(LOGGER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

# ---------------------------------------------------------------------------
# Signal handler tests
# ---------------------------------------------------------------------------

$(TEST_SIGNAL_HANDLER): tests/signal/test_signal_handler.cpp $(SIGNAL_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

# ---------------------------------------------------------------------------
# Thread manager tests
# ---------------------------------------------------------------------------

$(TEST_THREAD_MANAGER): tests/thread/test_thread_manager.cpp $(THREAD_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(TEST_THREAD_MANAGER_STRESS): tests/thread/test_thread_manager_stress.cpp $(THREAD_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

$(TEST_THREAD_MANAGER_SMALL): tests/thread/test_thread_manager_stress.cpp $(THREAD_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -DSTRESS_ITERATIONS=20 -DCONCURRENT_THREADS=4 $^ $(LDFLAGS) -o $@

$(TEST_THREAD_MANAGER_TIMEOUT): tests/thread/test_thread_manager_timeout.cpp $(THREAD_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

# ---------------------------------------------------------------------------
# Queue tests (header-only — headers listed for dependency tracking)
# ---------------------------------------------------------------------------

$(TEST_QUEUE): tests/queue/test_queue.cpp $(QUEUE_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< $(LDFLAGS) -o $@

$(TEST_QUEUE_TIMEOUT): tests/queue/test_queue_timeout.cpp $(QUEUE_HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< $(LDFLAGS) -o $@

# ---------------------------------------------------------------------------
# FirmwareApp tests
# ---------------------------------------------------------------------------

$(TEST_FIRMWARE_APP): tests/firmware_app/test_firmware_app.cpp \
                      app/firmware_app.cpp \
                      $(LOGGER_SRC) \
                      $(SIGNAL_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

# ---------------------------------------------------------------------------
# TimerScheduler tests
# ---------------------------------------------------------------------------

$(TEST_TIMER_SCHEDULER): tests/timer/test_timer_scheduler.cpp \
                         $(TIMER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

# ---------------------------------------------------------------------------
# run-tests
# ---------------------------------------------------------------------------
#
# Runs every test binary. Output is captured and shown only on failure,
# matching cargo test / pytest conventions.

run-tests: tests
	@echo ""
	@echo "========================================="
	@echo "  Running test suite"
	@echo "========================================="
	@pass=0; fail=0; \
	for t in $(ALL_TESTS); do \
	    name=$$(basename $$t); \
	    log=/tmp/$$name.log; \
	    if ./$$t > $$log 2>&1; then \
	        printf "  [PASS] %s\n" "$$name"; \
	        pass=$$((pass + 1)); \
	    else \
	        printf "  [FAIL] %s\n" "$$name"; \
	        echo "    --- captured output ---"; \
	        sed 's/^/    /' $$log; \
	        echo "    -----------------------"; \
	        fail=$$((fail + 1)); \
	    fi; \
	done; \
	echo ""; \
	echo "  $$pass passed, $$fail failed"; \
	echo "========================================="; \
	if [ $$fail -ne 0 ]; then exit 1; fi
	@echo ""

# ---------------------------------------------------------------------------
# valgrind
# ---------------------------------------------------------------------------

valgrind: tests
	@echo ""
	@echo "========================================="
	@echo "  Running Valgrind checks"
	@echo "========================================="
	@fail=0; \
	for t in $(VALGRIND_TARGETS); do \
	    name=$$(basename $$t); \
	    mem_log=/tmp/$$name.mem.log; \
	    hg_log=/tmp/$$name.hg.log; \
	    printf "  [Memcheck] %s ... " "$$name"; \
	    if valgrind --leak-check=full --error-exitcode=1 \
	        --suppressions=framework/util/valgrind.supp \
	        ./$$t > $$mem_log 2>&1; then \
	        echo "PASS"; \
	    else \
	        echo "FAIL"; \
	        tail -25 $$mem_log | sed 's/^/    /'; \
	        fail=$$((fail + 1)); \
	    fi; \
	    printf "  [Helgrind] %s ... " "$$name"; \
	    if valgrind --tool=helgrind --error-exitcode=1 \
	        --suppressions=framework/util/valgrind.supp \
	        ./$$t > $$hg_log 2>&1; then \
	        echo "PASS"; \
	    else \
	        echo "FAIL"; \
	        tail -25 $$hg_log | sed 's/^/    /'; \
	        fail=$$((fail + 1)); \
	    fi; \
	done; \
	echo ""; \
	if [ $$fail -ne 0 ]; then \
	    echo "  VALGRIND CHECKS FAILED ($$fail)"; \
	    echo "========================================="; \
	    exit 1; \
	fi; \
	echo "  ALL VALGRIND CHECKS PASSED"; \
	echo "========================================="

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
	@echo "  make             Build firmware + all tests"
	@echo "  make firmware    Build only the production binary"
	@echo "  make tests       Build only the test binaries"
	@echo "  make run-tests   Run the full test suite"
	@echo "  make valgrind    Run Memcheck + Helgrind on key tests"
	@echo "                   (with framework/util/valgrind.supp applied)"
	@echo "  make clean       Remove build artifacts"
	@echo "  make help        Show this message"