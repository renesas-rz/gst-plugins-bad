/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

#include "gstcudaloader.h"
#include <gmodule.h>

GST_DEBUG_CATEGORY_EXTERN (gst_nvcodec_debug);
#define GST_CAT_DEFAULT gst_nvcodec_debug

#ifndef G_OS_WIN32
#define CUDA_LIBNAME "libcuda.so.1"
#else
#define CUDA_LIBNAME "nvcuda.dll"
#endif

#define LOAD_SYMBOL(name,func) G_STMT_START { \
  if (!g_module_symbol (module, G_STRINGIFY (name), (gpointer *) &vtable->func)) { \
    GST_ERROR ("Failed to load '%s' from %s, %s", G_STRINGIFY (name), filename, g_module_error()); \
    goto error; \
  } \
} G_STMT_END;

/* *INDENT-OFF* */
typedef struct _GstNvCodecCudaVTable
{
  gboolean loaded;

  CUresult (CUDAAPI * CuInit) (unsigned int Flags);
  CUresult (CUDAAPI * CuGetErrorName) (CUresult error, const char **pStr);
  CUresult (CUDAAPI * CuGetErrorString) (CUresult error, const char **pStr);

  CUresult (CUDAAPI * CuCtxCreate) (CUcontext * pctx, unsigned int flags,
      CUdevice dev);
  CUresult (CUDAAPI * CuCtxDestroy) (CUcontext ctx);
  CUresult (CUDAAPI * CuCtxPopCurrent) (CUcontext * pctx);
  CUresult (CUDAAPI * CuCtxPushCurrent) (CUcontext ctx);

  CUresult (CUDAAPI * CuCtxEnablePeerAccess) (CUcontext peerContext,
      unsigned int Flags);
  CUresult (CUDAAPI * CuCtxDisablePeerAccess) (CUcontext peerContext);
  CUresult (CUDAAPI * CuGraphicsMapResources) (unsigned int count,
      CUgraphicsResource * resources, CUstream hStream);
  CUresult (CUDAAPI * CuGraphicsUnmapResources) (unsigned int count,
      CUgraphicsResource * resources, CUstream hStream);
  CUresult (CUDAAPI *
      CuGraphicsResourceSetMapFlags) (CUgraphicsResource resource,
      unsigned int flags);
  CUresult (CUDAAPI * CuGraphicsSubResourceGetMappedArray) (CUarray * pArray,
      CUgraphicsResource resource, unsigned int arrayIndex,
      unsigned int mipLevel);
  CUresult (CUDAAPI * CuGraphicsResourceGetMappedPointer) (CUdeviceptr *
      pDevPtr, size_t * pSize, CUgraphicsResource resource);
  CUresult (CUDAAPI *
      CuGraphicsUnregisterResource) (CUgraphicsResource resource);

  CUresult (CUDAAPI * CuMemAlloc) (CUdeviceptr * dptr, unsigned int bytesize);
  CUresult (CUDAAPI * CuMemAllocPitch) (CUdeviceptr * dptr, size_t * pPitch,
      size_t WidthInBytes, size_t Height, unsigned int ElementSizeBytes);
  CUresult (CUDAAPI * CuMemAllocHost) (void **pp, unsigned int bytesize);
  CUresult (CUDAAPI * CuMemcpy2D) (const CUDA_MEMCPY2D * pCopy);
  CUresult (CUDAAPI * CuMemcpy2DAsync) (const CUDA_MEMCPY2D * pCopy,
      CUstream hStream);

  CUresult (CUDAAPI * CuMemFree) (CUdeviceptr dptr);
  CUresult (CUDAAPI * CuMemFreeHost) (void *p);

  CUresult (CUDAAPI * CuStreamCreate) (CUstream * phStream,
      unsigned int Flags);
  CUresult (CUDAAPI * CuStreamDestroy) (CUstream hStream);
  CUresult (CUDAAPI * CuStreamSynchronize) (CUstream hStream);

  CUresult (CUDAAPI * CuDeviceGet) (CUdevice * device, int ordinal);
  CUresult (CUDAAPI * CuDeviceGetCount) (int *count);
  CUresult (CUDAAPI * CuDeviceGetName) (char *name, int len, CUdevice dev);
  CUresult (CUDAAPI * CuDeviceGetAttribute) (int *pi,
      CUdevice_attribute attrib, CUdevice dev);
  CUresult (CUDAAPI * CuDeviceCanAccessPeer) (int *canAccessPeer,
      CUdevice dev, CUdevice peerDev);
  CUresult (CUDAAPI * CuDriverGetVersion) (int *driverVersion);

  CUresult (CUDAAPI * CuModuleLoadData) (CUmodule * module,
      const void *image);
  CUresult (CUDAAPI * CuModuleUnload) (CUmodule module);
  CUresult (CUDAAPI * CuModuleGetFunction) (CUfunction * hfunc,
      CUmodule hmod, const char *name);
  CUresult (CUDAAPI * CuTexObjectCreate) (CUtexObject * pTexObject,
      const CUDA_RESOURCE_DESC * pResDesc, const CUDA_TEXTURE_DESC * pTexDesc,
      const CUDA_RESOURCE_VIEW_DESC * pResViewDesc);
  CUresult (CUDAAPI * CuTexObjectDestroy) (CUtexObject texObject);
  CUresult (CUDAAPI * CuLaunchKernel) (CUfunction f, unsigned int gridDimX,
      unsigned int gridDimY, unsigned int gridDimZ,
      unsigned int blockDimX, unsigned int blockDimY, unsigned int blockDimZ,
      unsigned int sharedMemBytes, CUstream hStream, void **kernelParams,
      void **extra);

  CUresult (CUDAAPI * CuGraphicsGLRegisterImage) (CUgraphicsResource *
      pCudaResource, unsigned int image, unsigned int target,
      unsigned int Flags);
  CUresult (CUDAAPI * CuGraphicsGLRegisterBuffer) (CUgraphicsResource *
      pCudaResource, unsigned int buffer, unsigned int Flags);
  CUresult (CUDAAPI * CuGLGetDevices) (unsigned int *pCudaDeviceCount,
      CUdevice * pCudaDevices, unsigned int cudaDeviceCount,
      CUGLDeviceList deviceList);

  CUresult (CUDAAPI * CuGraphicsD3D11RegisterResource) (CUgraphicsResource *
      pCudaResource, gpointer pD3DResource, unsigned int Flags);
  CUresult (CUDAAPI * CuD3D11GetDevice) (CUdevice * device,
      gpointer pAdapter);
  CUresult (CUDAAPI * CuD3D11GetDevices) (unsigned int *pCudaDeviceCount,
      CUdevice * pCudaDevices, unsigned int cudaDeviceCount,
      gpointer pD3D11Device, CUD3D11DeviceList deviceList);
} GstNvCodecCudaVTable;
/* *INDENT-ON* */

