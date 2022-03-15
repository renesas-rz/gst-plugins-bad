/* GStreamer
 * Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstnvh264encoder.h"
#include <gst/codecparsers/gsth264parser.h>
#include <gst/pbutils/codec-utils.h>
#include <string>
#include <set>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_nv_h264_encoder_debug);
#define GST_CAT_DEFAULT gst_nv_h264_encoder_debug

static GTypeClass *parent_class = NULL;

typedef struct
{
  gint max_bframes;
  gint ratecontrol_modes;
  gint field_encoding;
  gint monochrome;
  gint fmo;
  gint qpelmv;
  gint bdirect_mode;
  gint cabac;
  gint adaptive_transform;
  gint stereo_mvc;
  gint temoral_layers;
  gint hierarchical_pframes;
  gint hierarchical_bframes;
  gint level_max;
  gint level_min;
  gint seperate_colour_plane;
  gint width_max;
  gint height_max;
  gint temporal_svc;
  gint dyn_res_change;
  gint dyn_bitrate_change;
  gint dyn_force_constqp;
  gint dyn_rcmode_change;
  gint subframe_readback;
  gint constrained_encoding;
  gint intra_refresh;
  gint custom_vbv_buf_size;
  gint dynamic_slice_mode;
  gint ref_pic_invalidation;
  gint preproc_support;
  gint async_encoding_support;
  gint mb_num_max;
  gint mb_per_sec_max;
  gint yuv444_encode;
  gint lossless_encode;
  gint meonly_mode;
  gint lookahead;
  gint temporal_aq;
  gint supports_10bit_encode;
  gint num_max_ltr_frames;
  gint weighted_prediction;
  gint bframe_ref_mode;
  gint emphasis_level_map;
  gint width_min;
  gint height_min;
  gint multiple_ref_frames;
} GstNvH264EncoderDeviceCaps;

typedef struct
{
  GstCaps *sink_caps;
  GstCaps *src_caps;

  guint cuda_device_id;
  gint64 adapter_luid;
  gboolean d3d11_mode;

  GstNvH264EncoderDeviceCaps dev_caps;
} GstNvH264EncoderClassData;

enum
{
  PROP_0,
  PROP_ADAPTER_LUID,
  PROP_CUDA_DEVICE_ID,

  /* init params */
  PROP_PRESET,
  PROP_WEIGHTED_PRED,

  /* encoding config */
  PROP_GOP_SIZE,
  PROP_B_FRAMES,

  /* rate-control params */
  PROP_RC_MODE,

  PROP_QP_CONST_I,
  PROP_QP_CONST_P,
  PROP_QP_CONST_B,

  PROP_BITRATE,
  PROP_MAX_BITRATE,
  PROP_VBV_BUFFER_SIZE,

  PROP_RC_LOOKAHEAD,
  PROP_I_ADAPT,
  PROP_B_ADAPT,
  PROP_SPATIAL_AQ,
  PROP_TEMPORAL_AQ,
  PROP_ZERO_LATENCY,
  PROP_NON_REF_P,
  PROP_STRICT_GOP,
  PROP_AQ_STRENGTH,

  PROP_QP_MIN_I,
  PROP_QP_MIN_P,
  PROP_QP_MIN_B,

  PROP_QP_MAX_I,
  PROP_QP_MAX_P,
  PROP_QP_MAX_B,

  PROP_CONST_QUALITY,

  /* h264 specific */
  PROP_AUD,
  PROP_CABAC,
  PROP_REPEAT_SEQUENCE_HEADER,
};

#define DEFAULT_PRESET            GST_NV_ENCODER_PRESET_DEFAULT
#define DEFAULT_WEIGHTED_PRED     FALSE
#define DEFAULT_GOP_SIZE          75
#define DEFAULT_B_FRAMES          0
#define DEFAULT_RC_MODE           GST_NV_ENCODER_RC_MODE_VBR
#define DEFAULT_QP                -1
#define DEFAULT_BITRATE           0
#define DEFAULT_MAX_BITRATE       0
#define DEFAULT_VBV_BUFFER_SIZE   0
#define DEFAULT_RC_LOOKAHEAD      0
#define DEFAULT_I_ADAPT           FALSE
#define DEFAULT_B_ADAPT           FALSE
#define DEFAULT_SPATIAL_AQ        FALSE
#define DEFAULT_TEMPORAL_AQ       FALSE
#define DEFAULT_ZERO_LATENCY      FALSE
#define DEFAULT_NON_REF_P         FALSE
#define DEFAULT_STRICT_GOP        FALSE
#define DEFAULT_AQ_STRENGTH       FALSE
#define DEFAULT_CONST_QUALITY     0
#define DEFAULT_AUD               TRUE
#define DEFAULT_REPEAT_SEQUENCE_HEADER FALSE

typedef struct _GstNvH264Encoder
{
  GstNvEncoder parent;
  GMutex prop_lock;

  gboolean init_param_updated;
  gboolean rc_param_updated;
  gboolean bitrate_updated;

  gboolean packetized;
  GstH264NalParser *parser;

  /* Properties */
  GstNvEncoderPreset preset;
  gboolean weighted_pred;

  gint gop_size;
  guint bframes;

  GstNvEncoderRCMode rc_mode;
  gint qp_const_i;
  gint qp_const_p;
  gint qp_const_b;
  guint bitrate;
  guint max_bitrate;
  guint vbv_buffer_size;
  guint rc_lookahead;
  gboolean i_adapt;
  gboolean b_adapt;
  gboolean spatial_aq;
  gboolean temporal_aq;
  gboolean zero_latency;
  gboolean non_ref_p;
  gboolean strict_gop;
  guint aq_strength;
  gint qp_min_i;
  gint qp_min_p;
  gint qp_min_b;
  gint qp_max_i;
  gint qp_max_p;
  gint qp_max_b;
  gdouble const_quality;

  gboolean aud;
  gboolean cabac;
  gboolean repeat_sequence_header;
} GstNvH264Encoder;

typedef struct _GstNvH264EncoderClass
{
  GstNvEncoderClass parent_class;
  GstNvH264EncoderDeviceCaps dev_caps;

  guint cuda_device_id;
  gint64 adapter_luid;
  gboolean d3d11_mode;
} GstNvH264EncoderClass;

#define GST_NV_H264_ENCODER(object) ((GstNvH264Encoder *) (object))
#define GST_NV_H264_ENCODER_GET_CLASS(object) \
    (G_TYPE_INSTANCE_GET_CLASS ((object),G_TYPE_FROM_INSTANCE (object),GstNvH264EncoderClass))

static void gst_nv_h264_encoder_finalize (GObject * object);
static void gst_nv_h264_encoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_nv_h264_encoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstCaps *gst_nv_h264_encoder_getcaps (GstVideoEncoder * encoder,
    GstCaps * filter);
static gboolean gst_nv_h264_encoder_set_format (GstNvEncoder * encoder,
    GstVideoCodecState * state, gpointer session,
    NV_ENC_INITIALIZE_PARAMS * init_params, NV_ENC_CONFIG * config);
static gboolean gst_nv_h264_encoder_set_output_state (GstNvEncoder * encoder,
    GstVideoCodecState * state, gpointer session);
static GstBuffer *gst_nv_h264_encoder_create_output_buffer (GstNvEncoder *
    encoder, NV_ENC_LOCK_BITSTREAM * bitstream);
static GstNvEncoderReconfigure
gst_nv_h264_encoder_check_reconfigure (GstNvEncoder * encoder,
    NV_ENC_CONFIG * config);

