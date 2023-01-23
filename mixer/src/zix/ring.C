/*
  Copyright 2011-2020 David Robillard <d@drobilla.net>

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THIS SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include "ring.h"

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_MLOCK
#  include <sys/mman.h>
#  define ZIX_MLOCK(ptr, size) mlock((ptr), (size))
#elif defined(_WIN32)
#  include <windows.h>
#  define ZIX_MLOCK(ptr, size) VirtualLock((ptr), (size))
#else
#  pragma message("warning: No memory locking, possible RT violations")
#  define ZIX_MLOCK(ptr, size)
#endif

#if defined(_MSC_VER)
#  include <windows.h>
#  define ZIX_READ_BARRIER() MemoryBarrier()
#  define ZIX_WRITE_BARRIER() MemoryBarrier()
#elif defined(__GNUC__)
#  define ZIX_READ_BARRIER() __atomic_thread_fence(__ATOMIC_ACQUIRE)
#  define ZIX_WRITE_BARRIER() __atomic_thread_fence(__ATOMIC_RELEASE)
#else
#  pragma message("warning: No memory barriers, possible SMP bugs")
#  define ZIX_READ_BARRIER()
#  define ZIX_WRITE_BARRIER()
#endif

struct ZixRingImpl {
  ZixAllocator* allocator;  ///< User allocator
  uint32_t      write_head; ///< Read index into buf
  uint32_t      read_head;  ///< Write index into buf
  uint32_t      size;       ///< Size (capacity) in bytes
  uint32_t      size_mask;  ///< Mask for fast modulo
  char*         buf;        ///< Contents
};

static inline uint32_t
zix_atomic_load(const uint32_t* const ptr)
{
#if defined(_MSC_VER)
  const uint32_t val = *ptr;
  _ReadBarrier();
  return val;
#else
  return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
#endif
}

static inline void
zix_atomic_store(uint32_t* const ptr, // NOLINT(readability-non-const-parameter)
                 const uint32_t  val)
{
#if defined(_MSC_VER)
  _WriteBarrier();
  *ptr = val;
#else
  __atomic_store_n(ptr, val, __ATOMIC_RELEASE);
#endif
}

static inline uint32_t
next_power_of_two(uint32_t size)
{
  // http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
  size--;
  size |= size >> 1u;
  size |= size >> 2u;
  size |= size >> 4u;
  size |= size >> 8u;
  size |= size >> 16u;
  size++;
  return size;
}

ZixRing*
zix_ring_new(uint32_t size)
{
  ZixRing* ring    = (ZixRing*)malloc(sizeof(ZixRing));
  ring->write_head = 0;
  ring->read_head  = 0;
  ring->size       = next_power_of_two(size);
  ring->size_mask  = ring->size - 1;
  ring->buf        = (char*)malloc(ring->size);
  return ring;
}

void
zix_ring_free(ZixRing* ring)
{
  if (ring) {
    free(ring->buf);
    free(ring);
  }
}

void
zix_ring_mlock(ZixRing* ring)
{
  ZIX_MLOCK(ring, sizeof(ZixRing));
  ZIX_MLOCK(ring->buf, ring->size);
}

void
zix_ring_reset(ZixRing* ring)
{
  ring->write_head = 0;
  ring->read_head  = 0;
}

static inline uint32_t
read_space_internal(const ZixRing* ring, uint32_t r, uint32_t w)
{
  if (r < w) {
    return w - r;
  }

  return (w - r + ring->size) & ring->size_mask;
}

uint32_t
zix_ring_read_space(const ZixRing* ring)
{
  return read_space_internal(ring, ring->read_head, ring->write_head);
}

static inline uint32_t
write_space_internal(const ZixRing* ring, uint32_t r, uint32_t w)
{
  if (r == w) {
    return ring->size - 1;
  }

  if (r < w) {
    return ((r - w + ring->size) & ring->size_mask) - 1;
  }

  return (r - w) - 1;
}

uint32_t
zix_ring_write_space(const ZixRing* ring)
{
  return write_space_internal(ring, ring->read_head, ring->write_head);
}

uint32_t
zix_ring_capacity(const ZixRing* ring)
{
  return ring->size - 1;
}

static inline uint32_t
peek_internal(const ZixRing* ring,
              uint32_t       r,
              uint32_t       w,
              uint32_t       size,
              void*          dst)
{
  if (read_space_internal(ring, r, w) < size) {
    return 0;
  }

  if (r + size < ring->size) {
    memcpy(dst, &ring->buf[r], size);
  } else {
    const uint32_t first_size = ring->size - r;
    memcpy(dst, &ring->buf[r], first_size);
    memcpy((char*)dst + first_size, &ring->buf[0], size - first_size);
  }

  return size;
}

uint32_t
zix_ring_peek(ZixRing* ring, void* dst, uint32_t size)
{
  return peek_internal(ring, ring->read_head, ring->write_head, size, dst);
}

uint32_t
zix_ring_read(ZixRing* ring, void* dst, uint32_t size)
{
  const uint32_t r = ring->read_head;
  const uint32_t w = ring->write_head;

  if (peek_internal(ring, r, w, size, dst)) {
    ZIX_READ_BARRIER();
    ring->read_head = (r + size) & ring->size_mask;
    return size;
  }

  return 0;
}

uint32_t
zix_ring_skip(ZixRing* ring, uint32_t size)
{
  const uint32_t r = ring->read_head;
  const uint32_t w = ring->write_head;
  if (read_space_internal(ring, r, w) < size) {
    return 0;
  }

  ZIX_READ_BARRIER();
  ring->read_head = (r + size) & ring->size_mask;
  return size;
}

uint32_t
zix_ring_write(ZixRing* const ring, const void* src, const uint32_t size)
{
  ZixRingTransaction tx = zix_ring_begin_write(ring);

  if (zix_ring_amend_write(ring, &tx, src, size) ||
      zix_ring_commit_write(ring, &tx)) {
    return 0;
  }

  return size;
}

ZixRingTransaction
zix_ring_begin_write(ZixRing* const ring)
{
  const uint32_t r = zix_atomic_load(&ring->read_head);
  const uint32_t w = ring->write_head;

  const ZixRingTransaction tx = {r, w};
  return tx;
}

ZixStatus
zix_ring_amend_write(ZixRing* const            ring,
                     ZixRingTransaction* const tx,
                     const void* const         src,
                     const uint32_t            size)
{
  const uint32_t r = tx->read_head;
  const uint32_t w = tx->write_head;
  if (write_space_internal(ring, r, w) < size) {
    return ZIX_STATUS_NO_MEM;
  }

  const uint32_t end = w + size;
  if (end <= ring->size) {
    memcpy(&ring->buf[w], src, size);
    tx->write_head = end & ring->size_mask;
  } else {
    const uint32_t size1 = ring->size - w;
    const uint32_t size2 = size - size1;
    memcpy(&ring->buf[w], src, size1);
    memcpy(&ring->buf[0], (const char*)src + size1, size2);
    tx->write_head = size2;
  }

  return ZIX_STATUS_SUCCESS;
}

ZixStatus
zix_ring_commit_write(ZixRing* const ring, const ZixRingTransaction* const tx)
{
  zix_atomic_store(&ring->write_head, tx->write_head);
  return ZIX_STATUS_SUCCESS;
}