static GstNvCodecCudaVTable gst_cuda_vtable = { 0, };

gboolean
gst_cuda_load_library (void)
{
  GModule *module;
  const gchar *filename = CUDA_LIBNAME;
  GstNvCodecCudaVTable *vtable;

  if (gst_cuda_vtable.loaded)
    return TRUE;

  module = g_module_open (filename, G_MODULE_BIND_LAZY);
  if (module == NULL) {
    GST_WARNING ("Could not open library %s, %s", filename, g_module_error ());
    return FALSE;
  }

  vtable = &gst_cuda_vtable;

  /* cuda.h */
  LOAD_SYMBOL (cuInit, CuInit);
  LOAD_SYMBOL (cuGetErrorName, CuGetErrorName);
  LOAD_SYMBOL (cuGetErrorString, CuGetErrorString);
  LOAD_SYMBOL (cuCtxCreate, CuCtxCreate);
  LOAD_SYMBOL (cuCtxDestroy, CuCtxDestroy);
  LOAD_SYMBOL (cuCtxPopCurrent, CuCtxPopCurrent);
  LOAD_SYMBOL (cuCtxPushCurrent, CuCtxPushCurrent);
  LOAD_SYMBOL (cuCtxEnablePeerAccess, CuCtxEnablePeerAccess);
  LOAD_SYMBOL (cuCtxDisablePeerAccess, CuCtxDisablePeerAccess);

  LOAD_SYMBOL (cuGraphicsMapResources, CuGraphicsMapResources);
  LOAD_SYMBOL (cuGraphicsUnmapResources, CuGraphicsUnmapResources);
  LOAD_SYMBOL (cuGraphicsResourceSetMapFlags, CuGraphicsResourceSetMapFlags);
  LOAD_SYMBOL (cuGraphicsSubResourceGetMappedArray,
      CuGraphicsSubResourceGetMappedArray);
  LOAD_SYMBOL (cuGraphicsResourceGetMappedPointer,
      CuGraphicsResourceGetMappedPointer);
  LOAD_SYMBOL (cuGraphicsUnregisterResource, CuGraphicsUnregisterResource);

  LOAD_SYMBOL (cuMemAlloc, CuMemAlloc);
  LOAD_SYMBOL (cuMemAllocPitch, CuMemAllocPitch);
  LOAD_SYMBOL (cuMemAllocHost, CuMemAllocHost);
  LOAD_SYMBOL (cuMemcpy2D, CuMemcpy2D);
  LOAD_SYMBOL (cuMemcpy2DAsync, CuMemcpy2DAsync);

  LOAD_SYMBOL (cuMemFree, CuMemFree);
  LOAD_SYMBOL (cuMemFreeHost, CuMemFreeHost);

  LOAD_SYMBOL (cuStreamCreate, CuStreamCreate);
  LOAD_SYMBOL (cuStreamDestroy, CuStreamDestroy);
  LOAD_SYMBOL (cuStreamSynchronize, CuStreamSynchronize);

  LOAD_SYMBOL (cuDeviceGet, CuDeviceGet);
  LOAD_SYMBOL (cuDeviceGetCount, CuDeviceGetCount);
  LOAD_SYMBOL (cuDeviceGetName, CuDeviceGetName);
  LOAD_SYMBOL (cuDeviceGetAttribute, CuDeviceGetAttribute);
  LOAD_SYMBOL (cuDeviceCanAccessPeer, CuDeviceCanAccessPeer);

  LOAD_SYMBOL (cuDriverGetVersion, CuDriverGetVersion);

  LOAD_SYMBOL (cuModuleLoadData, CuModuleLoadData);
  LOAD_SYMBOL (cuModuleUnload, CuModuleUnload);
  LOAD_SYMBOL (cuModuleGetFunction, CuModuleGetFunction);
  LOAD_SYMBOL (cuTexObjectCreate, CuTexObjectCreate);
  LOAD_SYMBOL (cuTexObjectDestroy, CuTexObjectDestroy);
  LOAD_SYMBOL (cuLaunchKernel, CuLaunchKernel);

  /* cudaGL.h */
  LOAD_SYMBOL (cuGraphicsGLRegisterImage, CuGraphicsGLRegisterImage);
  LOAD_SYMBOL (cuGraphicsGLRegisterBuffer, CuGraphicsGLRegisterBuffer);
  LOAD_SYMBOL (cuGLGetDevices, CuGLGetDevices);

#ifdef HAVE_NVCODEC_GST_D3D11
  /* cudaD3D11.h */
  LOAD_SYMBOL (cuGraphicsD3D11RegisterResource,
      CuGraphicsD3D11RegisterResource);
  LOAD_SYMBOL (cuD3D11GetDevice, CuD3D11GetDevice);
  LOAD_SYMBOL (cuD3D11GetDevices, CuD3D11GetDevices);
#endif

  vtable->loaded = TRUE;

  return TRUE;

error:
  g_module_close (module);

  return FALSE;
}

