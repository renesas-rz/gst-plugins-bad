/*
 * Copyright (C) 2008 Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>
 * Copyright (C) 2013 Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) 2018 Centricular Ltd.
 *   Author: Nirbheek Chauhan <nirbheek@centricular.com>
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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

#include "AsyncOperations.h"
#include "gstwasapi2client.h"
#include "gstwasapi2util.h"
#include <initguid.h>
#include <windows.foundation.h>
#include <windows.ui.core.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <string.h>
#include <string>
#include <locale>
#include <codecvt>

/* *INDENT-OFF* */
using namespace ABI::Windows::ApplicationModel::Core;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
using namespace ABI::Windows::UI::Core;
using namespace ABI::Windows::Media::Devices;
using namespace ABI::Windows::Devices::Enumeration;

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (gst_wasapi2_client_debug);
#define GST_CAT_DEFAULT gst_wasapi2_client_debug

G_END_DECLS
/* *INDENT-ON* */

static void
gst_wasapi2_client_on_device_activated (GstWasapi2Client * client,
    IAudioClient * audio_client);

/* *INDENT-OFF* */
class GstWasapiDeviceActivator
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, FtmBase,
        IActivateAudioInterfaceCompletionHandler>
{
public:
  GstWasapiDeviceActivator ()
  {
    g_weak_ref_init (&listener_, nullptr);
  }

  ~GstWasapiDeviceActivator ()
  {
    g_weak_ref_set (&listener_, nullptr);
  }

  HRESULT
  RuntimeClassInitialize (GstWasapi2Client * listener, gpointer dispatcher)
  {
    if (!listener)
      return E_INVALIDARG;

    g_weak_ref_set (&listener_, listener);

    if (dispatcher) {
      ComPtr<IInspectable> inspectable =
        reinterpret_cast<IInspectable*> (dispatcher);
      HRESULT hr;

      hr = inspectable.As (&dispatcher_);
      if (gst_wasapi2_result (hr))
        GST_INFO("Main UI dispatcher is available");
    }

    return S_OK;
  }

  STDMETHOD(ActivateCompleted)
  (IActivateAudioInterfaceAsyncOperation *async_op)
  {
    ComPtr<IAudioClient> audio_client;
    HRESULT hr = S_OK;
    HRESULT hr_async_op = S_OK;
    ComPtr<IUnknown> audio_interface;
    GstWasapi2Client *client;

    client = (GstWasapi2Client *) g_weak_ref_get (&listener_);

    if (!client) {
      this->Release ();
      GST_WARNING ("No listener was configured");
      return S_OK;
    }

    GST_INFO_OBJECT (client, "AsyncOperation done");

    hr = async_op->GetActivateResult(&hr_async_op, &audio_interface);

    if (!gst_wasapi2_result (hr)) {
      GST_WARNING_OBJECT (client, "Failed to get activate result, hr: 0x%x", hr);
      goto done;
    }

    if (!gst_wasapi2_result (hr_async_op)) {
      GST_WARNING_OBJECT (client, "Failed to activate device");
      goto done;
    }

    hr = audio_interface.As (&audio_client);
    if (!gst_wasapi2_result (hr)) {
      GST_ERROR_OBJECT (client, "Failed to get IAudioClient3 interface");
      goto done;
    }

  done:
    /* Should call this method anyway, listener will wait this event */
    gst_wasapi2_client_on_device_activated (client, audio_client.Get());
    gst_object_unref (client);
    /* return S_OK anyway, but listener can know it's succeeded or not
     * by passed IAudioClient handle via gst_wasapi2_client_on_device_activated
     */

    this->Release ();

    return S_OK;
  }

  HRESULT
  ActivateDeviceAsync(const std::wstring &device_id)
  {
    ComPtr<IAsyncAction> async_action;
    bool run_async = false;
    HRESULT hr;

    auto work_item = Callback<Implements<RuntimeClassFlags<ClassicCom>,
        IDispatchedHandler, FtmBase>>([this, device_id]{
      ComPtr<IActivateAudioInterfaceAsyncOperation> async_op;
      HRESULT async_hr = S_OK;

      async_hr = ActivateAudioInterfaceAsync (device_id.c_str (),
            __uuidof(IAudioClient3), nullptr, this, &async_op);

      /* for debugging */
      gst_wasapi2_result (async_hr);

      return async_hr;
    });

    if (dispatcher_) {
      boolean can_now;
      hr = dispatcher_->get_HasThreadAccess (&can_now);

      if (!gst_wasapi2_result (hr))
        return hr;

      if (!can_now)
        run_async = true;
    }

    if (run_async && dispatcher_) {
      hr = dispatcher_->RunAsync (CoreDispatcherPriority_Normal,
          work_item.Get (), &async_action);
    } else {
      hr = work_item->Invoke ();
    }

    /* We should hold activator object until activation callback has executed,
     * because OS doesn't hold reference of this callback COM object.
     * otherwise access violation would happen
     * See https://docs.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-activateaudiointerfaceasync
     *
     * This reference count will be decreased by self later on callback,
     * which will be called from device worker thread.
     */
    if (gst_wasapi2_result (hr))
      this->AddRef ();

    return hr;
  }

private:
  GWeakRef listener_;
  ComPtr<ICoreDispatcher> dispatcher_;
};
/* *INDENT-ON* */