static void
gst_nv_h264_encoder_class_init (GstNvH264EncoderClass * klass, gpointer data)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoEncoderClass *videoenc_class = GST_VIDEO_ENCODER_CLASS (klass);
  GstNvEncoderClass *nvenc_class = GST_NV_ENCODER_CLASS (klass);
  GstNvH264EncoderClassData *cdata = (GstNvH264EncoderClassData *) data;
  GstNvH264EncoderDeviceCaps *dev_caps = &cdata->dev_caps;
  GParamFlags param_flags = (GParamFlags) (G_PARAM_READWRITE |
      GST_PARAM_MUTABLE_PLAYING | G_PARAM_STATIC_STRINGS);
  GParamFlags conditonal_param_flags = (GParamFlags) (G_PARAM_READWRITE |
      GST_PARAM_CONDITIONALLY_AVAILABLE | GST_PARAM_MUTABLE_PLAYING |
      G_PARAM_STATIC_STRINGS);

  parent_class = (GTypeClass *) g_type_class_peek_parent (klass);

  object_class->finalize = gst_nv_h264_encoder_finalize;
  object_class->set_property = gst_nv_h264_encoder_set_property;
  object_class->get_property = gst_nv_h264_encoder_get_property;

  if (cdata->d3d11_mode) {
    g_object_class_install_property (object_class, PROP_ADAPTER_LUID,
        g_param_spec_int64 ("adapter-luid", "Adapter LUID",
            "DXGI Adapter LUID (Locally Unique Identifier) of associated GPU",
            G_MININT64, G_MAXINT64, cdata->adapter_luid,
            (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
  } else {
    g_object_class_install_property (object_class, PROP_CUDA_DEVICE_ID,
        g_param_spec_uint ("cuda-device-id", "CUDA Device ID",
            "CUDA device ID of associated GPU",
            0, G_MAXINT, cdata->cuda_device_id,
            (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
  }
  g_object_class_install_property (object_class, PROP_PRESET,
      g_param_spec_enum ("preset", "Encoding Preset",
          "Encoding Preset", GST_TYPE_NV_ENCODER_PRESET,
          DEFAULT_PRESET, param_flags));
  if (dev_caps->weighted_prediction) {
    g_object_class_install_property (object_class, PROP_WEIGHTED_PRED,
        g_param_spec_boolean ("weighted-pred", "Weighted Pred",
            "Enables Weighted Prediction", DEFAULT_WEIGHTED_PRED,
            conditonal_param_flags));
  }
  g_object_class_install_property (object_class, PROP_GOP_SIZE,
      g_param_spec_int ("gop-size", "GOP size",
          "Number of frames between intra frames (-1 = infinite)",
          -1, G_MAXINT, DEFAULT_GOP_SIZE, param_flags));
  if (dev_caps->max_bframes > 0) {
    g_object_class_install_property (object_class, PROP_B_FRAMES,
        g_param_spec_uint ("bframes", "B-Frames",
            "Number of B-frames between I and P", 0, dev_caps->max_bframes,
            DEFAULT_B_FRAMES, conditonal_param_flags));
  }
  g_object_class_install_property (object_class, PROP_RC_MODE,
      g_param_spec_enum ("rc-mode", "RC Mode", "Rate Control Mode",
          GST_TYPE_NV_ENCODER_RC_MODE, DEFAULT_RC_MODE, param_flags));
  g_object_class_install_property (object_class, PROP_QP_CONST_I,
      g_param_spec_int ("qp-const-i", "QP Const I",
          "Constant QP value for I frame (-1 = disabled)", -1, 51,
          DEFAULT_QP, param_flags));
  g_object_class_install_property (object_class, PROP_QP_CONST_P,
      g_param_spec_int ("qp-const-p", "QP Const P",
          "Constant QP value for P frame (-1 = disabled)", -1, 51,
          DEFAULT_QP, param_flags));
  g_object_class_install_property (object_class, PROP_QP_CONST_B,
      g_param_spec_int ("qp-const-b", "QP Const B",
          "Constant QP value for B frame (-1 = disabled)", -1, 51,
          DEFAULT_QP, param_flags));
  g_object_class_install_property (object_class, PROP_BITRATE,
      g_param_spec_uint ("bitrate", "Bitrate",
          "Bitrate in kbit/sec (0 = automatic)", 0, 2000 * 1024,
          DEFAULT_BITRATE, param_flags));
  g_object_class_install_property (object_class, PROP_MAX_BITRATE,
      g_param_spec_uint ("max-bitrate", "Max Bitrate",
          "Maximum Bitrate in kbit/sec (ignored in CBR mode)", 0, 2000 * 1024,
          DEFAULT_MAX_BITRATE, param_flags));
  if (dev_caps->custom_vbv_buf_size) {
    g_object_class_install_property (object_class,
        PROP_VBV_BUFFER_SIZE,
        g_param_spec_uint ("vbv-buffer-size", "VBV Buffer Size",
            "VBV(HRD) Buffer Size in kbits (0 = NVENC default)",
            0, G_MAXUINT, DEFAULT_VBV_BUFFER_SIZE, conditonal_param_flags));
  }
  if (dev_caps->lookahead) {
    g_object_class_install_property (object_class, PROP_RC_LOOKAHEAD,
        g_param_spec_uint ("rc-lookahead", "Rate Control Lookahead",
            "Number of frames for frame type lookahead",
            0, 32, DEFAULT_RC_LOOKAHEAD, conditonal_param_flags));
    g_object_class_install_property (object_class, PROP_I_ADAPT,
        g_param_spec_boolean ("i-adapt", "I Adapt",
            "Enable adaptive I-frame insert when lookahead is enabled",
            DEFAULT_I_ADAPT, conditonal_param_flags));
    if (dev_caps->max_bframes > 0) {
      g_object_class_install_property (object_class, PROP_B_ADAPT,
          g_param_spec_boolean ("b-adapt", "B Adapt",
              "Enable adaptive B-frame insert when lookahead is enabled",
              DEFAULT_B_ADAPT, conditonal_param_flags));
    }
  }
  g_object_class_install_property (object_class, PROP_SPATIAL_AQ,
      g_param_spec_boolean ("spatial-aq", "Spatial AQ",
          "Spatial Adaptive Quantization", DEFAULT_SPATIAL_AQ, param_flags));
  if (dev_caps->temporal_aq) {
    g_object_class_install_property (object_class, PROP_TEMPORAL_AQ,
        g_param_spec_boolean ("temporal-aq", "Temporal AQ",
            "Temporal Adaptive Quantization", DEFAULT_TEMPORAL_AQ,
            conditonal_param_flags));
  }
  g_object_class_install_property (object_class, PROP_ZERO_LATENCY,
      g_param_spec_boolean ("zerolatency", "Zerolatency",
          "Zero latency operation (no reordering delay)", DEFAULT_ZERO_LATENCY,
          param_flags));
  g_object_class_install_property (object_class, PROP_NON_REF_P,
      g_param_spec_boolean ("nonref-p", "Nonref P",
          "Automatic insertion of non-reference P-frames", DEFAULT_NON_REF_P,
          param_flags));
  g_object_class_install_property (object_class, PROP_STRICT_GOP,
      g_param_spec_boolean ("strict-gop", "Strict GOP",
          "Minimize GOP-to-GOP rate fluctuations", DEFAULT_STRICT_GOP,
          param_flags));
  g_object_class_install_property (object_class, PROP_AQ_STRENGTH,
      g_param_spec_uint ("aq-strength", "AQ Strength",
          "Adaptive Quantization Strength when spatial-aq is enabled"
          " from 1 (low) to 15 (aggressive), (0 = autoselect)",
          0, 15, DEFAULT_AQ_STRENGTH, param_flags));
  g_object_class_install_property (object_class, PROP_QP_MIN_I,
      g_param_spec_int ("qp-min-i", "QP Min I",
          "Minimum QP value for I frame, (-1 = disabled)", -1, 51,
          DEFAULT_QP, param_flags));
  g_object_class_install_property (object_class, PROP_QP_MIN_P,
      g_param_spec_int ("qp-min-p", "QP Min P",
          "Minimum QP value for P frame, (-1 = automatic)", -1, 51,
          DEFAULT_QP, param_flags));
  g_object_class_install_property (object_class, PROP_QP_MIN_B,
      g_param_spec_int ("qp-min-b", "QP Min B",
          "Minimum QP value for B frame, (-1 = automatic)", -1, 51,
          DEFAULT_QP, param_flags));
  g_object_class_install_property (object_class, PROP_QP_MAX_I,
      g_param_spec_int ("qp-max-i", "QP Max I",
          "Maximum QP value for I frame, (-1 = disabled)", -1, 51,
          DEFAULT_QP, param_flags));
  g_object_class_install_property (object_class, PROP_QP_MAX_P,
      g_param_spec_int ("qp-max-p", "QP Max P",
          "Maximum QP value for P frame, (-1 = automatic)", -1, 51,
          DEFAULT_QP, param_flags));
  g_object_class_install_property (object_class, PROP_QP_MAX_B,
      g_param_spec_int ("qp-max-b", "QP Max B",
          "Maximum QP value for B frame, (-1 = automatic)", -1, 51,
          DEFAULT_QP, param_flags));
  g_object_class_install_property (object_class, PROP_CONST_QUALITY,
      g_param_spec_double ("const-quality", "Constant Quality",
          "Target Constant Quality level for VBR mode (0 = automatic)",
          0, 51, DEFAULT_CONST_QUALITY, param_flags));
  g_object_class_install_property (object_class, PROP_AUD,
      g_param_spec_boolean ("aud", "AUD",
          "Use AU (Access Unit) delimiter", DEFAULT_AUD, param_flags));
  if (dev_caps->cabac) {
    g_object_class_install_property (object_class, PROP_CABAC,
        g_param_spec_boolean ("cabac", "CABAC",
            "Enable CABAC entropy coding", TRUE, conditonal_param_flags));
  }
  g_object_class_install_property (object_class, PROP_REPEAT_SEQUENCE_HEADER,
      g_param_spec_boolean ("repeat-sequence-header", "Repeat Sequence Header",
          "Insert sequence headers (SPS/PPS) per IDR",
          DEFAULT_REPEAT_SEQUENCE_HEADER, param_flags));

  if (cdata->d3d11_mode) {
    gst_element_class_set_metadata (element_class,
        "NVENC H.264 Video Encoder Direct3D11 Mode",
        "Codec/Encoder/Video/Hardware",
        "Encode H.264 video streams using NVCODEC API Direct3D11 Mode",
        "Seungha Yang <seungha@centricular.com>");
  } else {
    gst_element_class_set_metadata (element_class,
        "NVENC H.264 Video Encoder CUDA Mode",
        "Codec/Encoder/Video/Hardware",
        "Encode H.264 video streams using NVCODEC API CUDA Mode",
        "Seungha Yang <seungha@centricular.com>");
  }

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));

  videoenc_class->getcaps = GST_DEBUG_FUNCPTR (gst_nv_h264_encoder_getcaps);

  nvenc_class->set_format = GST_DEBUG_FUNCPTR (gst_nv_h264_encoder_set_format);
  nvenc_class->set_output_state =
      GST_DEBUG_FUNCPTR (gst_nv_h264_encoder_set_output_state);
  nvenc_class->create_output_buffer =
      GST_DEBUG_FUNCPTR (gst_nv_h264_encoder_create_output_buffer);
  nvenc_class->check_reconfigure =
      GST_DEBUG_FUNCPTR (gst_nv_h264_encoder_check_reconfigure);

  klass->dev_caps = cdata->dev_caps;
  klass->cuda_device_id = cdata->cuda_device_id;
  klass->adapter_luid = cdata->adapter_luid;
  klass->d3d11_mode = cdata->d3d11_mode;

  gst_caps_unref (cdata->sink_caps);
  gst_caps_unref (cdata->src_caps);
  g_free (cdata);
}

static void
gst_nv_h264_encoder_init (GstNvH264Encoder * self)
{
  GstNvH264EncoderClass *klass = GST_NV_H264_ENCODER_GET_CLASS (self);

  g_mutex_init (&self->prop_lock);

  self->preset = DEFAULT_PRESET;
  self->weighted_pred = DEFAULT_WEIGHTED_PRED;
  self->gop_size = DEFAULT_GOP_SIZE;
  self->bframes = DEFAULT_B_FRAMES;
  self->rc_mode = DEFAULT_RC_MODE;
  self->qp_const_i = DEFAULT_QP;
  self->qp_const_p = DEFAULT_QP;
  self->qp_const_b = DEFAULT_QP;
  self->bitrate = DEFAULT_BITRATE;
  self->max_bitrate = DEFAULT_MAX_BITRATE;
  self->vbv_buffer_size = DEFAULT_VBV_BUFFER_SIZE;
  self->rc_lookahead = DEFAULT_RC_LOOKAHEAD;
  self->i_adapt = DEFAULT_I_ADAPT;
  self->b_adapt = DEFAULT_B_ADAPT;
  self->spatial_aq = DEFAULT_SPATIAL_AQ;
  self->temporal_aq = DEFAULT_TEMPORAL_AQ;
  self->zero_latency = DEFAULT_ZERO_LATENCY;
  self->non_ref_p = DEFAULT_NON_REF_P;
  self->strict_gop = DEFAULT_STRICT_GOP;
  self->aq_strength = DEFAULT_AQ_STRENGTH;
  self->qp_min_i = DEFAULT_QP;
  self->qp_min_p = DEFAULT_QP;
  self->qp_min_b = DEFAULT_QP;
  self->qp_max_i = DEFAULT_QP;
  self->qp_max_p = DEFAULT_QP;
  self->qp_max_b = DEFAULT_QP;
  self->const_quality = DEFAULT_CONST_QUALITY;
  self->aud = DEFAULT_AUD;
  if (klass->dev_caps.cabac)
    self->cabac = TRUE;
  self->repeat_sequence_header = DEFAULT_REPEAT_SEQUENCE_HEADER;

  self->parser = gst_h264_nal_parser_new ();

  if (klass->d3d11_mode) {
    gst_nv_encoder_set_dxgi_adapter_luid (GST_NV_ENCODER (self),
        klass->adapter_luid);
  } else {
    gst_nv_encoder_set_cuda_device_id (GST_NV_ENCODER (self),
        klass->cuda_device_id);
  }
}

static void
gst_nv_h264_encoder_finalize (GObject * object)
{
  GstNvH264Encoder *self = GST_NV_H264_ENCODER (object);

  g_mutex_clear (&self->prop_lock);
  gst_h264_nal_parser_free (self->parser);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

typedef enum
{
  UPDATE_INIT_PARAM,
  UPDATE_RC_PARAM,
  UPDATE_BITRATE,
} PropUpdateLevel;

static void
update_boolean (GstNvH264Encoder * self, gboolean * old_val,
    const GValue * new_val, PropUpdateLevel level)
{
  gboolean val = g_value_get_boolean (new_val);

  if (*old_val == val)
    return;

  *old_val = val;
  switch (level) {
    case UPDATE_INIT_PARAM:
      self->init_param_updated = TRUE;
      break;
    case UPDATE_RC_PARAM:
      self->rc_param_updated = TRUE;
      break;
    case UPDATE_BITRATE:
      self->bitrate_updated = TRUE;
      break;
  }
}

static void
update_int (GstNvH264Encoder * self, gint * old_val,
    const GValue * new_val, PropUpdateLevel level)
{
  gint val = g_value_get_int (new_val);

  if (*old_val == val)
    return;

  *old_val = val;
  switch (level) {
    case UPDATE_INIT_PARAM:
      self->init_param_updated = TRUE;
      break;
    case UPDATE_RC_PARAM:
      self->rc_param_updated = TRUE;
      break;
    case UPDATE_BITRATE:
      self->bitrate_updated = TRUE;
      break;
  }
}

static void
update_uint (GstNvH264Encoder * self, guint * old_val,
    const GValue * new_val, PropUpdateLevel level)
{
  guint val = g_value_get_uint (new_val);

  if (*old_val == val)
    return;

  *old_val = val;
  switch (level) {
    case UPDATE_INIT_PARAM:
      self->init_param_updated = TRUE;
      break;
    case UPDATE_RC_PARAM:
      self->rc_param_updated = TRUE;
      break;
    case UPDATE_BITRATE:
      self->bitrate_updated = TRUE;
      break;
  }
}

static void
update_double (GstNvH264Encoder * self, gdouble * old_val,
    const GValue * new_val, PropUpdateLevel level)
{
  gdouble val = g_value_get_double (new_val);

  if (*old_val == val)
    return;

  *old_val = val;
  switch (level) {
    case UPDATE_INIT_PARAM:
      self->init_param_updated = TRUE;
      break;
    case UPDATE_RC_PARAM:
      self->rc_param_updated = TRUE;
      break;
    case UPDATE_BITRATE:
      self->bitrate_updated = TRUE;
      break;
  }
}

static void
gst_nv_h264_encoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstNvH264Encoder *self = GST_NV_H264_ENCODER (object);

  g_mutex_lock (&self->prop_lock);
  switch (prop_id) {
    case PROP_PRESET:{
      GstNvEncoderPreset preset = (GstNvEncoderPreset) g_value_get_enum (value);
      if (preset != self->preset) {
        self->preset = preset;
        self->init_param_updated = TRUE;
      }
      break;
    }
    case PROP_WEIGHTED_PRED:
      update_boolean (self, &self->weighted_pred, value, UPDATE_INIT_PARAM);
      break;
    case PROP_GOP_SIZE:
      update_int (self, &self->gop_size, value, UPDATE_INIT_PARAM);
      break;
    case PROP_B_FRAMES:
      update_uint (self, &self->bframes, value, UPDATE_INIT_PARAM);
      break;
    case PROP_RC_MODE:{
      GstNvEncoderRCMode mode = (GstNvEncoderRCMode) g_value_get_enum (value);
      if (mode != self->rc_mode) {
        self->rc_mode = mode;
        self->rc_param_updated = TRUE;
      }
      break;
    }
    case PROP_QP_CONST_I:
      update_int (self, &self->qp_const_i, value, UPDATE_RC_PARAM);
      break;
    case PROP_QP_CONST_P:
      update_int (self, &self->qp_const_p, value, UPDATE_RC_PARAM);
      break;
    case PROP_QP_CONST_B:
      update_int (self, &self->qp_const_b, value, UPDATE_RC_PARAM);
      break;
    case PROP_BITRATE:
      update_uint (self, &self->bitrate, value, UPDATE_BITRATE);
      break;
    case PROP_MAX_BITRATE:
      update_uint (self, &self->max_bitrate, value, UPDATE_BITRATE);
      break;
    case PROP_VBV_BUFFER_SIZE:
      update_uint (self, &self->vbv_buffer_size, value, UPDATE_RC_PARAM);
      break;
    case PROP_RC_LOOKAHEAD:
      /* rc-lookahead update requires pool size change */
      update_uint (self, &self->rc_lookahead, value, UPDATE_INIT_PARAM);
      break;
    case PROP_I_ADAPT:
      update_boolean (self, &self->i_adapt, value, UPDATE_RC_PARAM);
      break;
    case PROP_B_ADAPT:
      update_boolean (self, &self->b_adapt, value, UPDATE_RC_PARAM);
      break;
    case PROP_SPATIAL_AQ:
      update_boolean (self, &self->spatial_aq, value, UPDATE_RC_PARAM);
      break;
    case PROP_TEMPORAL_AQ:
      update_boolean (self, &self->temporal_aq, value, UPDATE_RC_PARAM);
      break;
    case PROP_ZERO_LATENCY:
      update_boolean (self, &self->zero_latency, value, UPDATE_RC_PARAM);
      break;
    case PROP_NON_REF_P:
      update_boolean (self, &self->non_ref_p, value, UPDATE_RC_PARAM);
      break;
    case PROP_STRICT_GOP:
      update_boolean (self, &self->strict_gop, value, UPDATE_RC_PARAM);
      break;
    case PROP_AQ_STRENGTH:
      update_uint (self, &self->aq_strength, value, UPDATE_RC_PARAM);
      break;
    case PROP_QP_MIN_I:
      update_int (self, &self->qp_min_i, value, UPDATE_RC_PARAM);
      break;
    case PROP_QP_MIN_P:
      update_int (self, &self->qp_min_p, value, UPDATE_RC_PARAM);
      break;
    case PROP_QP_MIN_B:
      update_int (self, &self->qp_min_b, value, UPDATE_RC_PARAM);
      break;
    case PROP_QP_MAX_I:
      update_int (self, &self->qp_min_i, value, UPDATE_RC_PARAM);
      break;
    case PROP_QP_MAX_P:
      update_int (self, &self->qp_min_p, value, UPDATE_RC_PARAM);
      break;
    case PROP_QP_MAX_B:
      update_int (self, &self->qp_min_b, value, UPDATE_RC_PARAM);
      break;
    case PROP_CONST_QUALITY:
      update_double (self, &self->const_quality, value, UPDATE_RC_PARAM);
      break;
    case PROP_AUD:
      update_boolean (self, &self->aud, value, UPDATE_INIT_PARAM);
      break;
    case PROP_CABAC:
      update_boolean (self, &self->cabac, value, UPDATE_INIT_PARAM);
      break;
    case PROP_REPEAT_SEQUENCE_HEADER:
      update_boolean (self,
          &self->repeat_sequence_header, value, UPDATE_INIT_PARAM);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  g_mutex_unlock (&self->prop_lock);
}

static void
gst_nv_h264_encoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstNvH264Encoder *self = GST_NV_H264_ENCODER (object);
  GstNvH264EncoderClass *klass = GST_NV_H264_ENCODER_GET_CLASS (self);

  switch (prop_id) {
    case PROP_ADAPTER_LUID:
      g_value_set_int64 (value, klass->adapter_luid);
      break;
    case PROP_CUDA_DEVICE_ID:
      g_value_set_uint (value, klass->cuda_device_id);
      break;
    case PROP_PRESET:
      g_value_set_enum (value, self->preset);
      break;
    case PROP_WEIGHTED_PRED:
      g_value_set_boolean (value, self->weighted_pred);
      break;
    case PROP_GOP_SIZE:
      g_value_set_int (value, self->gop_size);
      break;
    case PROP_B_FRAMES:
      g_value_set_uint (value, self->bframes);
      break;
    case PROP_RC_MODE:
      g_value_set_enum (value, self->rc_mode);
      break;
    case PROP_QP_CONST_I:
      g_value_set_int (value, self->qp_const_i);
      break;
    case PROP_QP_CONST_P:
      g_value_set_int (value, self->qp_const_p);
      break;
    case PROP_QP_CONST_B:
      g_value_set_int (value, self->qp_const_b);
      break;
    case PROP_BITRATE:
      g_value_set_uint (value, self->bitrate);
      break;
    case PROP_MAX_BITRATE:
      g_value_set_uint (value, self->max_bitrate);
      break;
    case PROP_VBV_BUFFER_SIZE:
      g_value_set_uint (value, self->vbv_buffer_size);
      break;
    case PROP_RC_LOOKAHEAD:
      g_value_set_uint (value, self->rc_lookahead);
      break;
    case PROP_I_ADAPT:
      g_value_set_boolean (value, self->i_adapt);
      break;
    case PROP_B_ADAPT:
      g_value_set_boolean (value, self->b_adapt);
      break;
    case PROP_SPATIAL_AQ:
      g_value_set_boolean (value, self->spatial_aq);
      break;
    case PROP_TEMPORAL_AQ:
      g_value_set_boolean (value, self->temporal_aq);
      break;
    case PROP_ZERO_LATENCY:
      g_value_set_boolean (value, self->zero_latency);
      break;
    case PROP_NON_REF_P:
      g_value_set_boolean (value, self->non_ref_p);
      break;
    case PROP_STRICT_GOP:
      g_value_set_boolean (value, self->strict_gop);
      break;
    case PROP_AQ_STRENGTH:
      g_value_set_uint (value, self->aq_strength);
      break;
    case PROP_QP_MIN_I:
      g_value_set_int (value, self->qp_min_i);
      break;
    case PROP_QP_MIN_P:
      g_value_set_int (value, self->qp_min_p);
      break;
    case PROP_QP_MIN_B:
      g_value_set_int (value, self->qp_min_b);
      break;
    case PROP_QP_MAX_I:
      g_value_set_int (value, self->qp_max_i);
      break;
    case PROP_QP_MAX_P:
      g_value_set_int (value, self->qp_max_p);
      break;
    case PROP_QP_MAX_B:
      g_value_set_int (value, self->qp_max_b);
      break;
    case PROP_CONST_QUALITY:
      g_value_set_double (value, self->const_quality);
      break;
    case PROP_AUD:
      g_value_set_boolean (value, self->aud);
      break;
    case PROP_CABAC:
      g_value_set_boolean (value, self->cabac);
      break;
    case PROP_REPEAT_SEQUENCE_HEADER:
      g_value_set_boolean (value, self->repeat_sequence_header);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_nv_h264_encoder_get_downstream_profiles_and_format (GstNvH264Encoder * self,
    std::set < std::string > &downstream_profiles, gboolean * packetized)
{
  GstCaps *allowed_caps;
  GstStructure *s;
  const gchar *stream_format;

  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (self));

  if (!allowed_caps || gst_caps_is_empty (allowed_caps) ||
      gst_caps_is_any (allowed_caps)) {
    gst_clear_caps (&allowed_caps);

    return;
  }

  for (guint i = 0; i < gst_caps_get_size (allowed_caps); i++) {
    const GValue *profile_value;
    const gchar *profile;

    s = gst_caps_get_structure (allowed_caps, i);
    profile_value = gst_structure_get_value (s, "profile");
    if (!profile_value)
      continue;

    if (GST_VALUE_HOLDS_LIST (profile_value)) {
      for (guint j = 0; j < gst_value_list_get_size (profile_value); j++) {
        const GValue *p = gst_value_list_get_value (profile_value, j);

        if (!G_VALUE_HOLDS_STRING (p))
          continue;

        profile = g_value_get_string (p);
        if (profile)
          downstream_profiles.insert (profile);
      }

    } else if (G_VALUE_HOLDS_STRING (profile_value)) {
      profile = g_value_get_string (profile_value);
      if (profile)
        downstream_profiles.insert (profile);
    }
  }

  if (packetized) {
    *packetized = FALSE;
    allowed_caps = gst_caps_fixate (allowed_caps);
    s = gst_caps_get_structure (allowed_caps, 0);
    stream_format = gst_structure_get_string (s, "stream-format");
    if (g_strcmp0 (stream_format, "avc") == 0)
      *packetized = TRUE;
  }

  gst_caps_unref (allowed_caps);
}

static GstCaps *
gst_nv_h264_encoder_getcaps (GstVideoEncoder * encoder, GstCaps * filter)
{
  GstNvH264Encoder *self = GST_NV_H264_ENCODER (encoder);
  GstNvH264EncoderClass *klass = GST_NV_H264_ENCODER_GET_CLASS (self);
  GstCaps *allowed_caps;
  GstCaps *template_caps;
  GstCaps *filtered_caps;
  GstCaps *supported_caps;
  std::set < std::string > downstream_profiles;
  std::set < std::string > allowed_formats;
  gboolean profile_support_interlaced = FALSE;

  gst_nv_h264_encoder_get_downstream_profiles_and_format (self,
      downstream_profiles, NULL);

  GST_DEBUG_OBJECT (self, "Downstream specified %" G_GSIZE_FORMAT " profiles",
      downstream_profiles.size ());

  if (downstream_profiles.size () == 0)
    return gst_video_encoder_proxy_getcaps (encoder, NULL, filter);

  /* *INDENT-OFF* */
  for (const auto &iter: downstream_profiles) {
    if (iter == "high" || iter == "main") {
      profile_support_interlaced = TRUE;
    }

    if (iter == "high-4:4:4") {
      profile_support_interlaced = TRUE;
      allowed_formats.insert("Y444");
    } else {
      allowed_formats.insert("NV12");
    }
  }
  /* *INDENT-ON* */

  GST_DEBUG_OBJECT (self, "Downstream %s support interlaced format",
      profile_support_interlaced ? "can" : "cannot");

  template_caps = gst_pad_get_pad_template_caps (encoder->sinkpad);
  allowed_caps = gst_caps_copy (template_caps);

  if (klass->dev_caps.field_encoding == 0 || !profile_support_interlaced) {
    gst_caps_set_simple (allowed_caps, "interlace-mode", G_TYPE_STRING,
        "progressive", NULL);
  }

  GValue formats = G_VALUE_INIT;

  g_value_init (&formats, GST_TYPE_LIST);
  /* *INDENT-OFF* */
  for (const auto &iter: allowed_formats) {
    GValue val = G_VALUE_INIT;
    g_value_init (&val, G_TYPE_STRING);

    g_value_set_string (&val, iter.c_str());
    gst_value_list_append_and_take_value (&formats, &val);
  }
  /* *INDENT-ON* */

  gst_caps_set_value (allowed_caps, "format", &formats);
  g_value_unset (&formats);

  filtered_caps = gst_caps_intersect_full (template_caps, allowed_caps,
      GST_CAPS_INTERSECT_FIRST);

  supported_caps = gst_video_encoder_proxy_getcaps (encoder,
      filtered_caps, filter);
  gst_caps_unref (filtered_caps);
  gst_caps_unref (allowed_caps);
  gst_caps_unref (template_caps);

  GST_DEBUG_OBJECT (self, "Returning %" GST_PTR_FORMAT, supported_caps);

  return supported_caps;
}

static gboolean
gst_nv_h264_encoder_set_format (GstNvEncoder * encoder,
    GstVideoCodecState * state, gpointer session,
    NV_ENC_INITIALIZE_PARAMS * init_params, NV_ENC_CONFIG * config)
{
  GstNvH264Encoder *self = GST_NV_H264_ENCODER (encoder);
  GstNvH264EncoderClass *klass = GST_NV_H264_ENCODER_GET_CLASS (self);
  GstNvH264EncoderDeviceCaps *dev_caps = &klass->dev_caps;
  NV_ENC_RC_PARAMS *rc_params;
  GstVideoInfo *info = &state->info;
  NVENCSTATUS status;
  NV_ENC_PRESET_CONFIG preset_config = { 0, };
  gint dar_n, dar_d;
  GstNvEncoderRCMode rc_mode;
  NV_ENC_CONFIG_H264 *h264_config;
  NV_ENC_CONFIG_H264_VUI_PARAMETERS *vui;
  std::set < std::string > downstream_profiles;
  GUID selected_profile = NV_ENC_CODEC_PROFILE_AUTOSELECT_GUID;
  gboolean downstream_supports_bframe = FALSE;
  gboolean bframe_aborted = FALSE;

  self->packetized = FALSE;

  gst_nv_h264_encoder_get_downstream_profiles_and_format (self,
      downstream_profiles, &self->packetized);

  if (downstream_profiles.empty ()) {
    GST_ERROR_OBJECT (self, "Unable to get downstream profile");
    return FALSE;
  }

  if (GST_VIDEO_INFO_IS_INTERLACED (info)) {
    downstream_profiles.erase ("progressive-high");
    downstream_profiles.erase ("constrained-high");
    downstream_profiles.erase ("constrained-baseline");
    downstream_profiles.erase ("baseline");

    if (downstream_profiles.empty ()) {
      GST_ERROR_OBJECT (self,
          "None of downstream profile supports interlaced encoding");
      return FALSE;
    }
  }

  if (GST_VIDEO_INFO_FORMAT (info) == GST_VIDEO_FORMAT_Y444) {
    if (downstream_profiles.find ("high-4:4:4") == downstream_profiles.end ()) {
      GST_ERROR_OBJECT (self, "Downstream does not support 4:4:4 profile");
      return FALSE;
    } else {
      selected_profile = NV_ENC_H264_PROFILE_HIGH_444_GUID;
      downstream_supports_bframe = TRUE;
    }
  } else {
    /* *INDENT-OFF* */
    for (const auto &iter: downstream_profiles) {
      if (iter == "high" || iter == "main" || iter == "progressive-high") {
        downstream_supports_bframe = TRUE;
      }
    }
    /* *INDENT-ON* */
  }

  g_mutex_lock (&self->prop_lock);

  init_params->version = gst_nvenc_get_initialize_params_version ();
  init_params->encodeGUID = NV_ENC_CODEC_H264_GUID;

  init_params->encodeWidth = GST_VIDEO_INFO_WIDTH (info);
  init_params->maxEncodeWidth = GST_VIDEO_INFO_WIDTH (info);
  init_params->encodeHeight = GST_VIDEO_INFO_HEIGHT (info);
  init_params->maxEncodeHeight = GST_VIDEO_INFO_HEIGHT (info);
  init_params->enablePTD = TRUE;
  if (dev_caps->async_encoding_support)
    init_params->enableEncodeAsync = 1;
  if (info->fps_d > 0 && info->fps_n > 0) {
    init_params->frameRateNum = info->fps_n;
    init_params->frameRateDen = info->fps_d;
  } else {
    init_params->frameRateNum = 0;
    init_params->frameRateDen = 1;
  }

  init_params->enableWeightedPrediction = self->weighted_pred;

  if (gst_util_fraction_multiply (GST_VIDEO_INFO_WIDTH (info),
          GST_VIDEO_INFO_HEIGHT (info), GST_VIDEO_INFO_PAR_N (info),
          GST_VIDEO_INFO_PAR_D (info), &dar_n, &dar_d) && dar_n > 0
      && dar_d > 0) {
    init_params->darWidth = dar_n;
    init_params->darHeight = dar_d;
  }

  if (GST_VIDEO_INFO_IS_INTERLACED (info) && dev_caps->field_encoding > 0) {
    switch (GST_VIDEO_INFO_INTERLACE_MODE (info)) {
      case GST_VIDEO_INTERLACE_MODE_INTERLEAVED:
      case GST_VIDEO_INTERLACE_MODE_MIXED:
        config->frameFieldMode = NV_ENC_PARAMS_FRAME_FIELD_MODE_FIELD;
        preset_config.presetCfg.frameFieldMode =
            NV_ENC_PARAMS_FRAME_FIELD_MODE_FIELD;
        break;
      default:
        break;
    }
  }

  gst_nv_encoder_preset_to_guid (self->preset, &init_params->presetGUID);

  preset_config.version = gst_nvenc_get_preset_config_version ();
  preset_config.presetCfg.version = gst_nvenc_get_config_version ();

  status = NvEncGetEncodePresetConfig (session, NV_ENC_CODEC_H264_GUID,
      init_params->presetGUID, &preset_config);
  if (status != NV_ENC_SUCCESS) {
    GST_ERROR_OBJECT (self, "Failed to get preset config %"
        GST_NVENC_STATUS_FORMAT, GST_NVENC_STATUS_ARGS (status));
    g_mutex_unlock (&self->prop_lock);
    return FALSE;
  }

  *config = preset_config.presetCfg;
  if (self->gop_size < 0) {
    config->gopLength = NVENC_INFINITE_GOPLENGTH;
    config->frameIntervalP = 1;
  } else if (self->gop_size > 0) {
    config->gopLength = self->gop_size;
    /* frameIntervalP
     * 0: All Intra frames
     * 1: I/P only
     * 2: IBP
     * 3: IBBP
     */
    config->frameIntervalP = 1;
    if (self->bframes > 0 && !downstream_supports_bframe) {
      GST_WARNING_OBJECT (self,
          "B-frame was enabled but downstream profile does not support it");
      bframe_aborted = TRUE;
      self->bframes = 0;
    }

    config->frameIntervalP = self->bframes + 1;
  } else {
    /* gop size == 0 means all intra frames */
    config->gopLength = 1;
    config->frameIntervalP = 0;
  }

  rc_params = &config->rcParams;
  rc_mode = self->rc_mode;

  if (self->bitrate)
    rc_params->averageBitRate = self->bitrate * 1024;
  if (self->max_bitrate)
    rc_params->maxBitRate = self->max_bitrate * 1024;
  if (self->vbv_buffer_size)
    rc_params->vbvBufferSize = self->vbv_buffer_size * 1024;

  if (rc_mode == GST_NV_ENCODER_RC_MODE_DEFAULT) {
    if (self->qp_const_i >= 0)
      rc_mode = GST_NV_ENCODER_RC_MODE_CONSTQP;
  }

  if (self->qp_min_i >= 0) {
    rc_params->enableMinQP = TRUE;
    rc_params->minQP.qpIntra = self->qp_min_i;
    if (self->qp_min_p >= 0) {
      rc_params->minQP.qpInterP = self->qp_min_p;
    } else {
      rc_params->minQP.qpInterP = rc_params->minQP.qpIntra;
    }
    if (self->qp_min_b >= 0) {
      rc_params->minQP.qpInterB = self->qp_min_b;
    } else {
      rc_params->minQP.qpInterB = rc_params->minQP.qpInterP;
    }
  }

  if (self->qp_max_i >= 0) {
    rc_params->enableMaxQP = TRUE;
    rc_params->maxQP.qpIntra = self->qp_max_i;
    if (self->qp_max_p >= 0) {
      rc_params->maxQP.qpInterP = self->qp_max_p;
    } else {
      rc_params->maxQP.qpInterP = rc_params->maxQP.qpIntra;
    }
    if (self->qp_max_b >= 0) {
      rc_params->maxQP.qpInterB = self->qp_max_b;
    } else {
      rc_params->maxQP.qpInterB = rc_params->maxQP.qpInterP;
    }
  }

  if (rc_mode == GST_NV_ENCODER_RC_MODE_CONSTQP && self->qp_const_i >= 0) {
    rc_params->enableMaxQP = TRUE;
    rc_params->maxQP.qpIntra = self->qp_max_i;
    if (self->qp_max_p >= 0) {
      rc_params->maxQP.qpInterP = self->qp_max_p;
    } else {
      rc_params->maxQP.qpInterP = rc_params->maxQP.qpIntra;
    }
    if (self->qp_max_b >= 0) {
      rc_params->maxQP.qpInterB = self->qp_max_b;
    } else {
      rc_params->maxQP.qpInterB = rc_params->maxQP.qpInterP;
    }
  }

  rc_params->rateControlMode = gst_nv_encoder_rc_mode_to_native (rc_mode);

  if (self->spatial_aq) {
    rc_params->enableAQ = TRUE;
    rc_params->aqStrength = self->aq_strength;
  }

  rc_params->enableTemporalAQ = self->temporal_aq;

  if (self->rc_lookahead) {
    rc_params->enableLookahead = 1;
    rc_params->lookaheadDepth = self->rc_lookahead;
    rc_params->disableIadapt = !self->i_adapt;
    rc_params->disableBadapt = !self->b_adapt;
  }

  rc_params->strictGOPTarget = self->strict_gop;
  rc_params->enableNonRefP = self->non_ref_p;
  rc_params->zeroReorderDelay = self->zero_latency;

  if (self->const_quality) {
    guint scaled = (gint) (self->const_quality * 256.0);

    rc_params->targetQuality = (guint8) (scaled >> 8);
    rc_params->targetQualityLSB = (guint8) (scaled & 0xff);
  }
  self->init_param_updated = FALSE;
  self->bitrate_updated = FALSE;
  self->rc_param_updated = FALSE;

  if (selected_profile == NV_ENC_CODEC_PROFILE_AUTOSELECT_GUID &&
      config->frameIntervalP > 1) {
    if (downstream_profiles.find ("main") != downstream_profiles.end ()) {
      selected_profile = NV_ENC_H264_PROFILE_MAIN_GUID;
    } else if (downstream_profiles.find ("high") != downstream_profiles.end ()) {
      selected_profile = NV_ENC_H264_PROFILE_HIGH_GUID;
    } else if (downstream_profiles.find ("progressive-high") !=
        downstream_profiles.end ()) {
      selected_profile = NV_ENC_H264_PROFILE_PROGRESSIVE_HIGH_GUID;
    }
  }

  /* Pick the first profile */
  if (selected_profile == NV_ENC_CODEC_PROFILE_AUTOSELECT_GUID) {
    if (*downstream_profiles.begin () == "baseline" ||
        *downstream_profiles.begin () == "constrained-baseline") {
      selected_profile = NV_ENC_H264_PROFILE_BASELINE_GUID;
    } else if (*downstream_profiles.begin () == "main") {
      selected_profile = NV_ENC_H264_PROFILE_MAIN_GUID;
    } else if (*downstream_profiles.begin () == "progressive-high") {
      selected_profile = NV_ENC_H264_PROFILE_PROGRESSIVE_HIGH_GUID;
    } else if (*downstream_profiles.begin () == "constrained-high") {
      selected_profile = NV_ENC_H264_PROFILE_CONSTRAINED_HIGH_GUID;
    }
  }

  config->profileGUID = selected_profile;

  h264_config = &config->encodeCodecConfig.h264Config;
  vui = &h264_config->h264VUIParameters;

  h264_config->level = NV_ENC_LEVEL_AUTOSELECT;
  h264_config->chromaFormatIDC = 1;
  if (selected_profile == NV_ENC_H264_PROFILE_HIGH_444_GUID)
    h264_config->chromaFormatIDC = 3;
  h264_config->idrPeriod = config->gopLength;
  h264_config->outputAUD = self->aud;
  if (self->repeat_sequence_header) {
    h264_config->disableSPSPPS = 0;
    h264_config->repeatSPSPPS = 1;
  } else {
    if (self->packetized)
      h264_config->disableSPSPPS = 1;
    else
      h264_config->disableSPSPPS = 0;
  }

  if (dev_caps->cabac && selected_profile != NV_ENC_H264_PROFILE_BASELINE_GUID) {
    if (self->cabac)
      h264_config->entropyCodingMode = NV_ENC_H264_ENTROPY_CODING_MODE_CABAC;
    else
      h264_config->entropyCodingMode = NV_ENC_H264_ENTROPY_CODING_MODE_CAVLC;
  } else {
    h264_config->entropyCodingMode = NV_ENC_H264_ENTROPY_CODING_MODE_AUTOSELECT;
  }

  vui->videoSignalTypePresentFlag = 1;
  /* Unspecified */
  vui->videoFormat = 5;
  if (info->colorimetry.range == GST_VIDEO_COLOR_RANGE_0_255) {
    vui->videoFullRangeFlag = 1;
  } else {
    vui->videoFullRangeFlag = 0;
  }

  vui->colourDescriptionPresentFlag = 1;
  vui->colourMatrix = gst_video_color_matrix_to_iso (info->colorimetry.matrix);
  vui->colourPrimaries =
      gst_video_color_primaries_to_iso (info->colorimetry.primaries);
  vui->transferCharacteristics =
      gst_video_transfer_function_to_iso (info->colorimetry.transfer);

  g_mutex_unlock (&self->prop_lock);

  if (bframe_aborted)
    g_object_notify (G_OBJECT (self), "bframes");

  return TRUE;
}

static gboolean
gst_nv_h264_encoder_set_output_state (GstNvEncoder * encoder,
    GstVideoCodecState * state, gpointer session)
{
  GstNvH264Encoder *self = GST_NV_H264_ENCODER (encoder);
  GstVideoCodecState *output_state;
  NV_ENC_SEQUENCE_PARAM_PAYLOAD seq_params = { 0, };
  guint8 spspps[1024];
  guint32 seq_size;
  GstCaps *caps;
  const gchar *profile_from_sps;
  NVENCSTATUS status;
  std::set < std::string > downstream_profiles;
  std::string caps_str;
  GstTagList *tags;
  GstBuffer *codec_data = NULL;
  GstH264NalUnit sps_nalu, pps_nalu;
  GstH264ParserResult rst;

  caps_str = "video/x-h264, alignment = (string) au";

  gst_nv_h264_encoder_get_downstream_profiles_and_format (self,
      downstream_profiles, NULL);

  seq_params.version = gst_nvenc_get_sequence_param_payload_version ();
  seq_params.inBufferSize = sizeof (spspps);
  seq_params.spsppsBuffer = &spspps;
  seq_params.outSPSPPSPayloadSize = &seq_size;
  status = NvEncGetSequenceParams (session, &seq_params);
  if (status != NV_ENC_SUCCESS) {
    GST_ERROR_OBJECT (self, "Failed to get sequence header, status %"
        GST_NVENC_STATUS_FORMAT, GST_NVENC_STATUS_ARGS (status));
    return FALSE;
  }

  rst = gst_h264_parser_identify_nalu (self->parser,
      spspps, 0, seq_size, &sps_nalu);
  if (rst != GST_H264_PARSER_OK) {
    GST_ERROR_OBJECT (self, "Failed to identify SPS nal");
    return FALSE;
  }

  if (sps_nalu.size < 4) {
    GST_ERROR_OBJECT (self, "Too small sps nal size %d", sps_nalu.size);
    return FALSE;
  }

  rst = gst_h264_parser_identify_nalu_unchecked (self->parser,
      spspps, sps_nalu.offset + sps_nalu.size, seq_size, &pps_nalu);
  if (rst != GST_H264_PARSER_OK && self->packetized) {
    GST_ERROR_OBJECT (self, "Failed to identify PPS nal, %d", rst);
    return FALSE;
  }

  if (self->packetized) {
    GstMapInfo info;
    guint8 *data;
    guint8 profile_idc, profile_comp, level_idc;
    const guint nal_length_size = 4;
    const guint num_sps = 1;
    const guint num_pps = 1;

    data = sps_nalu.data + sps_nalu.offset + sps_nalu.header_bytes;
    profile_idc = data[0];
    profile_comp = data[1];
    level_idc = data[2];

    /* 5: configuration version, profile, compatibility, level, nal length
     * 1: num sps
     * 2: sps size bytes
     * sizeof (sps)
     * 1: num pps
     * 2: pps size bytes
     * sizeof (pps)
     *
     * -> 11 + sps_size + pps_size
     */
    codec_data = gst_buffer_new_and_alloc (11 + sps_nalu.size + pps_nalu.size);

    gst_buffer_map (codec_data, &info, GST_MAP_WRITE);

    data = (guint8 *) info.data;
    data[0] = 1;
    data[1] = profile_idc;
    data[2] = profile_comp;
    data[3] = level_idc;
    data[4] = 0xfc | (nal_length_size - 1);
    data[5] = 0xe0 | num_sps;
    data += 6;
    GST_WRITE_UINT16_BE (data, sps_nalu.size);
    data += 2;
    memcpy (data, sps_nalu.data + sps_nalu.offset, sps_nalu.size);
    data += sps_nalu.size;

    data[0] = num_pps;
    data++;

    GST_WRITE_UINT16_BE (data, pps_nalu.size);
    data += 2;
    memcpy (data, pps_nalu.data + pps_nalu.offset, pps_nalu.size);

    gst_buffer_unmap (codec_data, &info);
  }

  profile_from_sps =
      gst_codec_utils_h264_get_profile (sps_nalu.data + sps_nalu.offset +
      sps_nalu.header_bytes, 3);

  if (!profile_from_sps) {
    GST_WARNING_OBJECT (self, "Failed to parse profile from SPS");
  } else if (!downstream_profiles.empty ()) {
    if (downstream_profiles.find (profile_from_sps) !=
        downstream_profiles.end ()) {
      caps_str += ", profile = (string) " + std::string (profile_from_sps);
    } else if (downstream_profiles.find ("baseline") !=
        downstream_profiles.end () &&
        strcmp (profile_from_sps, "constrained-baseline") == 0) {
      caps_str += ", profile = (string) baseline";
    } else if (downstream_profiles.find ("constrained-baseline") !=
        downstream_profiles.end () &&
        strcmp (profile_from_sps, "constrained-baseline") == 0) {
      caps_str += ", profile = (string) constrained-baseline";
    }
  } else {
    caps_str += ", profile = (string) " + std::string (profile_from_sps);
  }

  if (self->packetized) {
    caps_str += ", stream-format = (string) avc";
  } else {
    caps_str += ", stream-format = (string) byte-stream";
  }

  caps = gst_caps_from_string (caps_str.c_str ());

  if (self->packetized) {
    gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, codec_data, NULL);
    gst_buffer_unref (codec_data);
  }

  output_state = gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (self),
      caps, state);

  GST_INFO_OBJECT (self, "Output caps: %" GST_PTR_FORMAT, output_state->caps);
  gst_video_codec_state_unref (output_state);

  tags = gst_tag_list_new_empty ();
  gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE, GST_TAG_ENCODER,
      "nvh264encoder", NULL);

  gst_video_encoder_merge_tags (GST_VIDEO_ENCODER (encoder),
      tags, GST_TAG_MERGE_REPLACE);
  gst_tag_list_unref (tags);

  return TRUE;
}

static GstBuffer *
gst_nv_h264_encoder_create_output_buffer (GstNvEncoder * encoder,
    NV_ENC_LOCK_BITSTREAM * bitstream)
{
  GstNvH264Encoder *self = GST_NV_H264_ENCODER (encoder);
  GstBuffer *buffer;
  GstH264ParserResult rst;
  GstH264NalUnit nalu;

  if (!self->packetized) {
    return gst_buffer_new_memdup (bitstream->bitstreamBufferPtr,
        bitstream->bitstreamSizeInBytes);
  }

  buffer = gst_buffer_new ();
  rst = gst_h264_parser_identify_nalu (self->parser,
      (guint8 *) bitstream->bitstreamBufferPtr, 0,
      bitstream->bitstreamSizeInBytes, &nalu);

  if (rst == GST_H264_PARSER_NO_NAL_END)
    rst = GST_H264_PARSER_OK;

  while (rst == GST_H264_PARSER_OK) {
    GstMemory *mem;
    guint8 *data;

    data = (guint8 *) g_malloc0 (nalu.size + 4);
    GST_WRITE_UINT32_BE (data, nalu.size);
    memcpy (data + 4, nalu.data + nalu.offset, nalu.size);

    mem = gst_memory_new_wrapped ((GstMemoryFlags) 0, data, nalu.size + 4,
        0, nalu.size + 4, data, (GDestroyNotify) g_free);
    gst_buffer_append_memory (buffer, mem);

    rst = gst_h264_parser_identify_nalu (self->parser,
        (guint8 *) bitstream->bitstreamBufferPtr, nalu.offset + nalu.size,
        bitstream->bitstreamSizeInBytes, &nalu);

    if (rst == GST_H264_PARSER_NO_NAL_END)
      rst = GST_H264_PARSER_OK;
  }

  return buffer;
}

static GstNvEncoderReconfigure
gst_nv_h264_encoder_check_reconfigure (GstNvEncoder * encoder,
    NV_ENC_CONFIG * config)
{
  GstNvH264Encoder *self = GST_NV_H264_ENCODER (encoder);
  GstNvEncoderReconfigure reconfig = GST_NV_ENCODER_RECONFIGURE_NONE;

  /* Dynamic RC param update is not tested, do soft-reconfigure only for
   * bitrate update */
  g_mutex_lock (&self->prop_lock);
  if (self->init_param_updated || self->rc_param_updated) {
    reconfig = GST_NV_ENCODER_RECONFIGURE_FULL;
    goto done;
  }

  if (self->bitrate_updated) {
    GstNvH264EncoderClass *klass = GST_NV_H264_ENCODER_GET_CLASS (self);
    if (klass->dev_caps.dyn_bitrate_change > 0) {
      config->rcParams.averageBitRate = self->bitrate * 1024;
      config->rcParams.maxBitRate = self->max_bitrate * 1024;
      reconfig = GST_NV_ENCODER_RECONFIGURE_BITRATE;
    } else {
      reconfig = GST_NV_ENCODER_RECONFIGURE_FULL;
    }
  }

done:
  self->init_param_updated = FALSE;
  self->rc_param_updated = FALSE;
  self->bitrate_updated = FALSE;
  g_mutex_unlock (&self->prop_lock);

  return reconfig;
}

static GstNvH264EncoderClassData *
gst_nv_h264_encoder_create_class_data (GstObject * device, gpointer session,
    gboolean d3d11_mode)
{
  NVENCSTATUS status;
  GstNvH264EncoderDeviceCaps dev_caps = { 0, };
  NV_ENC_CAPS_PARAM caps_param = { 0, };
  GUID profile_guids[16];
  NV_ENC_BUFFER_FORMAT input_formats[16];
  guint32 profile_guid_count = 0;
  guint32 input_format_count = 0;
  std::string sink_caps_str;
  std::string src_caps_str;
  std::string format_str;
  std::set < std::string > formats;
  std::set < std::string > profiles;
  std::string profile_str;
  std::string resolution_str;
  GstNvH264EncoderClassData *cdata;
  GstCaps *sink_caps;
  GstCaps *system_caps;

  status = NvEncGetEncodeProfileGUIDs (session, NV_ENC_CODEC_H264_GUID,
      profile_guids, G_N_ELEMENTS (profile_guids), &profile_guid_count);
  if (status != NV_ENC_SUCCESS || profile_guid_count == 0) {
    GST_WARNING_OBJECT (device, "Unable to get supported profiles");
    return NULL;
  }

  status = NvEncGetInputFormats (session, NV_ENC_CODEC_H264_GUID, input_formats,
      G_N_ELEMENTS (input_formats), &input_format_count);
  if (status != NV_ENC_SUCCESS || input_format_count == 0) {
    GST_WARNING_OBJECT (device, "Unable to get supported input formats");
    return NULL;
  }

  caps_param.version = gst_nvenc_get_caps_param_version ();

#define CHECK_CAPS(to_query,val,default_val) G_STMT_START { \
  gint _val; \
  caps_param.capsToQuery = to_query; \
  status = NvEncGetEncodeCaps (session, NV_ENC_CODEC_H264_GUID, &caps_param, \
      &_val); \
  if (status != NV_ENC_SUCCESS) { \
    GST_WARNING_OBJECT (device, "Unable to query %s, status: %" \
        GST_NVENC_STATUS_FORMAT, G_STRINGIFY (to_query), \
        GST_NVENC_STATUS_ARGS (status)); \
    val = default_val; \
  } else { \
    GST_DEBUG_OBJECT (device, "%s: %d", G_STRINGIFY (to_query), _val); \
    val = _val; \
  } \
} G_STMT_END

  CHECK_CAPS (NV_ENC_CAPS_NUM_MAX_BFRAMES, dev_caps.max_bframes, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORTED_RATECONTROL_MODES,
      dev_caps.ratecontrol_modes, NV_ENC_PARAMS_RC_VBR);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_FIELD_ENCODING, dev_caps.field_encoding, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_MONOCHROME, dev_caps.monochrome, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_FMO, dev_caps.fmo, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_QPELMV, dev_caps.qpelmv, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_BDIRECT_MODE, dev_caps.bdirect_mode, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_CABAC, dev_caps.cabac, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_ADAPTIVE_TRANSFORM,
      dev_caps.adaptive_transform, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_STEREO_MVC, dev_caps.stereo_mvc, 0);
  CHECK_CAPS (NV_ENC_CAPS_NUM_MAX_TEMPORAL_LAYERS, dev_caps.temoral_layers, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_HIERARCHICAL_PFRAMES,
      dev_caps.hierarchical_pframes, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_HIERARCHICAL_BFRAMES,
      dev_caps.hierarchical_bframes, 0);
  CHECK_CAPS (NV_ENC_CAPS_LEVEL_MAX, dev_caps.level_max, 0);
  CHECK_CAPS (NV_ENC_CAPS_LEVEL_MIN, dev_caps.level_min, 0);
  CHECK_CAPS (NV_ENC_CAPS_SEPARATE_COLOUR_PLANE,
      dev_caps.seperate_colour_plane, 0);
  CHECK_CAPS (NV_ENC_CAPS_WIDTH_MAX, dev_caps.width_max, 4096);
  CHECK_CAPS (NV_ENC_CAPS_HEIGHT_MAX, dev_caps.height_max, 4096);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_TEMPORAL_SVC, dev_caps.temporal_svc, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_DYN_RES_CHANGE, dev_caps.dyn_res_change, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_DYN_BITRATE_CHANGE,
      dev_caps.dyn_bitrate_change, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_DYN_FORCE_CONSTQP,
      dev_caps.dyn_force_constqp, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_DYN_RCMODE_CHANGE,
      dev_caps.dyn_rcmode_change, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_SUBFRAME_READBACK,
      dev_caps.subframe_readback, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_CONSTRAINED_ENCODING,
      dev_caps.constrained_encoding, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_INTRA_REFRESH, dev_caps.intra_refresh, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_CUSTOM_VBV_BUF_SIZE,
      dev_caps.custom_vbv_buf_size, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_DYNAMIC_SLICE_MODE,
      dev_caps.dynamic_slice_mode, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_REF_PIC_INVALIDATION,
      dev_caps.ref_pic_invalidation, 0);
  CHECK_CAPS (NV_ENC_CAPS_PREPROC_SUPPORT, dev_caps.preproc_support, 0);
  /* NOTE: Async is Windows only */