CUresult CUDAAPI
CuInit (unsigned int Flags)
{
  g_assert (gst_cuda_vtable.CuInit != NULL);

  return gst_cuda_vtable.CuInit (Flags);
}

CUresult CUDAAPI
CuGetErrorName (CUresult error, const char **pStr)
{
  g_assert (gst_cuda_vtable.CuGetErrorName != NULL);

  return gst_cuda_vtable.CuGetErrorName (error, pStr);
}

CUresult CUDAAPI
CuGetErrorString (CUresult error, const char **pStr)
{
  g_assert (gst_cuda_vtable.CuGetErrorString != NULL);

  return gst_cuda_vtable.CuGetErrorString (error, pStr);
}

CUresult CUDAAPI
CuCtxCreate (CUcontext * pctx, unsigned int flags, CUdevice dev)
{
  g_assert (gst_cuda_vtable.CuCtxCreate != NULL);

  return gst_cuda_vtable.CuCtxCreate (pctx, flags, dev);
}

CUresult CUDAAPI
CuCtxDestroy (CUcontext ctx)
{
  g_assert (gst_cuda_vtable.CuCtxDestroy != NULL);

  return gst_cuda_vtable.CuCtxDestroy (ctx);
}

CUresult CUDAAPI
CuCtxPopCurrent (CUcontext * pctx)
{
  g_assert (gst_cuda_vtable.CuCtxPopCurrent != NULL);

  return gst_cuda_vtable.CuCtxPopCurrent (pctx);
}