typedef enum
{
  GST_WASAPI2_CLIENT_ACTIVATE_FAILED = -1,
  GST_WASAPI2_CLIENT_ACTIVATE_INIT = 0,
  GST_WASAPI2_CLIENT_ACTIVATE_WAIT,
  GST_WASAPI2_CLIENT_ACTIVATE_DONE,
} GstWasapi2ClientActivateState;

enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_DEVICE_NAME,
  PROP_DEVICE_INDEX,
  PROP_DEVICE_CLASS,
  PROP_DISPATCHER,
  PROP_CAN_AUTO_ROUTING,
};

#define DEFAULT_DEVICE_INDEX  -1
#define DEFAULT_DEVICE_CLASS  GST_WASAPI2_CLIENT_DEVICE_CLASS_CAPTURE

struct _GstWasapi2Client
{
  GstObject parent;

  GstWasapi2ClientDeviceClass device_class;
  gchar *device_id;
  gchar *device_name;
  gint device_index;
  gpointer dispatcher;
  gboolean can_auto_routing;

  IAudioClient *audio_client;
  GstWasapiDeviceActivator *activator;

  GstCaps *supported_caps;

  GThread *thread;
  GMutex lock;
  GCond cond;
  GMainContext *context;
  GMainLoop *loop;

  /* To wait ActivateCompleted event */
  GMutex init_lock;
  GCond init_cond;
  GstWasapi2ClientActivateState activate_state;
};

GType
gst_wasapi2_client_device_class_get_type (void)
{
  static GType class_type = 0;
  static const GEnumValue types[] = {
    {GST_WASAPI2_CLIENT_DEVICE_CLASS_CAPTURE, "Capture", "capture"},
    {GST_WASAPI2_CLIENT_DEVICE_CLASS_RENDER, "Render", "render"},
    {GST_WASAPI2_CLIENT_DEVICE_CLASS_LOOPBACK_CAPTURE, "Loopback-Capture",
        "loopback-capture"},
    {0, nullptr, nullptr}
  };

  if (g_once_init_enter (&class_type)) {
    GType gtype = g_enum_register_static ("GstWasapi2ClientDeviceClass", types);
    g_once_init_leave (&class_type, gtype);
  }

  return class_type;
}

static void gst_wasapi2_client_constructed (GObject * object);
static void gst_wasapi2_client_finalize (GObject * object);
static void gst_wasapi2_client_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_wasapi2_client_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static gpointer gst_wasapi2_client_thread_func (GstWasapi2Client * self);
static gboolean
gst_wasapi2_client_main_loop_running_cb (GstWasapi2Client * self);

