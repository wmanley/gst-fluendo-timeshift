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

#include <stdio.h>
#include <glib/gstdio.h>
#include <string.h>

#ifdef G_OS_WIN32
#include <io.h>                 /* lseek, open, close, read */
#undef lseek
#define lseek _lseeki64
#undef off_t
#define off_t guint64
#else
#include <unistd.h>
#endif

GST_DEBUG_CATEGORY_EXTERN (ts_flow);
#define GST_CAT_DEFAULT (ts_flow)

#define DEBUG 0
#define DEBUG_DISK 0
#define DEBUG_RINGBUFFER 0

#define INVALID_OFFSET ((guint64) -1)

typedef struct _Slot Slot;
typedef struct _SlotMeta SlotMeta;

#if GST_CHECK_VERSION (1,0,0)
#define SLOT_META_INFO  (gst_slot_meta_get_info())
#define gst_buffer_get_slot_meta(b) ((SlotMeta*)gst_buffer_get_meta((b),SLOT_META_INFO))
#else
typedef SlotMeta GstSlotBuffer;
#define GST_TYPE_SLOT_BUFFER (gst_slot_buffer_get_type())
#define GST_IS_SLOT_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_SLOT_BUFFER))
#define GST_SLOT_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_SLOT_BUFFER, GstSlotBuffer))
#define GST_SLOT_BUFFER_CAST(obj) ((GstSlotBuffer *)(obj))

static GstBufferClass *gst_slot_buffer_parent_class = NULL;
#endif

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

  guint64 offset;
  guint8 *data;
  guint8 *wptr;
  guint32 size;
};

static inline gboolean
slot_available (Slot * slot, guint * size)
{
  CacheState state = g_atomic_int_get (&slot->state);

  if (state <= STATE_PART) {
    if (size) {
      *size = CACHE_SLOT_SIZE - (slot->wptr - slot->data);
    }
    return TRUE;
  }

  return FALSE;
}

static inline guint
slot_fullness (Slot * slot)
{
  return slot->size;
}

/* returns TRUE if slot is full */
static inline gboolean
slot_write (Slot * slot, guint8 * data, guint size, guint64 offset)
{
  slot->offset = offset;
  memcpy (slot->wptr, data, size);
#if DEBUG
  GST_LOG ("slot_write size %d", size);
//  gst_util_dump_mem (data, size);
#endif
  slot->wptr += size;
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
#if GST_CHECK_VERSION (1,0,0)
  GstMeta meta;
#else
  GstBuffer buffer;
#endif

  /* Reference to the cache we belong to */
  GstShifterCache *cache;
  Slot *slot;
};

static inline void
_slot_meta_init (SlotMeta * meta)
{
  meta->cache = NULL;
  meta->slot = NULL;
}

static inline void
_slot_meta_free (SlotMeta * meta)
{
  if (meta->slot)
    g_atomic_int_set (&meta->slot->state, STATE_RECYCLE);

  if (meta->cache)
    gst_shifter_cache_unref (meta->cache);
}

#if GST_CHECK_VERSION (1,0,0)

GType gst_slot_meta_api_get_type (void);
#define SLOT_META_API_TYPE  (gst_slot_meta_api_get_type())

static void
gst_slot_meta_init (SlotMeta * meta)
{
  _slot_meta_init (meta);
}

static void
gst_slot_meta_free (SlotMeta * meta, GstBuffer * buffer)
{
  _slot_meta_free (meta);
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
  GstBuffer * buffer = gst_buffer_new ();
  SlotMeta *meta;

  gst_buffer_append_memory (buffer,
      gst_memory_new_wrapped (GST_MEMORY_FLAG_READONLY,
          slot->data, slot->size, 0, slot->size, NULL, NULL));

  meta = (SlotMeta *) gst_buffer_add_meta (buffer, SLOT_META_INFO, NULL);
  meta->cache = gst_shifter_cache_ref (cache);
  meta->slot = slot;

  GST_BUFFER_OFFSET (buffer) = slot->offset;
  GST_BUFFER_OFFSET_END (buffer) = slot->offset + slot->size;

  return buffer;
}

#else
static void
gst_slot_buffer_finalize (GstSlotBuffer * buffer)
{
  _slot_meta_free (buffer);
}

