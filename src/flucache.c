/* GStreamer Time Shifting
 * Copyright (C) 2011 Fluendo S.A. <support@fluendo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG
#include "config.h"
#endif

#include "flucache.h"

#include <gst/gstfilememallocator.h>

#include <stdio.h>
#include <glib/gstdio.h>
#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (ts_flow);
#define GST_CAT_DEFAULT (ts_flow)

#define DEBUG 0
#define DEBUG_RINGBUFFER 0

#define INVALID_OFFSET ((guint64) -1)

typedef struct _Slot Slot;
typedef struct _SlotMeta SlotMeta;

#define SLOT_META_INFO  (gst_slot_meta_get_info())
#define gst_buffer_get_slot_meta(b) ((SlotMeta*)gst_buffer_get_meta((b),SLOT_META_INFO))

typedef enum
{
  STATE_EMPTY = 0,
  STATE_PART = 1,
  STATE_FULL = 2,
  STATE_POP = 3,
  STATE_RECYCLE = 4
} CacheState;

/* Slot manager */

struct _Slot
{
  volatile gint state;

  guint32 size;
  GstBuffer *buffer;
};

static inline gboolean
slot_available (Slot * slot, gsize * size)
{
  CacheState state = g_atomic_int_get (&slot->state);

  if (state <= STATE_PART) {
    if (size) {
      *size = CACHE_SLOT_SIZE - slot->size;
    }
    return TRUE;
  }

  return FALSE;
}

/* returns TRUE if slot is full */
static inline gboolean
slot_write (Slot * slot, guint8 * data, guint size, guint64 offset)
{
  GstMapInfo mi;

  GST_BUFFER_OFFSET (slot->buffer) = offset;
  g_return_val_if_fail (gst_buffer_map (slot->buffer, &mi, GST_MAP_WRITE),
      FALSE);
  memcpy (mi.data + slot->size, data, size);
  gst_buffer_unmap (slot->buffer, &mi);
#if DEBUG
  GST_LOG ("slot_write size %d", size);
//  gst_util_dump_mem (data, size);
#endif
  slot->size += size;
  if (slot->size == CACHE_SLOT_SIZE) {
    g_atomic_int_set (&slot->state, STATE_FULL);
    return TRUE;
  } else {
    g_atomic_int_set (&slot->state, STATE_PART);
    return FALSE;
  }
}

/* Slot Buffer */

/**
 * SlotMeta:
 * @cache: a reference to the our #cache
 * @slot: the slot of this buffer
 *
 * Subclass containing additional information about our cache.
 */
struct _SlotMeta
{
  GstMeta meta;

  /* Reference to the cache we belong to */
  GstShifterCache *cache;
  Slot *slot;
};

GType gst_slot_meta_api_get_type (void);
#define SLOT_META_API_TYPE  (gst_slot_meta_api_get_type())

static gboolean
gst_slot_meta_init (SlotMeta * meta, gpointer params, GstBuffer * buffer)
{
  meta->cache = NULL;
  meta->slot = NULL;
  return TRUE;
}

static void
gst_slot_meta_free (SlotMeta * meta, GstBuffer * buffer)
{
  if (meta->slot)
    g_atomic_int_set (&meta->slot->state, STATE_RECYCLE);

  if (meta->cache)
    gst_shifter_cache_unref (meta->cache);
}

GType
gst_slot_meta_api_get_type (void)
{
  static volatile GType type;
  static const gchar *tags[] = { NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstSlotMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

const GstMetaInfo *
gst_slot_meta_get_info (void)
{
  static const GstMetaInfo *info = NULL;

  if (g_once_init_enter (&info)) {
    const GstMetaInfo *_info = gst_meta_register (SLOT_META_API_TYPE,
        "GstSlotMeta", sizeof (SlotMeta),
        (GstMetaInitFunction) gst_slot_meta_init,
        (GstMetaFreeFunction) gst_slot_meta_free,
        (GstMetaTransformFunction) NULL);
    g_once_init_leave (&info, _info);
  }
  return info;
}

static GstBuffer *
gst_slot_buffer_new (GstShifterCache * cache, Slot * slot)
{
  SlotMeta *meta;

  GstBuffer *buffer = gst_buffer_copy (slot->buffer);

  meta = (SlotMeta *) gst_buffer_add_meta (buffer, SLOT_META_INFO, NULL);
  meta->cache = gst_shifter_cache_ref (cache);
  meta->slot = slot;

  GST_BUFFER_OFFSET (buffer) = GST_BUFFER_OFFSET (slot->buffer);
  GST_BUFFER_OFFSET_END (buffer) = GST_BUFFER_OFFSET (buffer) + slot->size;

  return buffer;
}

/* GstShifterCache */

struct _GstShifterCache
{
  volatile gint refcount;

  GMutex *lock;

  guint64 h_offset;             /* highest offset */
  guint64 l_rb_offset;          /* lowest offset in the ringbuffer */
  guint64 h_rb_offset;          /* highest offset in the ringbuffer (FULL slots) */

  gboolean need_discont;

  /* ring buffer */
  GstAllocator *alloc;
  guint nslots;
  volatile gint fslots;         /* number of full slots */
  Slot *slots;

  guint head;
  guint tail;

  GstClockTime mtime;           /* timestamp when migration started */
};

#define GST_CACHE_LOCK(cache) G_STMT_START {                                \
  g_mutex_lock (cache->lock);                                                \
} G_STMT_END

#define GST_CACHE_UNLOCK(cache) G_STMT_START {                              \
  g_mutex_unlock (cache->lock);                                              \
} G_STMT_END