#define gst_wasapi2_client_parent_class parent_class
G_DEFINE_TYPE (GstWasapi2Client, gst_wasapi2_client, GST_TYPE_OBJECT);

static void
gst_wasapi2_client_class_init (GstWasapi2ClientClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GParamFlags param_flags =
      (GParamFlags) (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_STATIC_STRINGS);

  gobject_class->constructed = gst_wasapi2_client_constructed;
  gobject_class->finalize = gst_wasapi2_client_finalize;
  gobject_class->get_property = gst_wasapi2_client_get_property;
  gobject_class->set_property = gst_wasapi2_client_set_property;

  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device",
          "WASAPI playback device as a GUID string", nullptr, param_flags));
  g_object_class_install_property (gobject_class, PROP_DEVICE_NAME,
      g_param_spec_string ("device-name", "Device Name",
          "The human-readable device name", nullptr, param_flags));
  g_object_class_install_property (gobject_class, PROP_DEVICE_INDEX,
      g_param_spec_int ("device-index", "Device Index",
          "The zero-based device index", -1, G_MAXINT, DEFAULT_DEVICE_INDEX,
          param_flags));
  g_object_class_install_property (gobject_class, PROP_DEVICE_CLASS,
      g_param_spec_enum ("device-class", "Device Class",
          "Device class", GST_TYPE_WASAPI2_CLIENT_DEVICE_CLASS,
          DEFAULT_DEVICE_CLASS, param_flags));
  g_object_class_install_property (gobject_class, PROP_DISPATCHER,
      g_param_spec_pointer ("dispatcher", "Dispatcher",
          "ICoreDispatcher COM object to use", param_flags));
  g_object_class_install_property (gobject_class, PROP_CAN_AUTO_ROUTING,
      g_param_spec_boolean ("auto-routing", "Auto Routing",
          "Whether client can support automatic stream routing", FALSE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void
gst_wasapi2_client_init (GstWasapi2Client * self)
{
  self->device_index = DEFAULT_DEVICE_INDEX;
  self->device_class = DEFAULT_DEVICE_CLASS;
  self->can_auto_routing = FALSE;

  g_mutex_init (&self->lock);
  g_cond_init (&self->cond);

  g_mutex_init (&self->init_lock);
  g_cond_init (&self->init_cond);
  self->activate_state = GST_WASAPI2_CLIENT_ACTIVATE_INIT;

  self->context = g_main_context_new ();
  self->loop = g_main_loop_new (self->context, FALSE);
}

static void
gst_wasapi2_client_constructed (GObject * object)
{
  GstWasapi2Client *self = GST_WASAPI2_CLIENT (object);
  /* *INDENT-OFF* */
  ComPtr<GstWasapiDeviceActivator> activator;
  /* *INDENT-ON* */

  /* Create a new thread to ensure that COM thread can be MTA thread.
   * We cannot ensure whether CoInitializeEx() was called outside of here for
   * this thread or not. If it was called with non-COINIT_MULTITHREADED option,
   * we cannot update it */
  g_mutex_lock (&self->lock);
  self->thread = g_thread_new ("GstWasapi2ClientWinRT",
      (GThreadFunc) gst_wasapi2_client_thread_func, self);
  while (!self->loop || !g_main_loop_is_running (self->loop))
    g_cond_wait (&self->cond, &self->lock);
  g_mutex_unlock (&self->lock);

  G_OBJECT_CLASS (parent_class)->constructed (object);
}

static void
gst_wasapi2_client_finalize (GObject * object)
{
  GstWasapi2Client *self = GST_WASAPI2_CLIENT (object);

  if (self->loop) {
    g_main_loop_quit (self->loop);
    g_thread_join (self->thread);
    g_main_context_unref (self->context);
    g_main_loop_unref (self->loop);

    self->thread = nullptr;
    self->context = nullptr;
    self->loop = nullptr;
  }

  gst_clear_caps (&self->supported_caps);

  g_free (self->device_id);
  g_free (self->device_name);

  g_mutex_clear (&self->lock);
  g_cond_clear (&self->cond);

  g_mutex_clear (&self->init_lock);
  g_cond_clear (&self->init_cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_wasapi2_client_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstWasapi2Client *self = GST_WASAPI2_CLIENT (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_string (value, self->device_id);
      break;
    case PROP_DEVICE_NAME:
      g_value_set_string (value, self->device_name);
      break;
    case PROP_DEVICE_INDEX:
      g_value_set_int (value, self->device_index);
      break;
    case PROP_DEVICE_CLASS:
      g_value_set_enum (value, self->device_class);
      break;
    case PROP_DISPATCHER:
      g_value_set_pointer (value, self->dispatcher);
      break;
    case PROP_CAN_AUTO_ROUTING:
      g_value_set_boolean (value, self->can_auto_routing);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_wasapi2_client_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWasapi2Client *self = GST_WASAPI2_CLIENT (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_free (self->device_id);
      self->device_id = g_value_dup_string (value);
      break;
    case PROP_DEVICE_NAME:
      g_free (self->device_name);
      self->device_name = g_value_dup_string (value);
      break;
    case PROP_DEVICE_INDEX:
      self->device_index = g_value_get_int (value);
      break;
    case PROP_DEVICE_CLASS:
      self->device_class =
          (GstWasapi2ClientDeviceClass) g_value_get_enum (value);
      break;
    case PROP_DISPATCHER:
      self->dispatcher = g_value_get_pointer (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_wasapi2_client_main_loop_running_cb (GstWasapi2Client * self)
{
  GST_DEBUG_OBJECT (self, "Main loop running now");

  g_mutex_lock (&self->lock);
  g_cond_signal (&self->cond);
  g_mutex_unlock (&self->lock);

  return G_SOURCE_REMOVE;
}

static void
gst_wasapi2_client_on_device_activated (GstWasapi2Client * self,
    IAudioClient * audio_client)
{
  GST_INFO_OBJECT (self, "Device activated");

  g_mutex_lock (&self->init_lock);
  if (audio_client) {
    audio_client->AddRef ();
    self->audio_client = audio_client;
    self->activate_state = GST_WASAPI2_CLIENT_ACTIVATE_DONE;
  } else {
    GST_WARNING_OBJECT (self, "IAudioClient is unavailable");
    self->activate_state = GST_WASAPI2_CLIENT_ACTIVATE_FAILED;
  }
  g_cond_broadcast (&self->init_cond);
  g_mutex_unlock (&self->init_lock);
}

/* *INDENT-OFF* */
static std::string
convert_wstring_to_string (const std::wstring &wstr)
{
  std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;

  return converter.to_bytes (wstr.c_str());
}

static std::string
convert_hstring_to_string (HString * hstr)
{
  const wchar_t *raw_hstr;

  if (!hstr)
    return std::string();

  raw_hstr = hstr->GetRawBuffer (nullptr);
  if (!raw_hstr)
    return std::string();

  return convert_wstring_to_string (std::wstring (raw_hstr));
}

static std::wstring
gst_wasapi2_client_get_default_device_id (GstWasapi2Client * self)
{
  HRESULT hr;
  PWSTR default_device_id_wstr = nullptr;

  if (self->device_class == GST_WASAPI2_CLIENT_DEVICE_CLASS_CAPTURE)
    hr = StringFromIID (DEVINTERFACE_AUDIO_CAPTURE, &default_device_id_wstr);
  else
    hr = StringFromIID (DEVINTERFACE_AUDIO_RENDER, &default_device_id_wstr);

  if (!gst_wasapi2_result (hr))
    return std::wstring();

  std::wstring ret = std::wstring (default_device_id_wstr);
  CoTaskMemFree (default_device_id_wstr);

  return ret;
}
/* *INDENT-ON* */

static gboolean
gst_wasapi2_client_activate_async (GstWasapi2Client * self,
    GstWasapiDeviceActivator * activator)
{
  /* *INDENT-OFF* */
  ComPtr<IDeviceInformationStatics> device_info_static;
  ComPtr<IAsyncOperation<DeviceInformationCollection*>> async_op;
  ComPtr<IVectorView<DeviceInformation*>> device_list;
  HStringReference hstr_device_info =
      HStringReference(RuntimeClass_Windows_Devices_Enumeration_DeviceInformation);
  /* *INDENT-ON* */
  HRESULT hr;
  DeviceClass device_class;
  unsigned int count = 0;
  gint device_index = 0;
  std::wstring default_device_id_wstring;
  std::string default_device_id;
  std::wstring target_device_id_wstring;
  std::string target_device_id;
  std::string target_device_name;
  gboolean use_default_device = FALSE;

  GST_INFO_OBJECT (self,
      "requested device info, device-class: %s, device: %s, device-index: %d",
      self->device_class ==
      GST_WASAPI2_CLIENT_DEVICE_CLASS_CAPTURE ? "capture" : "render",
      GST_STR_NULL (self->device_id), self->device_index);

  if (self->device_class == GST_WASAPI2_CLIENT_DEVICE_CLASS_CAPTURE) {
    device_class = DeviceClass::DeviceClass_AudioCapture;
  } else {
    device_class = DeviceClass::DeviceClass_AudioRender;
  }

  default_device_id_wstring = gst_wasapi2_client_get_default_device_id (self);
  if (default_device_id_wstring.empty ()) {
    GST_WARNING_OBJECT (self, "Couldn't get default device id");
    goto failed;
  }

  default_device_id = convert_wstring_to_string (default_device_id_wstring);
  GST_DEBUG_OBJECT (self, "Default device id: %s", default_device_id.c_str ());

  /* When
   * 1) default device was requested or
   * 2) no explicitly requested device or
   * 3) requested device string id is null but device index is zero
   * will use default device
   *
   * Note that default device is much preferred
   * See https://docs.microsoft.com/en-us/windows/win32/coreaudio/automatic-stream-routing
   */
  if (self->device_id &&
      g_ascii_strcasecmp (self->device_id, default_device_id.c_str ()) == 0) {
    GST_DEBUG_OBJECT (self, "Default device was requested");
    use_default_device = TRUE;
  } else if (self->device_index < 0 && !self->device_id) {
    GST_DEBUG_OBJECT (self,
        "No device was explicitly requested, use default device");
    use_default_device = TRUE;
  } else if (!self->device_id && self->device_index == 0) {
    GST_DEBUG_OBJECT (self, "device-index == zero means default device");
    use_default_device = TRUE;
  }

  if (use_default_device) {
    target_device_id_wstring = default_device_id_wstring;
    target_device_id = default_device_id;
    if (self->device_class == GST_WASAPI2_CLIENT_DEVICE_CLASS_CAPTURE)
      target_device_name = "Default Audio Capture Device";
    else
      target_device_name = "Default Audio Render Device";
    goto activate;
  }

  hr = GetActivationFactory (hstr_device_info.Get (), &device_info_static);
  if (!gst_wasapi2_result (hr))
    goto failed;

  hr = device_info_static->FindAllAsyncDeviceClass (device_class, &async_op);
  device_info_static.Reset ();
  if (!gst_wasapi2_result (hr))
    goto failed;

  /* *INDENT-OFF* */
  hr = SyncWait<DeviceInformationCollection*>(async_op.Get ());
  /* *INDENT-ON* */
  if (!gst_wasapi2_result (hr))
    goto failed;

  hr = async_op->GetResults (&device_list);
  async_op.Reset ();
  if (!gst_wasapi2_result (hr))
    goto failed;

  hr = device_list->get_Size (&count);
  if (!gst_wasapi2_result (hr))
    goto failed;

  if (count == 0) {
    GST_WARNING_OBJECT (self, "No available device");
    goto failed;
  }

  /* device_index 0 will be assigned for default device
   * so the number of available device is count + 1 (for default device) */
  if (self->device_index >= 0 && self->device_index > (gint) count) {
    GST_WARNING_OBJECT (self, "Device index %d is unavailable",
        self->device_index);
    goto failed;
  }

  GST_DEBUG_OBJECT (self, "Available device count: %d", count);

  /* zero is for default device */
  device_index = 1;
  for (unsigned int i = 0; i < count; i++) {
    /* *INDENT-OFF* */
    ComPtr<IDeviceInformation> device_info;
    /* *INDENT-ON* */
    HString id;
    HString name;
    boolean b_value;
    std::string cur_device_id;
    std::string cur_device_name;

    hr = device_list->GetAt (i, &device_info);
    if (!gst_wasapi2_result (hr))
      continue;

    hr = device_info->get_IsEnabled (&b_value);
    if (!gst_wasapi2_result (hr))
      continue;

    /* select only enabled device */
    if (!b_value) {
      GST_DEBUG_OBJECT (self, "Device index %d is disabled", i);
      continue;
    }

    /* To ensure device id and device name are available,
     * will query this later again once target device is determined */
    hr = device_info->get_Id (id.GetAddressOf ());
    if (!gst_wasapi2_result (hr))
      continue;

    if (!id.IsValid ()) {
      GST_WARNING_OBJECT (self, "Device index %d has invalid id", i);
      continue;
    }

    hr = device_info->get_Name (name.GetAddressOf ());
    if (!gst_wasapi2_result (hr))
      continue;

    if (!name.IsValid ()) {
      GST_WARNING_OBJECT (self, "Device index %d has invalid name", i);
      continue;
    }

    cur_device_id = convert_hstring_to_string (&id);
    if (cur_device_id.empty ()) {
      GST_WARNING_OBJECT (self, "Device index %d has empty id", i);
      continue;
    }

    cur_device_name = convert_hstring_to_string (&name);
    if (cur_device_name.empty ()) {
      GST_WARNING_OBJECT (self, "Device index %d has empty device name", i);
      continue;
    }

    GST_DEBUG_OBJECT (self, "device [%d] id: %s, name: %s",
        device_index, cur_device_id.c_str (), cur_device_name.c_str ());

    if (self->device_id &&
        g_ascii_strcasecmp (self->device_id, cur_device_id.c_str ()) == 0) {
      GST_INFO_OBJECT (self,
          "Device index %d has matching device id %s", device_index,
          cur_device_id.c_str ());
      target_device_id_wstring = id.GetRawBuffer (nullptr);
      target_device_id = cur_device_id;
      target_device_name = cur_device_name;
      break;
    }

    if (self->device_index >= 0 && self->device_index == device_index) {
      GST_INFO_OBJECT (self, "Select device index %d, device id %s",
          device_index, cur_device_id.c_str ());
      target_device_id_wstring = id.GetRawBuffer (nullptr);
      target_device_id = cur_device_id;
      target_device_name = cur_device_name;
      break;
    }

    /* count only available devices */
    device_index++;
  }

  if (target_device_id_wstring.empty ()) {
    GST_WARNING_OBJECT (self, "Couldn't find target device");
    goto failed;
  }

activate:
  /* fill device id and name */
  g_free (self->device_id);
  self->device_id = g_strdup (target_device_id.c_str ());

  g_free (self->device_name);
  self->device_name = g_strdup (target_device_name.c_str ());

  self->device_index = device_index;
  /* default device supports automatic stream routing */
  self->can_auto_routing = use_default_device;

  hr = activator->ActivateDeviceAsync (target_device_id_wstring);
  if (!gst_wasapi2_result (hr)) {
    GST_WARNING_OBJECT (self, "Failed to activate device");
    goto failed;
  }

  g_mutex_lock (&self->lock);
  if (self->activate_state == GST_WASAPI2_CLIENT_ACTIVATE_INIT)
    self->activate_state = GST_WASAPI2_CLIENT_ACTIVATE_WAIT;
  g_mutex_unlock (&self->lock);

  return TRUE;

failed:
  self->activate_state = GST_WASAPI2_CLIENT_ACTIVATE_FAILED;

  return FALSE;
}

static const gchar *
activate_state_to_string (GstWasapi2ClientActivateState state)
{
  switch (state) {
    case GST_WASAPI2_CLIENT_ACTIVATE_FAILED:
      return "FAILED";
    case GST_WASAPI2_CLIENT_ACTIVATE_INIT:
      return "INIT";
    case GST_WASAPI2_CLIENT_ACTIVATE_WAIT:
      return "WAIT";
    case GST_WASAPI2_CLIENT_ACTIVATE_DONE:
      return "DONE";
  }

  g_assert_not_reached ();

  return "Undefined";
}

static gpointer
gst_wasapi2_client_thread_func (GstWasapi2Client * self)
{
  RoInitializeWrapper initialize (RO_INIT_MULTITHREADED);
  GSource *source;
  HRESULT hr;
  /* *INDENT-OFF* */
  ComPtr<GstWasapiDeviceActivator> activator;

  hr = MakeAndInitialize<GstWasapiDeviceActivator> (&activator,
      self, self->dispatcher);
  /* *INDENT-ON* */

  if (!gst_wasapi2_result (hr)) {
    GST_ERROR_OBJECT (self, "Could not create activator object");
    self->activate_state = GST_WASAPI2_CLIENT_ACTIVATE_FAILED;
    goto run_loop;
  }

  gst_wasapi2_client_activate_async (self, activator.Get ());

  if (!self->dispatcher) {
    /* In case that dispatcher is unavailable, wait activation synchroniously */
    GST_DEBUG_OBJECT (self, "Wait device activation");
    gst_wasapi2_client_ensure_activation (self);
    GST_DEBUG_OBJECT (self, "Device activation result %s",
        activate_state_to_string (self->activate_state));
  }

run_loop:
  g_main_context_push_thread_default (self->context);

  source = g_idle_source_new ();
  g_source_set_callback (source,
      (GSourceFunc) gst_wasapi2_client_main_loop_running_cb, self, nullptr);
  g_source_attach (source, self->context);
  g_source_unref (source);

  GST_DEBUG_OBJECT (self, "Starting main loop");
  g_main_loop_run (self->loop);
  GST_DEBUG_OBJECT (self, "Stopped main loop");

  g_main_context_pop_thread_default (self->context);

  GST_WASAPI2_CLEAR_COM (self->audio_client);

  /* Reset explicitly to ensure that it happens before
   * RoInitializeWrapper dtor is called */
  activator.Reset ();

  GST_DEBUG_OBJECT (self, "Exit thread function");

  return nullptr;
}

GstCaps *
gst_wasapi2_client_get_caps (GstWasapi2Client * client)
{
  WAVEFORMATEX *mix_format = nullptr;
  static GstStaticCaps static_caps = GST_STATIC_CAPS (GST_WASAPI2_STATIC_CAPS);
  GstCaps *scaps;
  HRESULT hr;

  g_return_val_if_fail (GST_IS_WASAPI2_CLIENT (client), nullptr);

  if (client->supported_caps)
    return gst_caps_ref (client->supported_caps);

  if (!client->audio_client) {
    GST_WARNING_OBJECT (client, "IAudioClient3 wasn't configured");
    return nullptr;
  }

  hr = client->audio_client->GetMixFormat (&mix_format);
  if (!gst_wasapi2_result (hr)) {
    GST_WARNING_OBJECT (client, "Failed to get mix format");
    return nullptr;
  }

  scaps = gst_static_caps_get (&static_caps);
  gst_wasapi2_util_parse_waveformatex (mix_format,
      scaps, &client->supported_caps, nullptr);
  gst_caps_unref (scaps);

  CoTaskMemFree (mix_format);

  if (!client->supported_caps) {
    GST_ERROR_OBJECT (client, "No caps from subclass");
    return nullptr;
  }

  return gst_caps_ref (client->supported_caps);
}

gboolean
gst_wasapi2_client_ensure_activation (GstWasapi2Client * client)
{
  g_return_val_if_fail (GST_IS_WASAPI2_CLIENT (client), FALSE);

  /* should not happen */
  g_assert (client->activate_state != GST_WASAPI2_CLIENT_ACTIVATE_INIT);

  g_mutex_lock (&client->init_lock);
  while (client->activate_state == GST_WASAPI2_CLIENT_ACTIVATE_WAIT)
    g_cond_wait (&client->init_cond, &client->init_lock);
  g_mutex_unlock (&client->init_lock);

  return client->activate_state == GST_WASAPI2_CLIENT_ACTIVATE_DONE;
}

static HRESULT
find_dispatcher (ICoreDispatcher ** dispatcher)
{
  /* *INDENT-OFF* */
  HStringReference hstr_core_app =
      HStringReference(RuntimeClass_Windows_ApplicationModel_Core_CoreApplication);
  ComPtr<ICoreApplication> core_app;
  ComPtr<ICoreApplicationView> core_app_view;
  ComPtr<ICoreWindow> core_window;
  /* *INDENT-ON* */
  HRESULT hr;

  hr = GetActivationFactory (hstr_core_app.Get (), &core_app);
  if (FAILED (hr))
    return hr;

  hr = core_app->GetCurrentView (&core_app_view);
  if (FAILED (hr))
    return hr;

  hr = core_app_view->get_CoreWindow (&core_window);
  if (FAILED (hr))
    return hr;

  return core_window->get_Dispatcher (dispatcher);
}

GstWasapi2Client *
gst_wasapi2_client_new (GstWasapi2ClientDeviceClass device_class,
    gint device_index, const gchar * device_id, gpointer dispatcher)
{
  GstWasapi2Client *self;
  /* *INDENT-OFF* */
  ComPtr<ICoreDispatcher> core_dispatcher;
  /* *INDENT-ON* */
  /* Multiple COM init is allowed */
  RoInitializeWrapper init_wrapper (RO_INIT_MULTITHREADED);

  /* If application didn't pass ICoreDispatcher object,
   * try to get dispatcher object for the current thread */
  if (!dispatcher) {
    HRESULT hr;

    hr = find_dispatcher (&core_dispatcher);
    if (SUCCEEDED (hr)) {
      GST_DEBUG ("UI dispatcher is available");
      dispatcher = core_dispatcher.Get ();
    } else {
      GST_DEBUG ("UI dispatcher is unavailable");
    }
  } else {
    GST_DEBUG ("Use user passed UI dispatcher");
  }

  self = (GstWasapi2Client *) g_object_new (GST_TYPE_WASAPI2_CLIENT,
      "device-class", device_class, "device-index", device_index,
      "device", device_id, "dispatcher", dispatcher, nullptr);

  /* Reset explicitly to ensure that it happens before
   * RoInitializeWrapper dtor is called */
  core_dispatcher.Reset ();

  if (self->activate_state == GST_WASAPI2_CLIENT_ACTIVATE_FAILED) {
    gst_object_unref (self);
    return nullptr;
  }

  gst_object_ref_sink (self);

  return self;
}

IAudioClient *
gst_wasapi2_client_get_handle (GstWasapi2Client * client)
{
  g_return_val_if_fail (GST_IS_WASAPI2_CLIENT (client), nullptr);

  return client->audio_client;
}