CUresult CUDAAPI
CuCtxPushCurrent (CUcontext ctx)
{
  g_assert (gst_cuda_vtable.CuCtxPushCurrent != NULL);

  return gst_cuda_vtable.CuCtxPushCurrent (ctx);
}

CUresult CUDAAPI
CuCtxEnablePeerAccess (CUcontext peerContext, unsigned int Flags)
{
  g_assert (gst_cuda_vtable.CuCtxEnablePeerAccess != NULL);

  return gst_cuda_vtable.CuCtxEnablePeerAccess (peerContext, Flags);
}

CUresult CUDAAPI
CuCtxDisablePeerAccess (CUcontext peerContext)
{
  g_assert (gst_cuda_vtable.CuCtxDisablePeerAccess != NULL);

  return gst_cuda_vtable.CuCtxDisablePeerAccess (peerContext);
}

CUresult CUDAAPI
CuGraphicsMapResources (unsigned int count, CUgraphicsResource * resources,
    CUstream hStream)
{
  g_assert (gst_cuda_vtable.CuGraphicsMapResources != NULL);

  return gst_cuda_vtable.CuGraphicsMapResources (count, resources, hStream);
}

CUresult CUDAAPI
CuGraphicsUnmapResources (unsigned int count, CUgraphicsResource * resources,
    CUstream hStream)
{
  g_assert (gst_cuda_vtable.CuGraphicsUnmapResources != NULL);

  return gst_cuda_vtable.CuGraphicsUnmapResources (count, resources, hStream);
}

CUresult CUDAAPI
CuGraphicsResourceSetMapFlags (CUgraphicsResource resource, unsigned int flags)
{
  g_assert (gst_cuda_vtable.CuGraphicsResourceSetMapFlags != NULL);

  return gst_cuda_vtable.CuGraphicsResourceSetMapFlags (resource, flags);
}

CUresult CUDAAPI
CuGraphicsSubResourceGetMappedArray (CUarray * pArray,
    CUgraphicsResource resource, unsigned int arrayIndex, unsigned int mipLevel)
{
  g_assert (gst_cuda_vtable.CuGraphicsSubResourceGetMappedArray != NULL);

  return gst_cuda_vtable.CuGraphicsSubResourceGetMappedArray (pArray, resource,
      arrayIndex, mipLevel);
}

/* *INDENT-OFF* */
CUresult CUDAAPI
CuGraphicsResourceGetMappedPointer (CUdeviceptr * pDevPtr, size_t * pSize,
    CUgraphicsResource resource)
{
  g_assert (gst_cuda_vtable.CuGraphicsResourceGetMappedPointer != NULL);

  return gst_cuda_vtable.CuGraphicsResourceGetMappedPointer (pDevPtr, pSize,
      resource);
}
/* *INDENT-ON* */

CUresult CUDAAPI
CuGraphicsUnregisterResource (CUgraphicsResource resource)
{
  g_assert (gst_cuda_vtable.CuGraphicsUnregisterResource != NULL);

  return gst_cuda_vtable.CuGraphicsUnregisterResource (resource);
}

CUresult CUDAAPI
CuMemAlloc (CUdeviceptr * dptr, unsigned int bytesize)
{
  g_assert (gst_cuda_vtable.CuMemAlloc != NULL);

  return gst_cuda_vtable.CuMemAlloc (dptr, bytesize);
}

/* *INDENT-OFF* */
CUresult CUDAAPI
CuMemAllocPitch (CUdeviceptr * dptr, size_t * pPitch, size_t WidthInBytes,
    size_t Height, unsigned int ElementSizeBytes)
{
  g_assert (gst_cuda_vtable.CuMemAllocPitch != NULL);

  return gst_cuda_vtable.CuMemAllocPitch (dptr, pPitch, WidthInBytes, Height,
      ElementSizeBytes);
}
/* *INDENT-ON* */