void
dump_cache_state (GstShifterCache * cache, const gchar * title)
{
  static const gchar *state_names[] =
      { "EMPTY   ", "PART    ", "FULL    ", "POP     ", "RECYCLE " };
  gint i;
  GST_DEBUG ("---> %s \t head: %d tail: %d nslots: %d fslots: %d",
      title, cache->head, cache->tail, cache->nslots,
      g_atomic_int_get (&cache->fslots));
  GST_DEBUG ("     h_rb_offset %" G_GUINT64_FORMAT " l_rb_offset %"
      G_GUINT64_FORMAT " h_offset %" G_GUINT64_FORMAT,
      cache->h_rb_offset, cache->l_rb_offset,
      cache->h_offset);

  for (i = 0; i < cache->nslots; i++) {
    Slot *slot = &cache->slots[i];
    CacheState state = g_atomic_int_get (&slot->state);
    GST_LOG ("     %d. %s buffer %p size %" G_GUINT32_FORMAT " offset %"
        G_GUINT64_FORMAT, i, state_names[state],
        slot->buffer, slot->size, GST_BUFFER_OFFSET (slot->buffer));
  }
}

static inline void
gst_shifter_cache_flush (GstShifterCache * cache)
{
  guint i;
  for (i = 0; i < cache->nslots; i++) {
    Slot *slot = &cache->slots[i];
    slot->state = STATE_EMPTY;
    GST_BUFFER_OFFSET (slot->buffer) = INVALID_OFFSET;
    slot->size = 0;
  }
  cache->head = cache->tail = 0;
  cache->fslots = 0;
  cache->need_discont = TRUE;
}

/**
 * gst_shifter_cache_new:
 * @size: cache size
 *
 * Create a new cache instance. @size will be rounded up to the
 * nearest CACHE_SLOT_SIZE multiple and used as the ringbuffer size.
 *
 * Returns: a new #GstShifterCache
 *
 */
GstShifterCache *
gst_shifter_cache_new (gsize size, const gchar * allocator_name)
{
  GstShifterCache *cache;
  guint nslots;
  int i;

  cache = g_new (GstShifterCache, 1);

  cache->refcount = 1;

  cache->lock = g_mutex_new ();

  cache->h_offset = 0;
  cache->l_rb_offset = 0;
  cache->h_rb_offset = 0;

  /* Ring buffer */
  cache->alloc = gst_allocator_find (allocator_name);
  g_return_val_if_fail (cache->alloc, NULL);
  nslots = size / CACHE_SLOT_SIZE;
  cache->nslots = nslots;

  cache->slots = (Slot *) g_new (Slot, nslots);
  for (i = 0; i < cache->nslots; i++) {
    GstBuffer *buf = gst_buffer_new_allocate (
       cache->alloc, CACHE_SLOT_SIZE, NULL);

    g_return_val_if_fail (buf, NULL);

    cache->slots[i].buffer = buf;
  }

  gst_shifter_cache_flush (cache);

  return cache;
}

/**
 * gst_shifter_cache_ref:
 * @cache: a #GstShifterCache
 *
 * Increase the refcount of @cache.
 *
 */
GstShifterCache *
gst_shifter_cache_ref (GstShifterCache * cache)
{
  g_return_val_if_fail (cache != NULL, NULL);

  g_atomic_int_inc (&cache->refcount);

  return cache;
}

static void
gst_shifter_cache_free (GstShifterCache * cache)
{
  int i;

  for (i = 0; i < cache->nslots; i++) {
    gst_buffer_unref (cache->slots[i].buffer);
  }
  gst_object_unref (cache->alloc);
  g_free (cache->slots);

  g_mutex_free (cache->lock);

  g_free (cache);
}