static void
gst_slot_buffer_init (GstSlotBuffer * buffer, gpointer g_class)
{
  _slot_meta_init (buffer);
}

static void
gst_slot_buffer_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  gst_slot_buffer_parent_class = g_type_class_peek_parent (g_class);

  mini_object_class->finalize =
      (GstMiniObjectFinalizeFunction) gst_slot_buffer_finalize;
}

static GType
gst_slot_buffer_get_type (void)
{
  static GType _gst_slot_buffer_type;

  if (G_UNLIKELY (_gst_slot_buffer_type == 0)) {
    static const GTypeInfo slot_buffer_info = {
      sizeof (GstBufferClass),
      NULL,
      NULL,
      gst_slot_buffer_class_init,
      NULL,
      NULL,
      sizeof (GstSlotBuffer),
      0,
      (GInstanceInitFunc) gst_slot_buffer_init,
      NULL
    };
    _gst_slot_buffer_type = g_type_register_static (GST_TYPE_BUFFER,
        "GstSlotBuffer", &slot_buffer_info, 0);
  }
  return _gst_slot_buffer_type;
}

static GstBuffer *
gst_slot_buffer_new (GstShifterCache * cache, Slot * slot)
{
  GstSlotBuffer *buffer = NULL;

  buffer = GST_SLOT_BUFFER_CAST (gst_mini_object_new (GST_TYPE_SLOT_BUFFER));

  if (buffer) {
    buffer->cache = gst_shifter_cache_ref (cache);
    buffer->slot = slot;

    GST_BUFFER_DATA (buffer) = slot->data;
    GST_BUFFER_SIZE (buffer) = slot->size;
    GST_BUFFER_OFFSET (buffer) = slot->offset;
    GST_BUFFER_OFFSET_END (buffer) = slot->offset + slot->size;
  }

  return GST_BUFFER_CAST (buffer);
}
#endif

/* GstShifterCache */

struct _GstShifterCache
{
  volatile gint refcount;

  GMutex *lock;

  guint64 h_offset;             /* highest offset */
  guint64 l_rb_offset;          /* lowest offset in the ringbuffer */
  guint64 h_rb_offset;          /* highest offset in the ringbuffer (FULL slots) */
  guint64 l_dk_offset;          /* lowest offset in the disk */
  guint64 h_dk_offset;          /* highest offset in the disk */
  guint64 m_dk_offset;          /* offset migrated to the disk */

  gboolean need_discont;

  /* ring buffer */
  guint nslots;
  volatile gint fslots;         /* number of full slots */
  guint8 *memory;
  Slot *slots;

  guint head;
  guint tail;

  /* disk */
  FILE *file;
  gchar *filename_template;
  gchar *filename;
  gboolean autoremove;
  gsize w_dk_pos;               /* disk position in bytes where data is written */
  gsize r_dk_pos;               /* disk position in bytes where data is read */
  gsize m_dk_pos;               /* disk position in bytes where data is migratted */

  gboolean is_recording;
  gboolean is_rb_migrated;
  gboolean stop_recording;

  GThread *thread;              /* thread for async migration */

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
      { "EMPTY   ", "PART    ", "RECYCLE ", "FULL    ", "POP     " };
  gint i;
  GST_DEBUG ("---> %s \t head: %d tail: %d nslots: %d fslots: %d",
      title, cache->head, cache->tail, cache->nslots,
      g_atomic_int_get (&cache->fslots));
  GST_DEBUG ("     h_rb_offset %" G_GUINT64_FORMAT " l_rb_offset %"
      G_GUINT64_FORMAT " h_offset %" G_GUINT64_FORMAT " h_dk_offset %"
      G_GUINT64_FORMAT " l_dk_offset %" G_GUINT64_FORMAT,
      cache->h_rb_offset, cache->l_rb_offset,
      cache->h_offset, cache->h_dk_offset, cache->l_dk_offset);

  for (i = 0; i < cache->nslots; i++) {
    Slot *slot = &cache->slots[i];
    CacheState state = g_atomic_int_get (&slot->state);
    GST_LOG ("     %d. %s data %p wptr %p size %" G_GUINT32_FORMAT " offset %"
        G_GUINT64_FORMAT, i, state_names[state], slot->data, slot->wptr,
        slot->size, slot->offset);
  }
}