CUresult CUDAAPI
CuMemAllocHost (void **pp, unsigned int bytesize)
{
  g_assert (gst_cuda_vtable.CuMemAllocHost != NULL);

  return gst_cuda_vtable.CuMemAllocHost (pp, bytesize);
}

CUresult CUDAAPI
CuMemcpy2D (const CUDA_MEMCPY2D * pCopy)
{
  g_assert (gst_cuda_vtable.CuMemcpy2D != NULL);

  return gst_cuda_vtable.CuMemcpy2D (pCopy);
}

CUresult CUDAAPI
CuMemcpy2DAsync (const CUDA_MEMCPY2D * pCopy, CUstream hStream)
{
  g_assert (gst_cuda_vtable.CuMemcpy2DAsync != NULL);

  return gst_cuda_vtable.CuMemcpy2DAsync (pCopy, hStream);
}

CUresult CUDAAPI
CuMemFree (CUdeviceptr dptr)
{
  g_assert (gst_cuda_vtable.CuMemFree != NULL);

  return gst_cuda_vtable.CuMemFree (dptr);
}

CUresult CUDAAPI
CuMemFreeHost (void *p)
{
  g_assert (gst_cuda_vtable.CuMemFreeHost != NULL);

  return gst_cuda_vtable.CuMemFreeHost (p);
}

CUresult CUDAAPI
CuStreamCreate (CUstream * phStream, unsigned int Flags)
{
  g_assert (gst_cuda_vtable.CuStreamCreate != NULL);

  return gst_cuda_vtable.CuStreamCreate (phStream, Flags);
}

CUresult CUDAAPI
CuStreamDestroy (CUstream hStream)
{
  g_assert (gst_cuda_vtable.CuStreamDestroy != NULL);

  return gst_cuda_vtable.CuStreamDestroy (hStream);
}

CUresult CUDAAPI
CuStreamSynchronize (CUstream hStream)
{
  g_assert (gst_cuda_vtable.CuStreamSynchronize != NULL);

  return gst_cuda_vtable.CuStreamSynchronize (hStream);
}

CUresult CUDAAPI
CuDeviceGet (CUdevice * device, int ordinal)
{
  g_assert (gst_cuda_vtable.CuDeviceGet != NULL);

  return gst_cuda_vtable.CuDeviceGet (device, ordinal);
}

CUresult CUDAAPI
CuDeviceGetCount (int *count)
{
  g_assert (gst_cuda_vtable.CuDeviceGetCount != NULL);

  return gst_cuda_vtable.CuDeviceGetCount (count);
}

CUresult CUDAAPI
CuDeviceGetName (char *name, int len, CUdevice dev)
{
  g_assert (gst_cuda_vtable.CuDeviceGetName != NULL);

  return gst_cuda_vtable.CuDeviceGetName (name, len, dev);
}

CUresult CUDAAPI
CuDeviceGetAttribute (int *pi, CUdevice_attribute attrib, CUdevice dev)
{
  g_assert (gst_cuda_vtable.CuDeviceGetAttribute != NULL);

  return gst_cuda_vtable.CuDeviceGetAttribute (pi, attrib, dev);
}

CUresult CUDAAPI
CuDeviceCanAccessPeer (int *canAccessPeer, CUdevice dev, CUdevice peerDev)
{
  g_assert (gst_cuda_vtable.CuDeviceCanAccessPeer != NULL);

  return gst_cuda_vtable.CuDeviceCanAccessPeer (canAccessPeer, dev, peerDev);
}

CUresult CUDAAPI
CuDriverGetVersion (int *driverVersion)
{
  g_assert (gst_cuda_vtable.CuDriverGetVersion != NULL);

  return gst_cuda_vtable.CuDriverGetVersion (driverVersion);
}

CUresult CUDAAPI
CuModuleLoadData (CUmodule * module, const void *image)
{
  g_assert (gst_cuda_vtable.CuModuleLoadData != NULL);

  return gst_cuda_vtable.CuModuleLoadData (module, image);
}

CUresult CUDAAPI
CuModuleUnload (CUmodule module)
{
  g_assert (gst_cuda_vtable.CuModuleUnload != NULL);

  return gst_cuda_vtable.CuModuleUnload (module);
}