/**
 * gst_shifter_cache_unref:
 * @cache: a #GstShifterCache
 *
 * Unref @cache and free the resources when the refcount reaches 0.
 *
 */
void
gst_shifter_cache_unref (GstShifterCache * cache)
{
  g_return_if_fail (cache != NULL);

  if (g_atomic_int_dec_and_test (&cache->refcount))
    gst_shifter_cache_free (cache);
}

static inline gboolean
gst_shifter_cache_recycle (GstShifterCache * cache, Slot * slot)
{
  gboolean recycle;
  recycle = g_atomic_int_compare_and_exchange (&slot->state, STATE_RECYCLE,
      STATE_EMPTY);
  if (recycle) {
    if (GST_BUFFER_OFFSET (slot->buffer) != INVALID_OFFSET)
      cache->l_rb_offset = GST_BUFFER_OFFSET (slot->buffer) + slot->size;
    GST_BUFFER_OFFSET (slot->buffer) = INVALID_OFFSET;
    slot->size = 0;
  }
  return recycle;
}

static inline gboolean
gst_shifter_cache_rollback (GstShifterCache * cache, Slot * slot)
{
  gboolean rollback;
  rollback = g_atomic_int_compare_and_exchange (&slot->state, STATE_RECYCLE,
      STATE_FULL);
  if (rollback) {
    g_atomic_int_inc (&cache->fslots);
  } else if (g_atomic_int_get (&slot->state) == STATE_FULL) {
    rollback = TRUE;
  }
  return rollback;
}

static inline gboolean
gst_shifter_cache_rollforward (GstShifterCache * cache, Slot * slot)
{
  gboolean rollforward;
  rollforward = g_atomic_int_compare_and_exchange (&slot->state, STATE_FULL,
      STATE_RECYCLE);
  if (rollforward) {
    g_atomic_int_add (&cache->fslots, -1);
  } else if (g_atomic_int_get (&slot->state) == STATE_RECYCLE) {
    rollforward = TRUE;
  }
  return rollforward;
}

/**
 * gst_shifter_cache_pop:
 * @cache: a #GstShifterCache
 *
 * Get the head of the cache.
 *
 * Returns: the head buffer of @cache or NULL when the cache is empty.
 *
 */

GstBuffer *
gst_shifter_cache_pop (GstShifterCache * cache, gboolean drain)
{
  GstBuffer *buffer = NULL;
  Slot *head;
  gboolean pop;

#if DEBUG_RINGBUFFER
  dump_cache_state (cache, "pre-pop");
#endif

  head = &cache->slots[cache->head];

  if (drain) {
    if (g_atomic_int_compare_and_exchange (&head->state, STATE_PART,
            STATE_FULL)) {
      cache->h_rb_offset = GST_BUFFER_OFFSET (head->buffer) + head->size;
      g_atomic_int_inc (&cache->fslots);
    }
  }

  pop = g_atomic_int_compare_and_exchange (&head->state, STATE_FULL, STATE_POP);

  if (pop) {
    g_atomic_int_add (&cache->fslots, -1);
    buffer = gst_slot_buffer_new (cache, head);
    if (cache->need_discont) {
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
      cache->need_discont = FALSE;
    }

    cache->head = (cache->head + 1) % cache->nslots;
  }
#if DEBUG_RINGBUFFER
  dump_cache_state (cache, "post-pop");
#endif

  return buffer;
}

/**
 * gst_shifter_cache_push:
 * @cache: a #GstShifterCache
 * @data: pointer to the data to be inserted in the cache
 * @size: size in bytes of provided data
 *
 * Cache the @buffer and takes ownership of the it.
 *
 */

gboolean
gst_shifter_cache_push (GstShifterCache * cache, guint8 *data, gsize size)
{
  Slot *tail;
  gsize avail;

#if DEBUG_RINGBUFFER
  dump_cache_state (cache, "pre-push");
#endif

  while (size) {
#if DEBUG_RINGBUFFER
    GST_DEBUG ("remaining size %d", size);
    dump_cache_state (cache, "pre-push");
#endif
    tail = &cache->slots[cache->tail];
    gst_shifter_cache_recycle (cache, tail);
    if (slot_available (tail, &avail)) {
      avail = MIN (avail, size);
      if (slot_write (tail, data, avail, cache->h_rb_offset)) {
        /* Move the tail when the slot is full */
        cache->tail = (cache->tail + 1) % cache->nslots;
        cache->h_rb_offset += CACHE_SLOT_SIZE;
        g_atomic_int_inc (&cache->fslots);
      }
      data += avail;
      size -= avail;
      cache->h_offset += avail;
    } else {
      // TODO: This should be handled in a designed manner
      return FALSE;
    }
  }
#if DEBUG_RINGBUFFER
  dump_cache_state (cache, "post-push");
#endif

  return TRUE;
}