static inline gboolean
gst_shifter_cache_disk_open (GstShifterCache * cache)
{
  gint fd = -1;
  gchar *name = NULL;

  if (cache->file)
    return TRUE;

  if (cache->filename_template == NULL)
    return FALSE;

  /* make copy of the template, we don't want to change this */
  name = g_strdup (cache->filename_template);
  fd = g_mkstemp (name);
  if (fd == -1)
    return FALSE;

  GST_CACHE_LOCK (cache);
  /* open the file for update/writing */
  cache->file = fdopen (fd, "wb+");
  GST_CACHE_UNLOCK (cache);
  /* error creating file */
  if (cache->file == NULL) {
    g_free (name);
    if (fd != -1)
      close (fd);
    return FALSE;
  }

  g_free (cache->filename);
  cache->filename = name;

  return TRUE;
}

static inline void
gst_shifter_cache_disk_close (GstShifterCache * cache)
{
  /* nothing to do */
  GST_CACHE_LOCK (cache);
  if (cache->file == NULL) {
    goto beach;
  }
  fflush (cache->file);
  fclose (cache->file);
  if (cache->autoremove) {
    remove (cache->filename);
    g_free (cache->filename);
    cache->filename = NULL;
  }
  cache->file = NULL;
beach:
  GST_CACHE_UNLOCK (cache);
}

#ifdef HAVE_FSEEKO
#define FSEEK_FILE(file,offset)  (fseeko (file, (off_t) offset, SEEK_SET) != 0)
#elif defined (G_OS_UNIX) || defined (G_OS_WIN32)
#define FSEEK_FILE(file,offset)  (lseek (fileno (file), (off_t) offset, SEEK_SET) == (off_t) -1)
#else
#define FSEEK_FILE(file,offset)  (fseek (file, (long) offset, SEEK_SET) != 0)
#endif

static inline gboolean
gst_shifter_cache_disk_write (GstShifterCache * cache, guint8 * data,
    guint size)
{
  gboolean ret = FALSE;

  g_return_val_if_fail (cache->file, FALSE);

  GST_CACHE_LOCK (cache);

#if DEBUG_DISK
  GST_LOG ("pre  disk_write: dw %" G_GSIZE_FORMAT " dr: %" G_GSIZE_FORMAT,
      cache->w_dk_pos, cache->r_dk_pos);
#endif

  if (FSEEK_FILE (cache->file, cache->w_dk_pos)) {
    goto beach;
  }
  ret = fwrite (data, size, 1, cache->file);
  if (ret) {
    cache->w_dk_pos += size;
  }
  fflush (cache->file);
  cache->h_offset += size;
  cache->h_dk_offset = cache->h_offset;

#if DEBUG_DISK
  GST_LOG ("post disk_write: dw %" G_GSIZE_FORMAT " dr: %" G_GSIZE_FORMAT,
      cache->w_dk_pos, cache->r_dk_pos);
#endif

beach:
  GST_CACHE_UNLOCK (cache);
  return ret;
}

static inline gboolean
gst_shifter_cache_disk_read (GstShifterCache * cache, Slot * slot,
    guint64 offset, gboolean drain)
{
  gboolean ret = FALSE;
  gsize size;

  g_return_val_if_fail (cache->file, FALSE);

  size = MIN (cache->w_dk_pos - cache->r_dk_pos, CACHE_SLOT_SIZE);

#if DEBUG_DISK
  GST_LOG ("pre  disk_read: dw %" G_GSIZE_FORMAT " dr: %" G_GSIZE_FORMAT,
      cache->w_dk_pos, cache->r_dk_pos);
#endif

  if (G_UNLIKELY (size == 0)) {
    return FALSE;
  }

  if (!drain && size < CACHE_SLOT_SIZE)
    return FALSE;

  if (!slot_available (slot, NULL))
    return FALSE;

  GST_CACHE_LOCK (cache);
  if (FSEEK_FILE (cache->file, cache->r_dk_pos)) {
    goto beach;
  }

  ret = fread (slot->data, size, 1, cache->file);
  if (ret) {
    slot->offset = offset;
    slot->size = size;
    g_atomic_int_set (&slot->state, STATE_FULL);
    cache->r_dk_pos += size;
  }
#if DEBUG_DISK
  GST_LOG ("post disk_read: dw %" G_GSIZE_FORMAT " dr: %" G_GSIZE_FORMAT,
      cache->w_dk_pos, cache->r_dk_pos);
#endif

beach:
  GST_CACHE_UNLOCK (cache);
  return ret;
}

