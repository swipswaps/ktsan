The Kernel Concurrency Sanitizer (KCSAN)
========================================

Overview
--------

*Kernel Concurrency Sanitizer (KCSAN)* is a dynamic data-race detector for
kernel space. KCSAN is a sampling watchpoint-based data-race detector -- this
is unlike Kernel Thread Sanitizer (KTSAN), which is a happens-before data-race
detector. Key priorities in KCSAN's design are lack of false positives,
scalability, and simplicity. More details can be found in `Implementation
Details`_.

KCSAN uses compile-time instrumentation to instrument memory accesses. KCSAN is
supported in both GCC and Clang. With GCC it requires version 7.3.0 or later.
With Clang it requires version 7.0.0 or later.

Usage
-----

To enable KCSAN configure kernel with::

    CONFIG_KCSAN = y

KCSAN provides several other configuration options to customize behaviour (see
their respective help text for more info).

Error reports
~~~~~~~~~~~~~

A typical data-race report looks like this::

    ==================================================================
    BUG: KCSAN: data-race in generic_permission+0x5b/0x2a0 and kernfs_refresh_inode+0x70/0x170

    write to 0xffff8fee4c40700c of 4 bytes by task 175 on cpu 4:
     kernfs_refresh_inode+0x70/0x170
     kernfs_iop_permission+0x4f/0x90
     inode_permission+0x190/0x200
     link_path_walk.part.0+0x503/0x8e0
     path_lookupat.isra.0+0x69/0x4d0
     filename_lookup+0x136/0x280
     user_path_at_empty+0x47/0x60
     vfs_statx+0x9b/0x130
     __do_sys_newlstat+0x50/0xb0
     __x64_sys_newlstat+0x37/0x50
     do_syscall_64+0x85/0x260
     entry_SYSCALL_64_after_hwframe+0x44/0xa9

    read to 0xffff8fee4c40700c of 4 bytes by task 166 on cpu 6:
     generic_permission+0x5b/0x2a0
     kernfs_iop_permission+0x66/0x90
     inode_permission+0x190/0x200
     link_path_walk.part.0+0x503/0x8e0
     path_lookupat.isra.0+0x69/0x4d0
     filename_lookup+0x136/0x280
     user_path_at_empty+0x47/0x60
     do_faccessat+0x11a/0x390
     __x64_sys_access+0x3c/0x50
     do_syscall_64+0x85/0x260
     entry_SYSCALL_64_after_hwframe+0x44/0xa9

    Reported by Kernel Concurrency Sanitizer on:
    CPU: 6 PID: 166 Comm: systemd-journal Not tainted 5.3.0-rc7+ #1
    Hardware name: QEMU Standard PC (i440FX + PIIX, 1996), BIOS 1.12.0-1 04/01/2014
    ==================================================================

The header of the report provides a short summary of the functions involved in
the race. It is followed by the access types and stack traces of the 2 threads
involved in the data race.

The other less common type of data-race report looks like this::

    ==================================================================
    BUG: KCSAN: racing read in e1000_clean_rx_irq+0x551/0xb10

    race at unknown origin, with read to 0xffff933db8a2ae6c of 1 bytes by interrupt on cpu 0:
     e1000_clean_rx_irq+0x551/0xb10
     e1000_clean+0x533/0xda0
     net_rx_action+0x329/0x900
     __do_softirq+0xdb/0x2db
     irq_exit+0x9b/0xa0
     do_IRQ+0x9c/0xf0
     ret_from_intr+0x0/0x18
     default_idle+0x3f/0x220
     arch_cpu_idle+0x21/0x30
     do_idle+0x1df/0x230
     cpu_startup_entry+0x14/0x20
     rest_init+0xc5/0xcb
     arch_call_rest_init+0x13/0x2b
     start_kernel+0x6db/0x700

    Reported by Kernel Concurrency Sanitizer on:
    CPU: 0 PID: 0 Comm: swapper/0 Not tainted 5.3.0-rc7+ #2
    Hardware name: QEMU Standard PC (i440FX + PIIX, 1996), BIOS 1.12.0-1 04/01/2014
    ==================================================================

This report is generated where it was not possible to determine the other
racing thread, but a race was inferred due to the data-value of the watched
memory location having changed. These can occur either due to missing
instrumentation or e.g. DMA accesses.

Implementation Details
----------------------

The general approach is inspired by `DataCollider
<http://usenix.org/legacy/events/osdi10/tech/full_papers/Erickson.pdf>`_.
Unlike DataCollider, KCSAN does not use hardware watchpoints, but instead
relies on compiler instrumentation. Watchpoints are implemented using an
efficient encoding that stores access type, size, and address in a long; the
benefits of using "soft watchpoints" are portability and greater flexibility in
limiting which accesses trigger a watchpoint.

More specifically, KCSAN requires instrumenting plain (unmarked, non-atomic)
memory operations; for each instrumented plain access:

1. Check if a matching watchpoint exists; if yes, and at least one access is a
   write, then we encountered a racing access.

2. Periodically, if no matching watchpoint exists, set up a watchpoint and
   stall some delay.

3. Also check the data value before the delay, and re-check the data value
   after delay; if the values mismatch, we infer a race of unknown origin.

To detect races between plain and atomic memory operations, KCSAN also
annotates atomic accesses, but only to check if a watchpoint exists
(``kcsan_check_atomic(..)``); i.e.  KCSAN never sets up a watchpoint on atomic
accesses.

Key Properties
~~~~~~~~~~~~~~

1. **Performance Overhead:** KCSAN's runtime is minimal, and does not require
   locking shared state for each access. This results in significantly better
   performance in comparison with KTSAN.

2. **Memory Overhead:** No shadow memory is required. The current
   implementation uses a small array of longs to encode watchpoint information,
   which is negligible.

3. **Memory Ordering:** KCSAN is *not* aware of the LKMM's ordering rules. This
   may result in missed data-races (false negatives), compared to a
   happens-before race detector such as KTSAN.

4. **Accuracy:** Imprecise, since it uses a sampling strategy.

5. **Annotation Overheads:** Minimal annotation is required outside the KCSAN
   runtime. With a happens-before race detector, any omission leads to false
   positives, which is especially important in the context of the kernel which
   includes numerous custom synchronization mechanisms. With KCSAN, as a
   result, maintenance overheads are minimal as the kernel evolves.

6. **Detects Racy Writes from Devices:** Due to checking data values upon
   setting up watchpoints, racy writes from devices can also be detected.

Relationship with the Linux Kernel Memory Model (LKMM)
------------------------------------------------------

The LKMM defines the propagation and ordering rules of various memory
operations, which gives developers the ability to reason about concurrent code.
Ultimately this allows to determine the possible executions of concurrent code,
and if that code is free from data-races.

KCSAN is aware of *atomic* accesses (``READ_ONCE``, ``WRITE_ONCE``,
``atomic_*``, etc.), but is oblivious of any ordering guarantees. In other
words, KCSAN assumes that as long as a plain access is not observed to race
with another conflicting access, memory operations are correctly ordered.

This means that KCSAN will not report *potential* data-races due to missing
memory ordering. If, however, missing memory ordering (that is observable with
a particular compiler and architecture) leads to an observable data-race (e.g.
entering a critical section erroneously), KCSAN would report the resulting
data-race.