#ifdef G_OS_WIN32
  CHECK_CAPS (NV_ENC_CAPS_ASYNC_ENCODE_SUPPORT,
      dev_caps.async_encoding_support, 0);
#endif
  CHECK_CAPS (NV_ENC_CAPS_MB_NUM_MAX, dev_caps.mb_num_max, 0);
  CHECK_CAPS (NV_ENC_CAPS_MB_PER_SEC_MAX, dev_caps.mb_per_sec_max, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_YUV444_ENCODE, dev_caps.yuv444_encode, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_LOSSLESS_ENCODE, dev_caps.lossless_encode, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_MEONLY_MODE, dev_caps.meonly_mode, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_LOOKAHEAD, dev_caps.lookahead, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_TEMPORAL_AQ, dev_caps.temporal_aq, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_10BIT_ENCODE,
      dev_caps.supports_10bit_encode, 0);
  CHECK_CAPS (NV_ENC_CAPS_NUM_MAX_LTR_FRAMES, dev_caps.num_max_ltr_frames, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_WEIGHTED_PREDICTION,
      dev_caps.weighted_prediction, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_BFRAME_REF_MODE, dev_caps.bframe_ref_mode, 0);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_EMPHASIS_LEVEL_MAP,
      dev_caps.emphasis_level_map, 0);
  CHECK_CAPS (NV_ENC_CAPS_WIDTH_MIN, dev_caps.width_min, 16);
  CHECK_CAPS (NV_ENC_CAPS_HEIGHT_MIN, dev_caps.height_min, 16);
  CHECK_CAPS (NV_ENC_CAPS_SUPPORT_MULTIPLE_REF_FRAMES,
      dev_caps.multiple_ref_frames, 0);