static inline void
gst_shifter_cache_flush (GstShifterCache * cache)
{
  guint i;
  for (i = 0; i < cache->nslots; i++) {
    Slot *slot = &cache->slots[i];
    slot->state = STATE_EMPTY;
    slot->offset = INVALID_OFFSET;
    slot->wptr = slot->data = cache->memory + (i * CACHE_SLOT_SIZE);
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
gst_shifter_cache_new (gsize size, gchar * filename_template)
{
  GstShifterCache *cache;
  guint nslots;

  cache = g_new (GstShifterCache, 1);

  cache->refcount = 1;

  cache->lock = g_mutex_new ();

  cache->h_offset = 0;
  cache->l_rb_offset = 0;
  cache->h_rb_offset = 0;
  cache->l_dk_offset = INVALID_OFFSET;
  cache->h_dk_offset = INVALID_OFFSET;
  cache->m_dk_offset = INVALID_OFFSET;

  /* Disk */
  cache->file = NULL;
  cache->filename_template = g_strdup (filename_template);
  cache->filename = NULL;
  cache->autoremove = TRUE;
  cache->w_dk_pos = 0;
  cache->r_dk_pos = 0;
  cache->m_dk_pos = 0;
  cache->is_recording = FALSE;
  cache->is_rb_migrated = FALSE;
  cache->stop_recording = FALSE;
  cache->thread = NULL;
  gst_shifter_cache_disk_open (cache);

  /* Ring buffer */
  nslots = size / CACHE_SLOT_SIZE;
  cache->nslots = nslots;
  cache->memory = g_malloc (nslots * CACHE_SLOT_SIZE);

  cache->slots = (Slot *) g_new (Slot, nslots);

  gst_shifter_cache_flush (cache);

#if !GST_CHECK_VERSION (1,0,0)
  g_type_class_ref (gst_slot_buffer_get_type ());
#endif

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
  gst_shifter_cache_disk_close (cache);
  g_free (cache->filename_template);
  g_free (cache->filename);

  g_free (cache->memory);
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
    if (slot->offset != INVALID_OFFSET)
      cache->l_rb_offset = slot->offset + slot->size;
    slot->offset = INVALID_OFFSET;
    slot->size = 0;
    slot->wptr = slot->data;
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

static void
gst_shifter_cache_migration_thread (GstShifterCache * cache)
{
  guint i, j = cache->tail;

  for (i = 0; i < cache->nslots; i++) {
    Slot *slot = &cache->slots[j];
    j = (j + 1) % cache->nslots;
    if (g_atomic_int_get (&slot->state) == STATE_EMPTY) {
      continue;
    }

    GST_CACHE_LOCK (cache);
    if (G_UNLIKELY (cache->stop_recording && cache->autoremove)) {
      GST_INFO ("ring buffer migration aborted");
      goto beach;
    }

    if (!FSEEK_FILE (cache->file, cache->m_dk_pos)) {
      if (fwrite (slot->data, slot->size, 1, cache->file)) {
        cache->m_dk_pos += slot->size;
        fflush (cache->file);
      }
    }
    GST_CACHE_UNLOCK (cache);
    if ((i % 8) == 0) {
      /* Ensure other threads are scheduled */
      g_thread_yield ();
    }
  }
  GST_CACHE_LOCK (cache);
  cache->l_dk_offset = cache->m_dk_offset;
  cache->is_rb_migrated = TRUE;

  GST_INFO ("ring buffer migration finished in %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_CLOCK_DIFF (cache->mtime, gst_util_get_timestamp ())));
  dump_cache_state (cache, "post-migration");

beach:
  GST_CACHE_UNLOCK (cache);
  gst_shifter_cache_unref (cache);
}

gboolean
gst_shifter_cache_start_recording (GstShifterCache * cache)
{
  GError *error = NULL;
  gboolean ret = TRUE;

  g_return_val_if_fail (cache->file, FALSE);

  GST_CACHE_LOCK (cache);
  if (G_LIKELY (cache->thread != NULL)) {
    goto beach;                 /* Thread already running. Nothing to do */
  }
  if (G_UNLIKELY (cache->stop_recording)) {
    ret = FALSE;
    goto beach;
  }
  cache->m_dk_offset = cache->l_rb_offset;
  cache->l_dk_offset = cache->h_offset;
  cache->w_dk_pos = cache->h_offset - cache->l_rb_offset;
  cache->mtime = gst_util_get_timestamp ();
  cache->is_recording = TRUE;
  GST_INFO ("ring buffer migration started");
  dump_cache_state (cache, "pre-migration");

  cache->thread =
      g_thread_create ((GThreadFunc) gst_shifter_cache_migration_thread,
      gst_shifter_cache_ref (cache), TRUE, &error);

  if (G_UNLIKELY (error))
    goto no_thread;

beach:
  GST_CACHE_UNLOCK (cache);
  return ret;

  /* ERRORS */
no_thread:
  {
    GST_CACHE_UNLOCK (cache);
    GST_ERROR ("could not create migration thread: %s", error->message);
    g_error_free (error);
  }
  return FALSE;
}

void
gst_shifter_cache_stop_recording (GstShifterCache * cache)
{
  GST_CACHE_LOCK (cache);
  cache->stop_recording = TRUE;
  GST_CACHE_UNLOCK (cache);

  /* Ensure that the ringbuffer is migrated to the file */
  if (cache->thread) {
    g_thread_join (cache->thread);
  }
  GST_CACHE_LOCK (cache);
  cache->thread = NULL;
  cache->is_recording = FALSE;
  GST_CACHE_UNLOCK (cache);
}

/* Try to refill the ringbuffer with data from the disk */
static inline void
gst_shifter_cache_reload (GstShifterCache * cache, gboolean drain)
{
  guint i, n = 1;

  if (!drain) {
    /* we want to try keep the fullness on the ringbuffer to achieve it we
     * refill at log2(emptiness) to fill it faster */
    n = g_bit_storage (cache->nslots - g_atomic_int_get (&cache->fslots));
  }

  for (i = 0; i < n; i++) {
    Slot *tail = &cache->slots[cache->tail];
    gst_shifter_cache_recycle (cache, tail);
    if (gst_shifter_cache_disk_read (cache, tail, cache->h_rb_offset, drain)) {
      cache->tail = (cache->tail + 1) % cache->nslots;
      cache->h_rb_offset = tail->offset + tail->size;
      g_atomic_int_inc (&cache->fslots);
    } else {
      break;
    }
  }
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
      cache->h_rb_offset = head->offset + head->size;
      g_atomic_int_inc (&cache->fslots);
    }
  }

  pop = g_atomic_int_compare_and_exchange (&head->state, STATE_FULL, STATE_POP);

  if (pop) {
    gboolean is_recording, is_rb_migrated;

    GST_CACHE_LOCK (cache);
    is_recording = cache->is_recording;
    is_rb_migrated = cache->is_rb_migrated;
    GST_CACHE_UNLOCK (cache);

    g_atomic_int_add (&cache->fslots, -1);
    buffer = GST_BUFFER_CAST (gst_slot_buffer_new (cache, head));
    if (cache->need_discont) {
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
      cache->need_discont = FALSE;
    }

    cache->head = (cache->head + 1) % cache->nslots;
    if (is_recording && is_rb_migrated) {
      gst_shifter_cache_reload (cache, drain);
    }
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

void
gst_shifter_cache_push (GstShifterCache * cache, guint8 *data, gsize size)
{
  Slot *tail;
  gsize avail;
  gboolean is_recording;

  GST_CACHE_LOCK (cache);
  is_recording = cache->is_recording;
  GST_CACHE_UNLOCK (cache);

#if DEBUG_RINGBUFFER
  dump_cache_state (cache, "pre-push");
#endif

  if (is_recording) {
    gst_shifter_cache_disk_write (cache, data, size);
    /* handle underruns by refilling the ringbuffer */
    if (gst_shifter_cache_is_empty (cache)) {
      gst_shifter_cache_reload (cache, FALSE);
    }
  } else {
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
        gst_shifter_cache_start_recording (cache);
        gst_shifter_cache_disk_write (cache, data, size);
        size = 0;
      }
    }
  }
#if DEBUG_RINGBUFFER
  dump_cache_state (cache, "post-push");
#endif
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
  gboolean is_disk_usable;
  gboolean ret;

  GST_CACHE_LOCK (cache);
  dump_cache_state (cache, "has_offset");

  is_disk_usable = cache->is_recording && cache->is_rb_migrated;

  /* clamp to a min/max valid offset */
  if (is_disk_usable) {
    ret = (offset >= cache->l_dk_offset && offset < cache->h_dk_offset);
  } else {
    ret = (offset >= cache->l_rb_offset && offset < cache->h_offset);
  }
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
  gboolean is_disk_usable;

  GST_DEBUG ("requested seek at offset: %" G_GUINT64_FORMAT, offset);

  GST_CACHE_LOCK (cache);
  is_disk_usable = cache->is_recording && cache->is_rb_migrated;
  dump_cache_state (cache, "pre-seek");

  /* clamp to a min/max valid offset */
  if (is_disk_usable) {
    offset = MIN (offset, cache->h_dk_offset);
    offset = MAX (offset, cache->l_dk_offset);
  } else {
    offset = MIN (offset, cache->h_rb_offset);
    offset = MAX (offset, cache->l_rb_offset);
  }
  GST_CACHE_UNLOCK (cache);

  GST_DEBUG ("seeking for offset: %" G_GUINT64_FORMAT, offset);

  /* First check if we can find it in the ringbuffer */
  if (offset >= cache->l_rb_offset && offset < cache->h_rb_offset) {
    GST_DEBUG ("seeking in the ringbuffer");
    guint seeker = cache->head;
    head = &cache->slots[seeker];

    if (offset >= head->offset) {
      if (offset < head->offset + head->size) {
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
      } while (!(offset >= head->offset && offset < head->offset + head->size));
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
      } while (!(offset >= head->offset && offset < head->offset + head->size));
    }
    cache->head = seeker;
    goto beach;
  }

  if (is_disk_usable) {
    /* requested position is in the disk */
    GST_DEBUG ("seeking in disk");

    /* flush the ring buffer */
    gst_shifter_cache_flush (cache);

    /* update the reading position in the disk */
    GST_CACHE_LOCK (cache);
    cache->r_dk_pos = offset - cache->l_dk_offset;
    cache->h_rb_offset = cache->l_rb_offset = offset;
    GST_CACHE_UNLOCK (cache);

    /* reload data into the ringbuffer */
    gst_shifter_cache_reload (cache, FALSE);
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
    return (tail->offset - head->offset + head->size);
  }
}

/**
 * gst_shifter_cache_is_recording:
 * @cache: a #GstShifterCache
 *
 * Return TRUE if cache is using the disk.
 *
 */
gboolean
gst_shifter_cache_is_recording (GstShifterCache * cache)
{
  gboolean ret;
  g_return_val_if_fail (cache != NULL, FALSE);

  GST_CACHE_LOCK (cache);
  ret = cache->is_recording;
  GST_CACHE_UNLOCK (cache);
  return ret;
}

/**
 * gst_shifter_cache_get_filename:
 * @cache: a #GstShifterCache
 *
 * Return a pointer with the filename created to extend the ringbuffer.
 *
 */
gchar *
gst_shifter_cache_get_filename (GstShifterCache * cache)
{
  g_return_val_if_fail (cache != NULL, NULL);

  return cache->filename;
}

/**
 * gst_shifter_cache_get_autoremove:
 * @cache: a #GstShifterCache
 *
 * Return a gboolean that describes if cache file is going to be autoremvoced
 * on close.
 *
 */
gboolean
gst_shifter_cache_get_autoremove (GstShifterCache * cache)
{
  g_return_val_if_fail (cache != NULL, FALSE);

  return cache->autoremove;
}

/**
 * gst_shifter_cache_set_autoremove:
 * @cache: a #GstShifterCache
 * @autoremove: a #gboolean
 *
 * Defines if cache file is going to be autoremvoced on close.
 *
 */
void
gst_shifter_cache_set_autoremove (GstShifterCache * cache, gboolean autoremove)
{
  g_return_if_fail (cache != NULL);

  cache->autoremove = autoremove;
}