/**
 * gst_shifter_cache_has_offset:
 * @cache: a #GstShifterCache
 * @offset: byte offset to check if is in the cache.
 *
 * Checks if an specified offset can be found on the cache.
 *
 */
gboolean
gst_shifter_cache_has_offset (GstShifterCache * cache, guint64 offset)
{
  g_return_val_if_fail (cache != NULL, FALSE);
  gboolean ret;

  GST_CACHE_LOCK (cache);
  dump_cache_state (cache, "has_offset");

  ret = (offset >= cache->l_rb_offset && offset < cache->h_offset);
  GST_CACHE_UNLOCK (cache);
  return ret;
}

/**
 * gst_shifter_cache_seek:
 * @cache: a #GstShifterCache
 * @offset: byte offset where the cache have to be repositioned.
 *
 * Reconfigures the cache to read from the closest location to the specified
 * offset.
 *
 */
gboolean
gst_shifter_cache_seek (GstShifterCache * cache, guint64 offset)
{
  Slot *head;
  g_return_val_if_fail (cache != NULL, FALSE);

  GST_DEBUG ("requested seek at offset: %" G_GUINT64_FORMAT, offset);

  GST_CACHE_LOCK (cache);
  dump_cache_state (cache, "pre-seek");

  /* clamp to a min/max valid offset */
  offset = MIN (offset, cache->h_rb_offset);
  offset = MAX (offset, cache->l_rb_offset);
  GST_CACHE_UNLOCK (cache);

  GST_DEBUG ("seeking for offset: %" G_GUINT64_FORMAT, offset);

  /* First check if we can find it in the ringbuffer */
  if (offset >= cache->l_rb_offset && offset < cache->h_rb_offset) {
    GST_DEBUG ("seeking in the ringbuffer");
    guint seeker = cache->head;
    head = &cache->slots[seeker];

    if (offset >= GST_BUFFER_OFFSET (head->buffer)) {
      if (offset < GST_BUFFER_OFFSET (head->buffer) + head->size) {
        GST_DEBUG ("found in current head");
        /* Already in the requested position so do nothing */
        gst_shifter_cache_rollback (cache, head);
        goto beach;
      }
      /* perform seek in the future */
      GST_DEBUG ("seeking in the future");
      do {
        gst_shifter_cache_rollforward (cache, head);
        seeker = (seeker + 1) % cache->nslots;
        head = &cache->slots[seeker];
      } while (!(offset >= GST_BUFFER_OFFSET (head->buffer) &&
          offset < GST_BUFFER_OFFSET (head->buffer) + head->size));
      gst_shifter_cache_rollback (cache, head);
    } else {
      /* perform seek in the past */
      GST_DEBUG ("seeking in the past");
      gst_shifter_cache_rollback (cache, head);
      do {
        if (seeker == 0)
          seeker = cache->nslots - 1;
        else
          seeker--;
        head = &cache->slots[seeker];
        if (!gst_shifter_cache_rollback (cache, head)) {
          seeker = (seeker + 1) % cache->nslots;
          break;
        }
      } while (!(offset >= GST_BUFFER_OFFSET (head->buffer) &&
          offset < GST_BUFFER_OFFSET (head->buffer) + head->size));
    }
    cache->head = seeker;
    goto beach;
  }

beach:
  dump_cache_state (cache, "post-seek");

  return TRUE;
}

/**
 * gst_shifter_cache_is_empty:
 * @cache: a #GstShifterCache
 *
 * Return TRUE if cache is empty.
 *
 */
gboolean
gst_shifter_cache_is_empty (GstShifterCache * cache)
{
  gboolean is_empty = TRUE;

  g_return_val_if_fail (cache != NULL, TRUE);

  is_empty = (g_atomic_int_get (&cache->fslots) == 0);

  return is_empty;
}

/**
 * gst_shifter_cache_fullness:
 * @cache: a #GstShifterCache
 *
 * Return # of bytes remaining in the ringbuffer.
 *
 */
guint64
gst_shifter_cache_fullness (GstShifterCache * cache)
{
  g_return_val_if_fail (cache != NULL, 0);

  if (gst_shifter_cache_is_empty (cache)) {
    return 0;
  } else {
    Slot *head, *tail;
    head = &cache->slots[cache->head];
    tail = &cache->slots[cache->tail];
    return (GST_BUFFER_OFFSET (tail->buffer) - GST_BUFFER_OFFSET (head->buffer)
        + head->size);
  }
}
