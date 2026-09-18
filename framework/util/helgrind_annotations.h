#ifndef HELGRIND_ANNOTATIONS_H
#define HELGRIND_ANNOTATIONS_H

/*
 * Valgrind client-request annotations for Helgrind.
 *
 * Helgrind does not understand C++11 std::atomic as a synchronization
 * primitive. When two threads communicate via an atomic, Helgrind
 * reports a "possible data race" unless we explicitly annotate the
 * release-side (HAPPENS_BEFORE) and acquire-side (HAPPENS_AFTER).
 *
 * These annotations are:
 *   - Active when <valgrind/helgrind.h> is available at compile time
 *   - No-ops otherwise (production builds, non-Valgrind environments)
 *
 * Semantics:
 *   ANNOTATE_HAPPENS_BEFORE(addr):
 *       "Everything I have done before this point happens-before any
 *        thread that subsequently performs ANNOTATE_HAPPENS_AFTER on
 *        the same address."
 *
 *   ANNOTATE_HAPPENS_AFTER(addr):
 *       "Establish a happens-before edge with the last
 *        ANNOTATE_HAPPENS_BEFORE on this address."
 */

#if defined(__has_include)
#  if __has_include(<valgrind/helgrind.h>)
#    include <valgrind/helgrind.h>
#    if __has_include(<valgrind/memcheck.h>)
#      include <valgrind/memcheck.h>
#    endif
#    define FIRMWARE_HAVE_HELGRIND_ANNOTATIONS 1
#  endif
#endif

#ifndef FIRMWARE_HAVE_HELGRIND_ANNOTATIONS
#  define ANNOTATE_HAPPENS_BEFORE(addr)            ((void)0)
#  define ANNOTATE_HAPPENS_AFTER(addr)             ((void)0)
#  define ANNOTATE_HAPPENS_BEFORE_FORGET_ALL(addr) ((void)0)
#endif

/*
 * For std::atomic counters used with fetch_add from multiple threads.
 * Helgrind has no model for lock-free RMW; this tells it the access
 * pattern is intentional.
 *
 * Usage:
 *     std::atomic<int> count{0};
 *     ANNOTATE_ATOMIC_COUNTER(&count);   // once, at declaration
 */
#ifndef FIRMWARE_HAVE_HELGRIND_ANNOTATIONS
#  define ANNOTATE_ATOMIC_COUNTER(addr) ((void)0)
#else
#  define ANNOTATE_ATOMIC_COUNTER(addr) \
      ANNOTATE_BENIGN_RACE_SIZED( \
          (addr), sizeof(*(addr)), \
          "std::atomic counter: benign concurrent access")
#endif

#endif // HELGRIND_ANNOTATIONS_H