#undef CHECK_CAPS

  for (guint32 i = 0; i < input_format_count; i++) {
    switch (input_formats[i]) {
      case NV_ENC_BUFFER_FORMAT_NV12:
        formats.insert ("NV12");
        break;
      case NV_ENC_BUFFER_FORMAT_YUV444:
        if (dev_caps.yuv444_encode)
          formats.insert ("Y444");
        break;
      default:
        break;
    }
  }

  if (formats.empty ()) {
    GST_WARNING_OBJECT (device, "Empty supported input format");
    return NULL;
  }
#define APPEND_STRING(dst,set,str) G_STMT_START { \
  if (set.find(str) != set.end()) { \
    if (!first) \
      dst += ", "; \
    dst += str; \
    first = false; \
  } \
} G_STMT_END

  if (formats.size () == 1) {
    format_str = "format = (string) " + *(formats.begin ());
  } else {
    bool first = true;

    format_str = "format = (string) { ";
    APPEND_STRING (format_str, formats, "NV12");
    APPEND_STRING (format_str, formats, "Y444");
    format_str += " }";
  }

  for (guint32 i = 0; i < profile_guid_count; i++) {
    if (profile_guids[i] == NV_ENC_H264_PROFILE_BASELINE_GUID) {
      profiles.insert ("baseline");
      profiles.insert ("constrained-baseline");
    } else if (profile_guids[i] == NV_ENC_H264_PROFILE_MAIN_GUID) {
      profiles.insert ("main");
    } else if (profile_guids[i] == NV_ENC_H264_PROFILE_HIGH_GUID) {
      profiles.insert ("high");
    } else if (profile_guids[i] == NV_ENC_H264_PROFILE_HIGH_444_GUID) {
      profiles.insert ("high-4:4:4");
    } else if (profile_guids[i] == NV_ENC_H264_PROFILE_PROGRESSIVE_HIGH_GUID) {
      profiles.insert ("progressive-high");
    } else if (profile_guids[i] == NV_ENC_H264_PROFILE_CONSTRAINED_HIGH_GUID) {
      profiles.insert ("constrained-high");
    }
  }

  if (profiles.empty ()) {
    GST_WARNING_OBJECT (device, "Empty supported h264 profile");
    return NULL;
  }

  if (profiles.size () == 1) {
    profile_str = "profile = (string) " + *(profiles.begin ());
  } else {
    bool first = true;

    profile_str = "profile = (string) { ";
    APPEND_STRING (profile_str, profiles, "main");
    APPEND_STRING (profile_str, profiles, "high");
    APPEND_STRING (profile_str, profiles, "progressive-high");
    APPEND_STRING (profile_str, profiles, "constrained-high");
    APPEND_STRING (profile_str, profiles, "constrained-baseline");
    APPEND_STRING (profile_str, profiles, "baseline");
    APPEND_STRING (profile_str, profiles, "high-4:4:4");
    profile_str += " }";
  }
