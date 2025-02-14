/* GStreamer Wayland video sink
 *
 * Copyright (C) 2011 Intel Corporation
 * Copyright (C) 2011 Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 * Copyright (C) 2012 Wim Taymans <wim.taymans@gmail.com>
 * Copyright (C) 2014 Collabora Ltd.
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
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

/**
 * SECTION:element-waylandsink
 * @title: waylandsink
 *
 *  The waylandsink is creating its own window and render the decoded video frames to that.
 *  Setup the Wayland environment as described in
 *  [Wayland](http://wayland.freedesktop.org/building.html) home page.
 *
 *  The current implementation is based on weston compositor.
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 -v videotestsrc ! waylandsink
 * ]| test the video rendering in wayland
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstwaylandsink.h"
#include <gst/allocators/allocators.h>

#include <gst/video/videooverlay.h>
#include <gst/video/video.h>

/* signals */
enum
{
  SIGNAL_0,
  LAST_SIGNAL
};

/* Properties */
enum
{
  PROP_0,
  PROP_DISPLAY,
  PROP_FULLSCREEN,
  PROP_USE_SUBSURFACE,
  PROP_WAYLAND_POSITION_X,    /* add property (position_x) */
  PROP_WAYLAND_POSITION_Y,    /* add property (position_y) */
  PROP_WAYLAND_OUTPUT_WIDTH,    /* add property (out_w) */
  PROP_WAYLAND_OUTPUT_HEIGHT,   /* add property (out_h) */
  PROP_SUPPRESS_INTERLACE,
  PROP_ROTATE_METHOD,
  PROP_LAST
};

#define DEFAULT_USE_SUBSURFACE          TRUE
#define DEFAULT_SUPPRESS_INTERLACE      TRUE

GST_DEBUG_CATEGORY (gstwayland_debug);
#define GST_CAT_DEFAULT gstwayland_debug

#define WL_VIDEO_FORMATS \
    "{ BGRx, BGRA, RGBx, xBGR, xRGB, RGBA, ABGR, ARGB, RGB, BGR, " \
    "RGB16, BGR16, YUY2, YVYU, UYVY, AYUV, NV12, NV21, NV16, NV61, " \
    "YUV9, YVU9, Y41B, I420, YV12, Y42B, v308 }"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (WL_VIDEO_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_DMABUF,
            WL_VIDEO_FORMATS))
    );

static void gst_wayland_sink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_wayland_sink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_wayland_sink_finalize (GObject * object);

static GstStateChangeReturn gst_wayland_sink_change_state (GstElement * element,
    GstStateChange transition);
static void gst_wayland_sink_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_wayland_sink_event (GstBaseSink * bsink, GstEvent * event);
static GstCaps *gst_wayland_sink_get_caps (GstBaseSink * bsink,
    GstCaps * filter);
static gboolean gst_wayland_sink_set_caps (GstBaseSink * bsink, GstCaps * caps);
static gboolean
gst_wayland_sink_propose_allocation (GstBaseSink * bsink, GstQuery * query);
static GstFlowReturn gst_wayland_sink_show_frame (GstVideoSink * vsink,
    GstBuffer * buffer);

/* VideoOverlay interface */
static void gst_wayland_sink_videooverlay_init (GstVideoOverlayInterface *
    iface);
static void gst_wayland_sink_set_window_handle (GstVideoOverlay * overlay,
    guintptr handle);
static void gst_wayland_sink_set_render_rectangle (GstVideoOverlay * overlay,
    gint x, gint y, gint w, gint h);
static void gst_wayland_sink_expose (GstVideoOverlay * overlay);

#define gst_wayland_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstWaylandSink, gst_wayland_sink, GST_TYPE_VIDEO_SINK,
    G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_OVERLAY,
        gst_wayland_sink_videooverlay_init));
GST_ELEMENT_REGISTER_DEFINE (waylandsink, "waylandsink", GST_RANK_MARGINAL,
    GST_TYPE_WAYLAND_SINK);

