/* GStreamer
 * Copyright (C) 2013 FIXME <fixme@example.com>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gsttimeshiftseeker
 *
 * The timeshiftseeker element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v fakesrc ! timeshiftseeker ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/gstelement.h>
#include <gst/base/gstbasetransform.h>
#include "gsttimeshiftseeker.h"

GST_DEBUG_CATEGORY_STATIC (gst_time_shift_seeker_debug_category);
#define GST_CAT_DEFAULT gst_time_shift_seeker_debug_category
#define parent_class GST_TYPE_BASE_TRANSFORM

/* prototypes */


static void gst_time_shift_seeker_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_time_shift_seeker_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_time_shift_seeker_dispose (GObject * object);
static void gst_time_shift_seeker_finalize (GObject * object);

static GstCaps *gst_time_shift_seeker_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *
gst_time_shift_seeker_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean
gst_time_shift_seeker_transform_size (GstBaseTransform * trans,
    GstPadDirection direction,
    GstCaps * caps, guint size, GstCaps * othercaps, guint * othersize);
static gboolean
gst_time_shift_seeker_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    guint * size);
static gboolean
gst_time_shift_seeker_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps);
static gboolean gst_time_shift_seeker_start (GstBaseTransform * trans);
static gboolean gst_time_shift_seeker_stop (GstBaseTransform * trans);
static gboolean
gst_time_shift_seeker_sink_event (GstBaseTransform * trans, GstEvent * event);
static GstFlowReturn
gst_time_shift_seeker_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf);
static GstFlowReturn
gst_time_shift_seeker_transform_ip (GstBaseTransform * trans, GstBuffer * buf);
static GstFlowReturn
gst_time_shift_seeker_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer * input, GstBuffer ** buf);
static gboolean
gst_time_shift_seeker_src_event (GstBaseTransform * trans, GstEvent * event);
static void
gst_time_shift_seeker_before_transform (GstBaseTransform * trans, GstBuffer * buffer);

enum
{
  PROP_0
};

/* pad templates */

static GstStaticPadTemplate gst_time_shift_seeker_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/unknown")
    );

static GstStaticPadTemplate gst_time_shift_seeker_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/unknown")
    );


/* class initialization */

G_DEFINE_TYPE (GstTimeShiftSeeker, gst_time_shift_seeker, GST_TYPE_BASE_TRANSFORM);

static void
gst_time_shift_seeker_class_init (GstTimeShiftSeekerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_time_shift_seeker_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_time_shift_seeker_src_template));

  gst_element_class_set_static_metadata (element_class, "FIXME Long name",
      "Generic", "FIXME Description", "FIXME <fixme@example.com>");

  GST_DEBUG_CATEGORY_INIT (gst_time_shift_seeker_debug_category,
      "gst_time_shift_seeker", 0, "FIXME Description");


  gobject_class->set_property = gst_time_shift_seeker_set_property;
  gobject_class->get_property = gst_time_shift_seeker_get_property;
  gobject_class->dispose = gst_time_shift_seeker_dispose;
  gobject_class->finalize = gst_time_shift_seeker_finalize;
  base_transform_class->transform_caps = GST_DEBUG_FUNCPTR (gst_time_shift_seeker_transform_caps);
  base_transform_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_time_shift_seeker_fixate_caps);
  base_transform_class->transform_size = GST_DEBUG_FUNCPTR (gst_time_shift_seeker_transform_size);
  base_transform_class->get_unit_size = GST_DEBUG_FUNCPTR (gst_time_shift_seeker_get_unit_size);
  base_transform_class->set_caps = GST_DEBUG_FUNCPTR (gst_time_shift_seeker_set_caps);
  base_transform_class->start = GST_DEBUG_FUNCPTR (gst_time_shift_seeker_start);
  base_transform_class->stop = GST_DEBUG_FUNCPTR (gst_time_shift_seeker_stop);
  base_transform_class->sink_event = GST_DEBUG_FUNCPTR (gst_time_shift_seeker_sink_event);
  base_transform_class->transform = GST_DEBUG_FUNCPTR (gst_time_shift_seeker_transform);
  base_transform_class->transform_ip = GST_DEBUG_FUNCPTR (gst_time_shift_seeker_transform_ip);
  base_transform_class->prepare_output_buffer = GST_DEBUG_FUNCPTR (gst_time_shift_seeker_prepare_output_buffer);
  base_transform_class->src_event = GST_DEBUG_FUNCPTR (gst_time_shift_seeker_src_event);
  base_transform_class->before_transform = GST_DEBUG_FUNCPTR (gst_time_shift_seeker_before_transform);

}

static void
gst_time_shift_seeker_init (GstTimeShiftSeeker * timeshiftseeker)
{

  timeshiftseeker->sinkpad = gst_pad_new_from_static_template (&gst_time_shift_seeker_sink_template
      ,     
            "sink");

  timeshiftseeker->srcpad = gst_pad_new_from_static_template (&gst_time_shift_seeker_src_template
      ,     
            "src");
}

void
gst_time_shift_seeker_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  /* GstTimeShiftSeeker *timeshiftseeker = GST_TIME_SHIFT_SEEKER (object); */

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_time_shift_seeker_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  /* GstTimeShiftSeeker *timeshiftseeker = GST_TIME_SHIFT_SEEKER (object); */

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_time_shift_seeker_dispose (GObject * object)
{
  /* GstTimeShiftSeeker *timeshiftseeker = GST_TIME_SHIFT_SEEKER (object); */

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_time_shift_seeker_finalize (GObject * object)
{
  /* GstTimeShiftSeeker *timeshiftseeker = GST_TIME_SHIFT_SEEKER (object); */

  /* clean up object here */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static GstCaps *
gst_time_shift_seeker_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{

  return NULL;
}

static GstCaps *
gst_time_shift_seeker_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{

  return othercaps;
}

static gboolean
gst_time_shift_seeker_transform_size (GstBaseTransform * trans,
    GstPadDirection direction,
    GstCaps * caps, guint size, GstCaps * othercaps, guint * othersize)
{

  return FALSE;
}

static gboolean
gst_time_shift_seeker_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    guint * size)
{

  return FALSE;
}

static gboolean
gst_time_shift_seeker_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{

  return FALSE;
}

static gboolean
gst_time_shift_seeker_start (GstBaseTransform * trans)
{

  return FALSE;
}

static gboolean
gst_time_shift_seeker_stop (GstBaseTransform * trans)
{

  return FALSE;
}

static gboolean
gst_time_shift_seeker_sink_event (GstBaseTransform * trans, GstEvent * event)
{

  return FALSE;
}

static GstFlowReturn
gst_time_shift_seeker_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{

  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_time_shift_seeker_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{

  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_time_shift_seeker_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer * input, GstBuffer ** buf)
{

  return GST_FLOW_ERROR;
}

static gboolean
gst_time_shift_seeker_src_event (GstBaseTransform * trans, GstEvent * event)
{

  return FALSE;
}

static void
gst_time_shift_seeker_before_transform (GstBaseTransform * trans, GstBuffer * buffer)
{

}