#undef APPEND_STRING

  resolution_str = "width = (int) [ " +
      std::to_string (GST_ROUND_UP_16 (dev_caps.width_min))
      + ", " + std::to_string (dev_caps.width_max) + " ]";
  resolution_str += ", height = (int) [ " +
      std::to_string (GST_ROUND_UP_16 (dev_caps.height_min))
      + ", " + std::to_string (dev_caps.height_max) + " ]";

  sink_caps_str = "video/x-raw, " + format_str + ", " + resolution_str;

  if (dev_caps.field_encoding > 0) {
    sink_caps_str += ", interlace-mode = (string) { interleaved, mixed }";
  } else {
    sink_caps_str += ", interlace-mode = (string) progressive";
  }

  src_caps_str = "video/x-h264, " + resolution_str + ", " + profile_str +
      ", stream-format = (string) { avc, byte-stream }, alignment = (string) au";

  system_caps = gst_caps_from_string (sink_caps_str.c_str ());
  sink_caps = gst_caps_copy (system_caps);
#ifdef HAVE_NVCODEC_GST_D3D11
  if (d3d11_mode) {
    gst_caps_set_features (sink_caps, 0,
        gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, NULL));
  } else
#endif
  {
    gst_caps_set_features (sink_caps, 0,
        gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, NULL));
  }

  gst_caps_append (sink_caps, system_caps);

  cdata = g_new0 (GstNvH264EncoderClassData, 1);
  cdata->sink_caps = sink_caps;
  cdata->src_caps = gst_caps_from_string (src_caps_str.c_str ());
  cdata->dev_caps = dev_caps;
  cdata->d3d11_mode = d3d11_mode;
  if (d3d11_mode)
    g_object_get (device, "adapter-luid", &cdata->adapter_luid, NULL);
  else
    g_object_get (device, "cuda-device-id", &cdata->cuda_device_id, NULL);

  GST_MINI_OBJECT_FLAG_SET (cdata->sink_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (cdata->src_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  return cdata;
}

void
gst_nv_h264_encoder_register_cuda (GstPlugin * plugin, GstCudaContext * context,
    guint rank)
{
  NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS session_params = { 0, };
  gpointer session;
  NVENCSTATUS status;
  GstNvH264EncoderClassData *cdata;

  GST_DEBUG_CATEGORY_INIT (gst_nv_h264_encoder_debug, "nvh264encoder", 0,
      "nvh264encoder");

  session_params.version =
      gst_nvenc_get_open_encode_session_ex_params_version ();
  session_params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
  session_params.device = gst_cuda_context_get_handle (context);
  session_params.apiVersion = gst_nvenc_get_api_version ();

  status = NvEncOpenEncodeSessionEx (&session_params, &session);
  if (status != NV_ENC_SUCCESS) {
    GST_WARNING_OBJECT (context, "Failed to open session");
    return;
  }

  cdata = gst_nv_h264_encoder_create_class_data (GST_OBJECT (context), session,
      FALSE);
  NvEncDestroyEncoder (session);

  if (!cdata)
    return;

  GType type;
  gchar *type_name;
  gchar *feature_name;
  GTypeInfo type_info = {
    sizeof (GstNvH264EncoderClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_nv_h264_encoder_class_init,
    NULL,
    cdata,
    sizeof (GstNvH264Encoder),
    0,
    (GInstanceInitFunc) gst_nv_h264_encoder_init,
  };

  type_name = g_strdup ("GstNvCudaH264Enc");
  feature_name = g_strdup ("nvcudah264enc");

  gint index = 0;
  while (g_type_from_name (type_name)) {
    index++;
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstNvCudaH264Device%dEnc", index);
    feature_name = g_strdup_printf ("nvcudah264device%denc", index);
  }

  type = g_type_register_static (GST_TYPE_NV_ENCODER, type_name,
      &type_info, (GTypeFlags) 0);

  if (rank > 0 && index != 0)
    rank--;

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
  g_free (feature_name);
}

#ifdef HAVE_NVCODEC_GST_D3D11
void
gst_nv_h264_encoder_register_d3d11 (GstPlugin * plugin, GstD3D11Device * device,
    guint rank)
{
  NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS session_params = { 0, };
  gpointer session;
  NVENCSTATUS status;
  GstNvH264EncoderClassData *cdata;

  GST_DEBUG_CATEGORY_INIT (gst_nv_h264_encoder_debug, "nvh264encoder", 0,
      "nvh264encoder");

  session_params.version =
      gst_nvenc_get_open_encode_session_ex_params_version ();
  session_params.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
  session_params.device = gst_d3d11_device_get_device_handle (device);
  session_params.apiVersion = gst_nvenc_get_api_version ();

  status = NvEncOpenEncodeSessionEx (&session_params, &session);
  if (status != NV_ENC_SUCCESS) {
    GST_WARNING_OBJECT (device, "Failed to open session");
    return;
  }

  cdata = gst_nv_h264_encoder_create_class_data (GST_OBJECT (device), session,
      TRUE);
  NvEncDestroyEncoder (session);

  if (!cdata)
    return;

  GType type;
  gchar *type_name;
  gchar *feature_name;
  GTypeInfo type_info = {
    sizeof (GstNvH264EncoderClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_nv_h264_encoder_class_init,
    NULL,
    cdata,
    sizeof (GstNvH264Encoder),
    0,
    (GInstanceInitFunc) gst_nv_h264_encoder_init,
  };

  type_name = g_strdup ("GstNvD3D11H264Enc");
  feature_name = g_strdup ("nvd3d11h264enc");

  gint index = 0;
  while (g_type_from_name (type_name)) {
    index++;
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstNvD3D11H264Device%dEnc", index);
    feature_name = g_strdup_printf ("nvd3d11h264device%denc", index);
  }

  type = g_type_register_static (GST_TYPE_NV_ENCODER, type_name,
      &type_info, (GTypeFlags) 0);

  if (rank > 0 && index != 0)
    rank--;

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
  g_free (feature_name);
}
#endif