static void
gst_wayland_sink_class_init (GstWaylandSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstVideoSinkClass *gstvideosink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstvideosink_class = (GstVideoSinkClass *) klass;

  gobject_class->set_property = gst_wayland_sink_set_property;
  gobject_class->get_property = gst_wayland_sink_get_property;
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_wayland_sink_finalize);

  gst_element_class_add_static_pad_template (gstelement_class, &sink_template);

  gst_element_class_set_static_metadata (gstelement_class,
      "wayland video sink", "Sink/Video",
      "Output to wayland surface",
      "Sreerenj Balachandran <sreerenj.balachandran@intel.com>, "
      "George Kiagiadakis <george.kiagiadakis@collabora.com>");

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_wayland_sink_change_state);
  gstelement_class->set_context =
      GST_DEBUG_FUNCPTR (gst_wayland_sink_set_context);

  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_wayland_sink_event);
  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_wayland_sink_get_caps);
  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_wayland_sink_set_caps);
  gstbasesink_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_wayland_sink_propose_allocation);

  gstvideosink_class->show_frame =
      GST_DEBUG_FUNCPTR (gst_wayland_sink_show_frame);

  g_object_class_install_property (gobject_class, PROP_DISPLAY,
      g_param_spec_string ("display", "Wayland Display name", "Wayland "
          "display name to connect to, if not supplied via the GstContext",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FULLSCREEN,
      g_param_spec_boolean ("fullscreen", "Fullscreen",
          "Whether the surface should be made fullscreen ", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_USE_SUBSURFACE,
      g_param_spec_boolean ("use-subsurface", "Use Subsurface",
          "NOP and deprecated", DEFAULT_USE_SUBSURFACE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_DEPRECATED));

  /* install property (position_x) */
  g_object_class_install_property (G_OBJECT_CLASS(klass), PROP_WAYLAND_POSITION_X,
      g_param_spec_int ("position_x", "Position_X",
                        "Wayland  Position X value from the application ",
                        0, G_MAXINT, 0, G_PARAM_READWRITE));

  /* install property (position_y) */
  g_object_class_install_property (G_OBJECT_CLASS(klass), PROP_WAYLAND_POSITION_Y,
      g_param_spec_int ("position_y", "Position_Y",
                        "Wayland  Position Y value from the application ",
                        0, G_MAXINT, 0, G_PARAM_READWRITE));

  /* install property (out_w) */
  g_object_class_install_property (G_OBJECT_CLASS(klass), PROP_WAYLAND_OUTPUT_WIDTH,
      g_param_spec_int ("out_w", "Output Width",
                        "Wayland  Width size of application ",
                        0, G_MAXINT, 0, G_PARAM_READWRITE));

  /* install property (out_h) */
  g_object_class_install_property (G_OBJECT_CLASS(klass), PROP_WAYLAND_OUTPUT_HEIGHT,
      g_param_spec_int ("out_h", "Output Height",
                        "Wayland  Height size of application ",
                        0, G_MAXINT, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SUPPRESS_INTERLACE,
      g_param_spec_boolean ("suppress-interlace", "Suppress Interlace",
          "When enabled, dmabuf are created without flag of interlaced buffer",
          DEFAULT_SUPPRESS_INTERLACE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * waylandsink:rotate-method:
   *
   * Since: 1.22
   */
  g_object_class_install_property (gobject_class, PROP_ROTATE_METHOD,
      g_param_spec_enum ("rotate-method",
          "rotate method",
          "rotate method",
          GST_TYPE_VIDEO_ORIENTATION_METHOD, GST_VIDEO_ORIENTATION_IDENTITY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

 /**
  * waylandsink:render-rectangle:
  *
  * This helper installs the "render-rectangle" property into the
  * class.
  *
  * Since: 1.22
  */
  gst_video_overlay_install_properties (gobject_class, PROP_LAST);
}

static void
gst_wayland_sink_init (GstWaylandSink * self)
{
  g_mutex_init (&self->display_lock);
  g_mutex_init (&self->render_lock);

  self->use_subsurface = DEFAULT_USE_SUBSURFACE;
  self->enable_interlace = !DEFAULT_SUPPRESS_INTERLACE;
  self->position_x = -1;
  self->position_y = -1;
  self->out_w = -1;
  self->out_h = -1;

}

static void
gst_wayland_sink_set_fullscreen (GstWaylandSink * self, gboolean fullscreen)
{
  if (fullscreen == self->fullscreen)
    return;

  g_mutex_lock (&self->render_lock);
  self->fullscreen = fullscreen;
  gst_wl_window_ensure_fullscreen (self->window, fullscreen);
  g_mutex_unlock (&self->render_lock);
}

static void
gst_wayland_sink_set_rotate_method (GstWaylandSink * self,
    GstVideoOrientationMethod method, gboolean from_tag)
{
  GstVideoOrientationMethod new_method;

  if (method == GST_VIDEO_ORIENTATION_CUSTOM) {
    GST_WARNING_OBJECT (self, "unsupported custom orientation");
    return;
  }

  GST_OBJECT_LOCK (self);
  if (from_tag)
    self->tag_rotate_method = method;
  else
    self->sink_rotate_method = method;

  if (self->sink_rotate_method == GST_VIDEO_ORIENTATION_AUTO)
    new_method = self->tag_rotate_method;
  else
    new_method = self->sink_rotate_method;

  if (new_method != self->current_rotate_method) {
    GST_DEBUG_OBJECT (self, "Changing method from %d to %d",
        self->current_rotate_method, new_method);

    if (self->window) {
      g_mutex_lock (&self->render_lock);
      gst_wl_window_set_rotate_method (self->window, new_method);
      g_mutex_unlock (&self->render_lock);
    }

    self->current_rotate_method = new_method;
  }
  GST_OBJECT_UNLOCK (self);
}

static void
gst_wayland_sink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstWaylandSink *self = GST_WAYLAND_SINK (object);

  switch (prop_id) {
    case PROP_DISPLAY:
      GST_OBJECT_LOCK (self);
      g_value_set_string (value, self->display_name);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_USE_SUBSURFACE:
      GST_OBJECT_LOCK (self);
      g_value_set_boolean (value, self->use_subsurface);
      GST_OBJECT_UNLOCK (self);
    case PROP_SUPPRESS_INTERLACE:
      GST_OBJECT_LOCK (self);
      g_value_set_boolean (value, !self->enable_interlace);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_WAYLAND_POSITION_X:
      /* set position_x property */
      g_value_set_int (value, self->position_x);
      break;
    case PROP_WAYLAND_POSITION_Y:
      /* set position_y property */
      g_value_set_int (value, self->position_y);
      break;
    case PROP_WAYLAND_OUTPUT_WIDTH:
      /* set out_w property */
      g_value_set_int (value, self->out_w);
      break;
    case PROP_WAYLAND_OUTPUT_HEIGHT:
      /* set out_h property */
      g_value_set_int (value, self->out_h);
      break;
    case PROP_FULLSCREEN:
      GST_OBJECT_LOCK (self);
      g_value_set_boolean (value, self->fullscreen);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_ROTATE_METHOD:
      GST_OBJECT_LOCK (self);
      g_value_set_enum (value, self->current_rotate_method);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_wayland_sink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstWaylandSink *self = GST_WAYLAND_SINK (object);

  switch (prop_id) {
    case PROP_DISPLAY:
      GST_OBJECT_LOCK (self);
      self->display_name = g_value_dup_string (value);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_USE_SUBSURFACE:
      GST_WARNING_OBJECT (self, "The option \"use-subsurface\" is deprecated"
          "and this option is NOP");
      GST_OBJECT_LOCK (self);
      self->use_subsurface = g_value_get_boolean (value);
      GST_OBJECT_UNLOCK (self);
    case PROP_SUPPRESS_INTERLACE:
      GST_OBJECT_LOCK (self);
      self->enable_interlace = !g_value_get_boolean (value);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_WAYLAND_POSITION_X:
      /* get position_x property */
      self->position_x = g_value_get_int (value);
      break;
    case PROP_WAYLAND_POSITION_Y:
      /* get position_y property */
      self->position_y = g_value_get_int (value);
      break;
    case PROP_WAYLAND_OUTPUT_WIDTH:
      /* get out_w property */
      self->out_w = g_value_get_int (value);
      break;
    case PROP_WAYLAND_OUTPUT_HEIGHT:
      /* get out_h property */
      self->out_h = g_value_get_int (value);
      break;
    case PROP_FULLSCREEN:
      GST_OBJECT_LOCK (self);
      gst_wayland_sink_set_fullscreen (self, g_value_get_boolean (value));
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_ROTATE_METHOD:
      gst_wayland_sink_set_rotate_method (self, g_value_get_enum (value),
          FALSE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_wayland_sink_finalize (GObject * object)
{
  GstWaylandSink *self = GST_WAYLAND_SINK (object);

  GST_DEBUG_OBJECT (self, "Finalizing the sink..");

  if (self->last_buffer)
    gst_buffer_unref (self->last_buffer);
  if (self->display)
    g_object_unref (self->display);
  if (self->window)
    g_object_unref (self->window);
  if (self->pool)
    gst_object_unref (self->pool);

  g_free (self->display_name);

  g_mutex_clear (&self->display_lock);
  g_mutex_clear (&self->render_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* must be called with the display_lock */
static void
gst_wayland_sink_set_display_from_context (GstWaylandSink * self,
    GstContext * context)
{
  struct wl_display *display;
  GError *error = NULL;

  display = gst_wl_display_handle_context_get_handle (context);
  self->display = gst_wl_display_new_existing (display, FALSE, &error);

  if (error) {
    GST_ELEMENT_WARNING (self, RESOURCE, OPEN_READ_WRITE,
        ("Could not set display handle"),
        ("Failed to use the external wayland display: '%s'", error->message));
    g_error_free (error);
  }
}

static gboolean
gst_wayland_sink_find_display (GstWaylandSink * self)
{
  GstQuery *query;
  GstMessage *msg;
  GstContext *context = NULL;
  GError *error = NULL;
  gboolean ret = TRUE;

  g_mutex_lock (&self->display_lock);

  if (!self->display) {
    /* first query upstream for the needed display handle */
    query = gst_query_new_context (GST_WL_DISPLAY_HANDLE_CONTEXT_TYPE);
    if (gst_pad_peer_query (GST_VIDEO_SINK_PAD (self), query)) {
      gst_query_parse_context (query, &context);
      gst_wayland_sink_set_display_from_context (self, context);
    }
    gst_query_unref (query);

    if (G_LIKELY (!self->display)) {
      /* now ask the application to set the display handle */
      msg = gst_message_new_need_context (GST_OBJECT_CAST (self),
          GST_WL_DISPLAY_HANDLE_CONTEXT_TYPE);

      g_mutex_unlock (&self->display_lock);
      gst_element_post_message (GST_ELEMENT_CAST (self), msg);
      /* at this point we expect gst_wayland_sink_set_context
       * to get called and fill self->display */
      g_mutex_lock (&self->display_lock);

      if (!self->display) {
        /* if the application didn't set a display, let's create it ourselves */
        GST_OBJECT_LOCK (self);
        self->display = gst_wl_display_new (self->display_name, &error);
        GST_OBJECT_UNLOCK (self);

        if (error) {
          GST_ELEMENT_WARNING (self, RESOURCE, OPEN_READ_WRITE,
              ("Could not initialise Wayland output"),
              ("Failed to create GstWlDisplay: '%s'", error->message));
          g_error_free (error);
          ret = FALSE;
        }
      }
    }
  }

  g_mutex_unlock (&self->display_lock);

  return ret;
}

static GstStateChangeReturn
gst_wayland_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstWaylandSink *self = GST_WAYLAND_SINK (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_wayland_sink_find_display (self))
        return GST_STATE_CHANGE_FAILURE;

      /* the event queue specific for wl_surface_frame events */
      self->frame_queue = wl_display_create_queue (gst_wl_display_get_display (self->display));
      if (!self->frame_queue) {
        GST_ERROR_OBJECT (self, "failed to create wl_event_queue");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_buffer_replace (&self->last_buffer, NULL);
      if (self->window) {
        if (gst_wl_window_is_toplevel (self->window)) {
          g_clear_object (&self->window);
        } else {
          /* remove buffer from surface, show nothing */
          gst_wl_window_render (self->window, NULL, NULL);
        }
      }

      g_mutex_lock (&self->render_lock);
      if (self->callback) {
        wl_callback_destroy (self->callback);
        self->callback = NULL;
      }
      self->redraw_pending = FALSE;
      g_mutex_unlock (&self->render_lock);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      g_mutex_lock (&self->display_lock);
      /* If we had a toplevel window, we most likely have our own connection
       * to the display too, and it is a good idea to disconnect and allow
       * potentially the application to embed us with GstVideoOverlay
       * (which requires to re-use the same display connection as the parent
       * surface). If we didn't have a toplevel window, then the display
       * connection that we have is definitely shared with the application
       * and it's better to keep it around (together with the window handle)
       * to avoid requesting them again from the application if/when we are
       * restarted (GstVideoOverlay behaves like that in other sinks)
       */
      if (self->display && !self->window){       /* -> the window was toplevel */
        wl_event_queue_destroy (self->frame_queue);
        g_clear_object (&self->display);
      }
      g_mutex_unlock (&self->display_lock);
      g_clear_object (&self->pool);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_wayland_sink_set_context (GstElement * element, GstContext * context)
{
  GstWaylandSink *self = GST_WAYLAND_SINK (element);

  if (gst_context_has_context_type (context,
          GST_WL_DISPLAY_HANDLE_CONTEXT_TYPE)) {
    g_mutex_lock (&self->display_lock);
    if (G_LIKELY (!self->display)) {
      gst_wayland_sink_set_display_from_context (self, context);
    } else {
      GST_WARNING_OBJECT (element, "changing display handle is not supported");
      g_mutex_unlock (&self->display_lock);
      return;
    }
    g_mutex_unlock (&self->display_lock);
  }

  if (GST_ELEMENT_CLASS (parent_class)->set_context)
    GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_wayland_sink_event (GstBaseSink * bsink, GstEvent * event)
{
  GstWaylandSink *self = GST_WAYLAND_SINK (bsink);
  GstTagList *taglist;
  GstVideoOrientationMethod method;
  gboolean ret;

  GST_DEBUG_OBJECT (self, "handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:
      gst_event_parse_tag (event, &taglist);

      if (gst_video_orientation_from_tag (taglist, &method)) {
        gst_wayland_sink_set_rotate_method (self, method, TRUE);
      }

      break;
    default:
      break;
  }

  ret = GST_BASE_SINK_CLASS (parent_class)->event (bsink, event);

  return ret;
}

static GstCaps *
gst_wayland_sink_get_caps (GstBaseSink * bsink, GstCaps * filter)
{
  GstWaylandSink *self = GST_WAYLAND_SINK (bsink);;
  GstCaps *caps;

  caps = gst_pad_get_pad_template_caps (GST_VIDEO_SINK_PAD (self));
  caps = gst_caps_make_writable (caps);

  g_mutex_lock (&self->display_lock);

  if (self->display) {
    GValue shm_list = G_VALUE_INIT, dmabuf_list = G_VALUE_INIT;
    GValue value = G_VALUE_INIT;
    GArray *formats;
    gint i;
    guint fmt;
    GstVideoFormat gfmt;

    g_value_init (&shm_list, GST_TYPE_LIST);
    g_value_init (&dmabuf_list, GST_TYPE_LIST);

    /* Add corresponding shm formats */
    formats = gst_wl_display_get_shm_formats (self->display);
    for (i = 0; i < formats->len; i++) {
      fmt = g_array_index (formats, uint32_t, i);
      gfmt = gst_wl_shm_format_to_video_format (fmt);
      if (gfmt != GST_VIDEO_FORMAT_UNKNOWN) {
        g_value_init (&value, G_TYPE_STRING);
        g_value_set_static_string (&value, gst_video_format_to_string (gfmt));
        gst_value_list_append_and_take_value (&shm_list, &value);
      }
    }

    gst_structure_take_value (gst_caps_get_structure (caps, 0), "format",
        &shm_list);

    /* Add corresponding dmabuf formats */
    formats = gst_wl_display_get_dmabuf_formats (self->display);
    for (i = 0; i < formats->len; i++) {
      fmt = g_array_index (formats, uint32_t, i);
      gfmt = gst_wl_dmabuf_format_to_video_format (fmt);
      if (gfmt != GST_VIDEO_FORMAT_UNKNOWN) {
        g_value_init (&value, G_TYPE_STRING);
        g_value_set_static_string (&value, gst_video_format_to_string (gfmt));
        gst_value_list_append_and_take_value (&dmabuf_list, &value);
      }
    }

    gst_structure_take_value (gst_caps_get_structure (caps, 1), "format",
        &dmabuf_list);

    GST_DEBUG_OBJECT (self, "display caps: %" GST_PTR_FORMAT, caps);
  }

  g_mutex_unlock (&self->display_lock);

  if (filter) {
    GstCaps *intersection;

    intersection =
        gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = intersection;
  }

  return caps;
}

static void
gst_wayland_set_alignment (GstWaylandSink * self, GstVideoAlignment * align)
{
  guint stride_align, n_planes, i;

  gst_video_alignment_reset (align);
  /* In dma-buffer, ARM Mali requires strict allocation alignment for each
   * color format (NV12, NV21, YV12, IYUV, I420, IMC1, IMC2, IMC3, IMC4,
   * P210, P010 require 16-byte alignment. Others require 64-byte alignment) */
  switch (GST_VIDEO_FORMAT_INFO_FORMAT (self->video_info.finfo)) {
    /* Currently, only NV12, NV21, YV12, I420, and P010 formats are supported
     * by GStreamer version 1.22.12. Please add more if later versions provide
     * additional support */
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_P010_10LE:
      stride_align = 16;
      break;
    default:
      /* Other formats */
      stride_align = 64;
      break;
  }

  n_planes = self->video_info.finfo->n_planes;
  for (i = 0; i < n_planes; i++) {
    align->stride_align[i] = stride_align;
  }

  GST_DEBUG_OBJECT(self, "padding top:%u, left:%u, right:%u, bottom:%u, "
                         "stride_align %d:%d:%d:%d",
                         align->padding_top, align->padding_left,
                         align->padding_right, align->padding_bottom,
                         align->stride_align[0], align->stride_align[1],
                         align->stride_align[2], align->stride_align[3]);

  return;
}

static GstBufferPool *
gst_wayland_create_pool (GstWaylandSink * self, GstCaps * caps)
{
  GstBufferPool *pool = NULL;
  GstStructure *structure;
  gsize size = self->video_info.size;
  GstAllocator *alloc;
  GstVideoAlignment align;

  pool = gst_wl_video_buffer_pool_new ();

  structure = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (structure, caps, size, 2, 0);

  gst_wayland_set_alignment (self, &align);
  gst_buffer_pool_config_set_video_alignment (structure, &align);

  alloc = gst_wl_shm_allocator_get ();
  gst_buffer_pool_config_set_allocator (structure, alloc, NULL);
  if (!gst_buffer_pool_set_config (pool, structure)) {
    g_object_unref (pool);
    pool = NULL;
  }
  g_object_unref (alloc);

  return pool;
}

static gboolean
gst_wayland_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstWaylandSink *self = GST_WAYLAND_SINK (bsink);;
  gboolean use_dmabuf;
  GstVideoFormat format;

  GST_DEBUG_OBJECT (self, "set caps %" GST_PTR_FORMAT, caps);

  /* extract info from caps */
  if (!gst_video_info_from_caps (&self->video_info, caps))
    goto invalid_format;

  format = GST_VIDEO_INFO_FORMAT (&self->video_info);
  self->video_info_changed = TRUE;

  /* create a new pool for the new caps */
  if (self->pool)
    gst_object_unref (self->pool);
  self->pool = gst_wayland_create_pool (self, caps);

  use_dmabuf = gst_caps_features_contains (gst_caps_get_features (caps, 0),
      GST_CAPS_FEATURE_MEMORY_DMABUF);

  /* validate the format base on the memory type. */
  if (use_dmabuf) {
    if (!gst_wl_display_check_format_for_dmabuf (self->display, format))
      goto unsupported_format;
  } else if (!gst_wl_display_check_format_for_shm (self->display, format)) {
    goto unsupported_format;
  }

  self->use_dmabuf = use_dmabuf;

  return TRUE;

invalid_format:
  {
    GST_ERROR_OBJECT (self,
        "Could not locate image format from caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
unsupported_format:
  {
    GST_ERROR_OBJECT (self, "Format %s is not available on the display",
        gst_video_format_to_string (format));
    return FALSE;
  }
}

static gboolean
gst_wayland_sink_propose_allocation (GstBaseSink * bsink, GstQuery * query)
{
  GstWaylandSink *self = GST_WAYLAND_SINK (bsink);
  GstCaps *caps;
  GstBufferPool *pool = NULL;
  gboolean need_pool;
  GstAllocator *alloc;

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (need_pool)
    pool = gst_wayland_create_pool (self, caps);

  gst_query_add_allocation_pool (query, pool, self->video_info.size, 2, 0);
  if (pool)
    g_object_unref (pool);

  alloc = gst_wl_shm_allocator_get ();
  gst_query_add_allocation_param (query, alloc, NULL);
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  g_object_unref (alloc);

  return TRUE;
}

static void
frame_redraw_callback (void *data, struct wl_callback *callback, uint32_t time)
{
  GstWaylandSink *self = data;

  GST_LOG_OBJECT (self, "frame_redraw_cb");

  g_mutex_lock (&self->render_lock);
  self->redraw_pending = FALSE;

  if (self->callback) {
    wl_callback_destroy (callback);
    self->callback = NULL;
  }
  g_mutex_unlock (&self->render_lock);
}

static const struct wl_callback_listener frame_callback_listener = {
  frame_redraw_callback
};

/* must be called with the render lock */
static void
render_last_buffer (GstWaylandSink * self, gboolean redraw)
{
  GstWlBuffer *wlbuffer;
  const GstVideoInfo *info = NULL;
  struct wl_surface *surface;

  wlbuffer = gst_buffer_get_wl_buffer (self->display, self->last_buffer);
  surface = gst_wl_window_get_wl_surface (self->window);

  self->redraw_pending = TRUE;

  self->callback = wl_surface_frame (surface);
  wl_proxy_set_queue ((struct wl_proxy *) self->callback, self->frame_queue);
  wl_callback_add_listener (self->callback, &frame_callback_listener, self);

  if (G_UNLIKELY (self->video_info_changed && !redraw)) {
    info = &self->video_info;
    self->video_info_changed = FALSE;
  }
  gst_wl_window_render (self->window, wlbuffer, info);
}

static void
on_window_closed (GstWlWindow * window, gpointer user_data)
{
  GstWaylandSink *self = GST_WAYLAND_SINK (user_data);

  /* Handle window closure by posting an error on the bus */
  GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
      ("Output window was closed"), (NULL));
}

static GstFlowReturn
gst_wayland_sink_show_frame (GstVideoSink * vsink, GstBuffer * buffer)
{
  GstWaylandSink *self = GST_WAYLAND_SINK (vsink);
  GstBuffer *to_render;
  GstWlBuffer *wlbuffer;
  GstVideoMeta *vmeta;
  GstVideoFormat format;
  GstVideoInfo old_vinfo;
  GstMemory *mem;
  struct wl_buffer *wbuf = NULL;

  GstFlowReturn ret = GST_FLOW_OK;

  g_mutex_lock (&self->render_lock);

  GST_LOG_OBJECT (self, "render buffer %" GST_PTR_FORMAT "", buffer);

  if (G_UNLIKELY (!self->window)) {
    /* ask for window handle. Unlock render_lock while doing that because
     * set_window_handle & friends will lock it in this context */
    g_mutex_unlock (&self->render_lock);
    gst_video_overlay_prepare_window_handle (GST_VIDEO_OVERLAY (self));
    g_mutex_lock (&self->render_lock);

    if (!self->window) {
      /* if we were not provided a window, create one ourselves */
      self->window = gst_wl_window_new_toplevel (self->display,
          &self->video_info, self->fullscreen, &self->render_lock,
          self->position_x, self->position_y,
          self->out_w, self->out_h);
      g_signal_connect_object (self->window, "closed",
          G_CALLBACK (on_window_closed), self, 0);
      gst_wl_window_set_rotate_method (self->window,
          self->current_rotate_method);
    }
  }

  g_mutex_unlock (&self->render_lock);
  wl_display_dispatch_queue_pending ((gst_wl_display_get_display (self->display)), self->frame_queue);
  g_mutex_lock (&self->render_lock);

  while (self->redraw_pending == TRUE) {
    g_mutex_unlock (&self->render_lock);
    wl_display_dispatch_queue ((gst_wl_display_get_display (self->display)), self->frame_queue);
    g_mutex_lock (&self->render_lock);
   }

  /* make sure that the application has called set_render_rectangle() */
  if (G_UNLIKELY (gst_wl_window_get_render_rectangle (self->window)->w == 0))
    goto no_window_size;

  wlbuffer = gst_buffer_get_wl_buffer (self->display, buffer);

  if (G_LIKELY (wlbuffer)) {
    GST_LOG_OBJECT (self,
        "buffer %" GST_PTR_FORMAT " has a wl_buffer from our display, "
        "writing directly", buffer);
    to_render = buffer;
    goto render;
  }

  /* update video info from video meta */
  mem = gst_buffer_peek_memory (buffer, 0);

  old_vinfo = self->video_info;
  vmeta = gst_buffer_get_video_meta (buffer);
  if (vmeta) {
    gint i;

    for (i = 0; i < vmeta->n_planes; i++) {
      self->video_info.offset[i] = vmeta->offset[i];
      self->video_info.stride[i] = vmeta->stride[i];
    }
    self->video_info.size = gst_buffer_get_size (buffer);
  }

  GST_LOG_OBJECT (self,
      "buffer %" GST_PTR_FORMAT " does not have a wl_buffer from our "
      "display, creating it", buffer);

  format = GST_VIDEO_INFO_FORMAT (&self->video_info);
  if (gst_wl_display_check_format_for_dmabuf (self->display, format)) {
    guint i, nb_dmabuf = 0;

    for (i = 0; i < gst_buffer_n_memory (buffer); i++)
      if (gst_is_dmabuf_memory (gst_buffer_peek_memory (buffer, i)))
        nb_dmabuf++;

    if (nb_dmabuf && (nb_dmabuf == gst_buffer_n_memory (buffer)))
      wbuf = gst_wl_linux_dmabuf_construct_wl_buffer (buffer, self->display,
          &self->video_info, self->enable_interlace);
  }

  if (!wbuf && gst_wl_display_check_format_for_shm (self->display, format)) {
    if (gst_buffer_n_memory (buffer) == 1 && gst_is_fd_memory (mem))
      wbuf = gst_wl_shm_memory_construct_wl_buffer (mem, self->display,
          &self->video_info);

    /* If nothing worked, copy into our internal pool */
    if (!wbuf) {
      GstVideoFrame src, dst;
      GstVideoInfo src_info = self->video_info;

      /* rollback video info changes */
      self->video_info = old_vinfo;

      /* we don't know how to create a wl_buffer directly from the provided
       * memory, so we have to copy the data to shm memory that we know how
       * to handle... */

      GST_LOG_OBJECT (self,
          "buffer %" GST_PTR_FORMAT " cannot have a wl_buffer, "
          "copying to wl_shm memory", buffer);

      /* self->pool always exists (created in set_caps), but it may not
       * be active if upstream is not using it */
      if (!gst_buffer_pool_is_active (self->pool)) {
        GstStructure *config;
        GstCaps *caps;

        config = gst_buffer_pool_get_config (self->pool);
        gst_buffer_pool_config_get_params (config, &caps, NULL, NULL, NULL);

        /* revert back to default strides and offsets */
        gst_video_info_from_caps (&self->video_info, caps);
        gst_buffer_pool_config_set_params (config, caps, self->video_info.size,
            2, 0);

        /* This is a video pool, it should not fail with basic settings */
        if (!gst_buffer_pool_set_config (self->pool, config) ||
            !gst_buffer_pool_set_active (self->pool, TRUE))
          goto activate_failed;
      }

      ret = gst_buffer_pool_acquire_buffer (self->pool, &to_render, NULL);
      if (ret != GST_FLOW_OK)
        goto no_buffer;

      wlbuffer = gst_buffer_get_wl_buffer (self->display, to_render);

      /* attach a wl_buffer if there isn't one yet */
      if (G_UNLIKELY (!wlbuffer)) {
        mem = gst_buffer_peek_memory (to_render, 0);
        wbuf = gst_wl_shm_memory_construct_wl_buffer (mem, self->display,
            &self->video_info);

        if (G_UNLIKELY (!wbuf))
          goto no_wl_buffer_shm;

        wlbuffer = gst_buffer_add_wl_buffer (to_render, wbuf, self->display);
      }

      if (!gst_video_frame_map (&dst, &self->video_info, to_render,
              GST_MAP_WRITE))
        goto dst_map_failed;

      if (!gst_video_frame_map (&src, &src_info, buffer, GST_MAP_READ)) {
        gst_video_frame_unmap (&dst);
        goto src_map_failed;
      }

      gst_video_frame_copy (&dst, &src);

      gst_video_frame_unmap (&src);
      gst_video_frame_unmap (&dst);

      goto render;
    }
  }

  if (!wbuf)
    goto no_wl_buffer;

  wlbuffer = gst_buffer_add_wl_buffer (buffer, wbuf, self->display);
  to_render = buffer;

render:
  /* drop double rendering */
  if (G_UNLIKELY (wlbuffer ==
          gst_buffer_get_wl_buffer (self->display, self->last_buffer))) {
    GST_LOG_OBJECT (self, "Buffer already being rendered");
    goto done;
  }

  gst_buffer_replace (&self->last_buffer, to_render);
  render_last_buffer (self, FALSE);

  if (buffer != to_render)
    gst_buffer_unref (to_render);
  goto done;

no_window_size:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
        ("Window has no size set"),
        ("Make sure you set the size after calling set_window_handle"));
    ret = GST_FLOW_ERROR;
    goto done;
  }
no_buffer:
  {
    GST_WARNING_OBJECT (self, "could not create buffer");
    goto done;
  }
no_wl_buffer_shm:
  {
    GST_ERROR_OBJECT (self, "could not create wl_buffer out of wl_shm memory");
    ret = GST_FLOW_ERROR;
    goto done;
  }
no_wl_buffer:
  {
    GST_ERROR_OBJECT (self,
        "buffer %" GST_PTR_FORMAT " cannot have a wl_buffer", buffer);
    ret = GST_FLOW_ERROR;
    goto done;
  }
activate_failed:
  {
    GST_ERROR_OBJECT (self, "failed to activate bufferpool.");
    ret = GST_FLOW_ERROR;
    goto done;
  }
src_map_failed:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, READ,
        ("Video memory can not be read from userspace."), (NULL));
    ret = GST_FLOW_ERROR;
    goto done;
  }
dst_map_failed:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
        ("Video memory can not be written from userspace."), (NULL));
    ret = GST_FLOW_ERROR;
    goto done;
  }
done:
  {
    g_mutex_unlock (&self->render_lock);
    return ret;
  }
}

static void
gst_wayland_sink_videooverlay_init (GstVideoOverlayInterface * iface)
{
  iface->set_window_handle = gst_wayland_sink_set_window_handle;
  iface->set_render_rectangle = gst_wayland_sink_set_render_rectangle;
  iface->expose = gst_wayland_sink_expose;
}

static void
gst_wayland_sink_set_window_handle (GstVideoOverlay * overlay, guintptr handle)
{
  GstWaylandSink *self = GST_WAYLAND_SINK (overlay);
  struct wl_surface *surface = (struct wl_surface *) handle;

  g_return_if_fail (self != NULL);

  if (self->window != NULL) {
    GST_WARNING_OBJECT (self, "changing window handle is not supported");
    return;
  }

  g_mutex_lock (&self->render_lock);

  GST_DEBUG_OBJECT (self, "Setting window handle %" GST_PTR_FORMAT,
      (void *) handle);

  g_clear_object (&self->window);

  if (handle) {
    if (G_LIKELY (gst_wayland_sink_find_display (self))) {
      /* we cannot use our own display with an external window handle */
      if (G_UNLIKELY (gst_wl_display_has_own_display (self->display))) {
        GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ_WRITE,
            ("Application did not provide a wayland display handle"),
            ("waylandsink cannot use an externally-supplied surface without "
                "an externally-supplied display handle. Consider providing a "
                "display handle from your application with GstContext"));
      } else {
        self->window = gst_wl_window_new_in_surface (self->display, surface,
            &self->render_lock);
        gst_wl_window_set_rotate_method (self->window,
            self->current_rotate_method);
      }
    } else {
      GST_ERROR_OBJECT (self, "Failed to find display handle, "
          "ignoring window handle");
    }
  }

  g_mutex_unlock (&self->render_lock);
}

static void
gst_wayland_sink_set_render_rectangle (GstVideoOverlay * overlay,
    gint x, gint y, gint w, gint h)
{
  GstWaylandSink *self = GST_WAYLAND_SINK (overlay);

  g_return_if_fail (self != NULL);

  g_mutex_lock (&self->render_lock);
  if (!self->window) {
    g_mutex_unlock (&self->render_lock);
    GST_WARNING_OBJECT (self,
        "set_render_rectangle called without window, ignoring");
    return;
  }

  GST_DEBUG_OBJECT (self, "window geometry changed to (%d, %d) %d x %d",
      x, y, w, h);
  gst_wl_window_set_render_rectangle (self->window, x, y, w, h);

  g_mutex_unlock (&self->render_lock);
}

static void
gst_wayland_sink_expose (GstVideoOverlay * overlay)
{
  GstWaylandSink *self = GST_WAYLAND_SINK (overlay);

  g_return_if_fail (self != NULL);

  GST_DEBUG_OBJECT (self, "expose");

  g_mutex_lock (&self->render_lock);
  if (self->last_buffer && !self->redraw_pending) {
    GST_DEBUG_OBJECT (self, "redrawing last buffer");
    render_last_buffer (self, TRUE);
  }
  g_mutex_unlock (&self->render_lock);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gstwayland_debug, "waylandsink", 0,
      " wayland video sink");

  return GST_ELEMENT_REGISTER (waylandsink, plugin);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    waylandsink,
    "Wayland Video Sink", plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