CUresult CUDAAPI
CuModuleGetFunction (CUfunction * hfunc, CUmodule hmod, const char *name)
{
  g_assert (gst_cuda_vtable.CuModuleGetFunction != NULL);

  return gst_cuda_vtable.CuModuleGetFunction (hfunc, hmod, name);
}

CUresult CUDAAPI
CuTexObjectCreate (CUtexObject * pTexObject,
    const CUDA_RESOURCE_DESC * pResDesc, const CUDA_TEXTURE_DESC * pTexDesc,
    const CUDA_RESOURCE_VIEW_DESC * pResViewDesc)
{
  g_assert (gst_cuda_vtable.CuTexObjectCreate != NULL);

  return gst_cuda_vtable.CuTexObjectCreate (pTexObject, pResDesc, pTexDesc,
      pResViewDesc);
}

CUresult CUDAAPI
CuTexObjectDestroy (CUtexObject texObject)
{
  g_assert (gst_cuda_vtable.CuTexObjectDestroy != NULL);

  return gst_cuda_vtable.CuTexObjectDestroy (texObject);
}

CUresult CUDAAPI
CuLaunchKernel (CUfunction f, unsigned int gridDimX,
    unsigned int gridDimY, unsigned int gridDimZ,
    unsigned int blockDimX, unsigned int blockDimY, unsigned int blockDimZ,
    unsigned int sharedMemBytes, CUstream hStream, void **kernelParams,
    void **extra)
{
  g_assert (gst_cuda_vtable.CuLaunchKernel != NULL);

  return gst_cuda_vtable.CuLaunchKernel (f, gridDimX, gridDimY, gridDimZ,
      blockDimX, blockDimY, blockDimZ, sharedMemBytes, hStream, kernelParams,
      extra);
}

/* cudaGL.h */
CUresult CUDAAPI
CuGraphicsGLRegisterImage (CUgraphicsResource * pCudaResource,
    unsigned int image, unsigned int target, unsigned int Flags)
{
  g_assert (gst_cuda_vtable.CuGraphicsGLRegisterImage != NULL);

  return gst_cuda_vtable.CuGraphicsGLRegisterImage (pCudaResource, image,
      target, Flags);
}

CUresult CUDAAPI
CuGraphicsGLRegisterBuffer (CUgraphicsResource * pCudaResource,
    unsigned int buffer, unsigned int Flags)
{
  g_assert (gst_cuda_vtable.CuGraphicsGLRegisterBuffer != NULL);

  return gst_cuda_vtable.CuGraphicsGLRegisterBuffer (pCudaResource, buffer,
      Flags);
}

CUresult CUDAAPI
CuGLGetDevices (unsigned int *pCudaDeviceCount, CUdevice * pCudaDevices,
    unsigned int cudaDeviceCount, CUGLDeviceList deviceList)
{
  g_assert (gst_cuda_vtable.CuGLGetDevices != NULL);

  return gst_cuda_vtable.CuGLGetDevices (pCudaDeviceCount, pCudaDevices,
      cudaDeviceCount, deviceList);
}

/* cudaD3D11.h */
CUresult CUDAAPI
CuGraphicsD3D11RegisterResource (CUgraphicsResource * pCudaResource,
    gpointer pD3DResource, unsigned int Flags)
{
  g_assert (gst_cuda_vtable.CuGraphicsD3D11RegisterResource != NULL);

  return gst_cuda_vtable.CuGraphicsD3D11RegisterResource (pCudaResource,
      pD3DResource, Flags);
}

CUresult CUDAAPI
CuD3D11GetDevice (CUdevice * device, gpointer pAdapter)
{
  g_assert (gst_cuda_vtable.CuD3D11GetDevice != NULL);

  return gst_cuda_vtable.CuD3D11GetDevice (device, pAdapter);
}

CUresult CUDAAPI
CuD3D11GetDevices (unsigned int *pCudaDeviceCount,
    CUdevice * pCudaDevices,
    unsigned int cudaDeviceCount,
    gpointer pD3D11Device, CUD3D11DeviceList deviceList)
{
  g_assert (gst_cuda_vtable.CuD3D11GetDevices != NULL);

  return gst_cuda_vtable.CuD3D11GetDevices (pCudaDeviceCount, pCudaDevices,
      cudaDeviceCount, pD3D11Device, deviceList);
}
