/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * X11 Graphics Pipeline
 *
 * Copyright 2014 Marc-Andre Moreau <marcandre.moreau@gmail.com>
 * Copyright 2016 Armin Novak <armin.novak@thincast.com>
 * Copyright 2016 Thincast Technologies GmbH
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <freerdp/config.h>

#include <math.h>

#include <winpr/assert.h>
#include <winpr/cast.h>

#include <freerdp/log.h>
#include "xf_gfx.h"
#include "xf_rail.h"
#include "xf_utils.h"

#include <X11/Xutil.h>

#define TAG CLIENT_TAG("x11")

static UINT xf_OutputUpdate(xfContext* xfc, xfGfxSurface* surface)
{
	UINT rc = ERROR_INTERNAL_ERROR;
	UINT32 surfaceX = 0;
	UINT32 surfaceY = 0;
	RECTANGLE_16 surfaceRect = { 0 };
	UINT32 nbRects = 0;
	const RECTANGLE_16* rects = NULL;

	WINPR_ASSERT(xfc);
	WINPR_ASSERT(surface);

	rdpGdi* gdi = xfc->common.context.gdi;
	WINPR_ASSERT(gdi);

	rdpSettings* settings = xfc->common.context.settings;
	WINPR_ASSERT(settings);

	surfaceX = surface->gdi.outputOriginX;
	surfaceY = surface->gdi.outputOriginY;
	surfaceRect.left = 0;
	surfaceRect.top = 0;
	surfaceRect.right = WINPR_ASSERTING_INT_CAST(UINT16, surface->gdi.mappedWidth);
	surfaceRect.bottom = WINPR_ASSERTING_INT_CAST(UINT16, surface->gdi.mappedHeight);
	LogDynAndXSetClipMask(xfc->log, xfc->display, xfc->gc, None);
	LogDynAndXSetFunction(xfc->log, xfc->display, xfc->gc, GXcopy);
	LogDynAndXSetFillStyle(xfc->log, xfc->display, xfc->gc, FillSolid);
	region16_intersect_rect(&(surface->gdi.invalidRegion), &(surface->gdi.invalidRegion),
	                        &surfaceRect);

	WINPR_ASSERT(surface->gdi.mappedWidth);
	WINPR_ASSERT(surface->gdi.mappedHeight);
	const double sx = 1.0 * surface->gdi.outputTargetWidth / (double)surface->gdi.mappedWidth;
	const double sy = 1.0 * surface->gdi.outputTargetHeight / (double)surface->gdi.mappedHeight;

	if (!(rects = region16_rects(&surface->gdi.invalidRegion, &nbRects)))
		return CHANNEL_RC_OK;

	for (UINT32 x = 0; x < nbRects; x++)
	{
		const RECTANGLE_16* rect = &rects[x];
		const UINT32 nXSrc = rect->left;
		const UINT32 nYSrc = rect->top;
		const UINT32 swidth = rect->right - nXSrc;
		const UINT32 sheight = rect->bottom - nYSrc;
		const UINT32 nXDst = (UINT32)lround(1.0 * surfaceX + nXSrc * sx);
		const UINT32 nYDst = (UINT32)lround(1.0 * surfaceY + nYSrc * sy);
		const UINT32 dwidth = (UINT32)lround(1.0 * swidth * sx);
		const UINT32 dheight = (UINT32)lround(1.0 * sheight * sy);

		if (surface->stage)
		{
			if (!freerdp_image_scale(surface->stage, gdi->dstFormat, surface->stageScanline, nXSrc,
			                         nYSrc, dwidth, dheight, surface->gdi.data, surface->gdi.format,
			                         surface->gdi.scanline, nXSrc, nYSrc, swidth, sheight))
				goto fail;
		}

		if (xfc->remote_app)
		{
			LogDynAndXPutImage(xfc->log, xfc->display, xfc->primary, xfc->gc, surface->image,
			                   WINPR_ASSERTING_INT_CAST(int, nXSrc),
			                   WINPR_ASSERTING_INT_CAST(int, nYSrc),
			                   WINPR_ASSERTING_INT_CAST(int, nXDst),
			                   WINPR_ASSERTING_INT_CAST(int, nYDst), dwidth, dheight);
			xf_lock_x11(xfc);
			xf_rail_paint_surface(xfc, surface->gdi.windowId, rect);
			xf_unlock_x11(xfc);
		}
		else
#ifdef WITH_XRENDER
		    if (freerdp_settings_get_bool(settings, FreeRDP_SmartSizing) ||
		        freerdp_settings_get_bool(settings, FreeRDP_MultiTouchGestures))
		{
			LogDynAndXPutImage(xfc->log, xfc->display, xfc->primary, xfc->gc, surface->image,
			                   WINPR_ASSERTING_INT_CAST(int, nXSrc),
			                   WINPR_ASSERTING_INT_CAST(int, nYSrc),
			                   WINPR_ASSERTING_INT_CAST(int, nXDst),
			                   WINPR_ASSERTING_INT_CAST(int, nYDst), dwidth, dheight);
			xf_draw_screen(xfc, WINPR_ASSERTING_INT_CAST(int32_t, nXDst),
			               WINPR_ASSERTING_INT_CAST(int32_t, nYDst),
			               WINPR_ASSERTING_INT_CAST(int32_t, dwidth),
			               WINPR_ASSERTING_INT_CAST(int32_t, dheight));
		}
		else
#endif
		{
			LogDynAndXPutImage(xfc->log, xfc->display, xfc->drawable, xfc->gc, surface->image,
			                   WINPR_ASSERTING_INT_CAST(int, nXSrc),
			                   WINPR_ASSERTING_INT_CAST(int, nYSrc),
			                   WINPR_ASSERTING_INT_CAST(int, nXDst),
			                   WINPR_ASSERTING_INT_CAST(int, nYDst), dwidth, dheight);
		}
	}

	rc = CHANNEL_RC_OK;
fail:
	region16_clear(&surface->gdi.invalidRegion);
	LogDynAndXSetClipMask(xfc->log, xfc->display, xfc->gc, None);
	LogDynAndXSync(xfc->log, xfc->display, False);
	return rc;
}

static UINT xf_WindowUpdate(RdpgfxClientContext* context, xfGfxSurface* surface)
{
	WINPR_ASSERT(context);
	WINPR_ASSERT(surface);
	return IFCALLRESULT(CHANNEL_RC_OK, context->UpdateWindowFromSurface, context, &surface->gdi);
}

static UINT xf_UpdateSurfaces(RdpgfxClientContext* context)
{
	UINT16 count = 0;
	UINT status = CHANNEL_RC_OK;
	UINT16* pSurfaceIds = NULL;
	rdpGdi* gdi = (rdpGdi*)context->custom;
	xfContext* xfc = NULL;

	if (!gdi)
		return status;

	if (gdi->suppressOutput)
		return CHANNEL_RC_OK;

	xfc = (xfContext*)gdi->context;
	EnterCriticalSection(&context->mux);
	context->GetSurfaceIds(context, &pSurfaceIds, &count);

	for (UINT32 index = 0; index < count; index++)
	{
		xfGfxSurface* surface = (xfGfxSurface*)context->GetSurfaceData(context, pSurfaceIds[index]);

		if (!surface)
			continue;

		/* If UpdateSurfaceArea callback is available, the output has already been updated. */
		if (context->UpdateSurfaceArea)
		{
			if (surface->gdi.handleInUpdateSurfaceArea)
				continue;
		}

		if (surface->gdi.outputMapped)
			status = xf_OutputUpdate(xfc, surface);
		else if (surface->gdi.windowMapped)
			status = xf_WindowUpdate(context, surface);

		if (status != CHANNEL_RC_OK)
			break;
	}

	free(pSurfaceIds);
	LeaveCriticalSection(&context->mux);
	return status;
}

UINT xf_OutputExpose(xfContext* xfc, UINT32 x, UINT32 y, UINT32 width, UINT32 height)
{
	UINT16 count = 0;
	UINT status = ERROR_INTERNAL_ERROR;
	RECTANGLE_16 invalidRect = { 0 };
	RECTANGLE_16 intersection = { 0 };
	UINT16* pSurfaceIds = NULL;
	RdpgfxClientContext* context = NULL;

	WINPR_ASSERT(xfc);
	WINPR_ASSERT(xfc->common.context.gdi);

	context = xfc->common.context.gdi->gfx;
	WINPR_ASSERT(context);

	invalidRect.left = WINPR_ASSERTING_INT_CAST(UINT16, x);
	invalidRect.top = WINPR_ASSERTING_INT_CAST(UINT16, y);
	invalidRect.right = WINPR_ASSERTING_INT_CAST(UINT16, x + width);
	invalidRect.bottom = WINPR_ASSERTING_INT_CAST(UINT16, y + height);
	status = context->GetSurfaceIds(context, &pSurfaceIds, &count);

	if (status != CHANNEL_RC_OK)
		goto fail;

	if (!TryEnterCriticalSection(&context->mux))
	{
		free(pSurfaceIds);
		return CHANNEL_RC_OK;
	}
	for (UINT32 index = 0; index < count; index++)
	{
		RECTANGLE_16 surfaceRect = { 0 };
		xfGfxSurface* surface = (xfGfxSurface*)context->GetSurfaceData(context, pSurfaceIds[index]);

		if (!surface || (!surface->gdi.outputMapped && !surface->gdi.windowMapped))
			continue;

		surfaceRect.left = WINPR_ASSERTING_INT_CAST(UINT16, surface->gdi.outputOriginX);
		surfaceRect.top = WINPR_ASSERTING_INT_CAST(UINT16, surface->gdi.outputOriginY);
		surfaceRect.right = WINPR_ASSERTING_INT_CAST(UINT16, surface->gdi.outputOriginX +
		                                                         surface->gdi.outputTargetWidth);
		surfaceRect.bottom = WINPR_ASSERTING_INT_CAST(UINT16, surface->gdi.outputOriginY +
		                                                          surface->gdi.outputTargetHeight);

		if (rectangles_intersection(&invalidRect, &surfaceRect, &intersection))
		{
			/* Invalid rects are specified relative to surface origin */
			intersection.left -= surfaceRect.left;
			intersection.top -= surfaceRect.top;
			intersection.right -= surfaceRect.left;
			intersection.bottom -= surfaceRect.top;
			region16_union_rect(&surface->gdi.invalidRegion, &surface->gdi.invalidRegion,
			                    &intersection);
		}
	}

	free(pSurfaceIds);
	LeaveCriticalSection(&context->mux);
	IFCALLRET(context->UpdateSurfaces, status, context);

	if (status != CHANNEL_RC_OK)
		goto fail;

fail:
	return status;
}

static UINT32 x11_pad_scanline(UINT32 scanline, UINT32 inPad)
{
	/* Ensure X11 alignment is met */
	if (inPad > 0)
	{
		const UINT32 align = inPad / 8;
		const UINT32 pad = align - scanline % align;

		if (align != pad)
			scanline += pad;
	}

	/* 16 byte alignment is required for ASM optimized code */
	if (scanline % 16)
		scanline += 16 - scanline % 16;

	return scanline;
}

/**
 * Function description
 *
 * @return 0 on success, otherwise a Win32 error code
 */
static UINT xf_CreateSurface(RdpgfxClientContext* context,
                             const RDPGFX_CREATE_SURFACE_PDU* createSurface)
{
	UINT ret = CHANNEL_RC_NO_MEMORY;
	size_t size = 0;
	xfGfxSurface* surface = NULL;
	rdpGdi* gdi = (rdpGdi*)context->custom;
	xfContext* xfc = (xfContext*)gdi->context;
	surface = (xfGfxSurface*)calloc(1, sizeof(xfGfxSurface));

	if (!surface)
		return CHANNEL_RC_NO_MEMORY;

	surface->gdi.codecs = context->codecs;

	if (!surface->gdi.codecs)
	{
		WLog_ERR(TAG, "global GDI codecs aren't set");
		goto out_free;
	}

	surface->gdi.surfaceId = createSurface->surfaceId;
	surface->gdi.width = x11_pad_scanline(createSurface->width, 0);
	surface->gdi.height = x11_pad_scanline(createSurface->height, 0);
	surface->gdi.mappedWidth = createSurface->width;
	surface->gdi.mappedHeight = createSurface->height;
	surface->gdi.outputTargetWidth = createSurface->width;
	surface->gdi.outputTargetHeight = createSurface->height;

	switch (createSurface->pixelFormat)
	{
		case GFX_PIXEL_FORMAT_ARGB_8888:
			surface->gdi.format = PIXEL_FORMAT_BGRA32;
			break;

		case GFX_PIXEL_FORMAT_XRGB_8888:
			surface->gdi.format = PIXEL_FORMAT_BGRX32;
			break;

		default:
			WLog_ERR(TAG, "unknown pixelFormat 0x%" PRIx32 "", createSurface->pixelFormat);
			ret = ERROR_INTERNAL_ERROR;
			goto out_free;
	}

	surface->gdi.scanline = surface->gdi.width * FreeRDPGetBytesPerPixel(surface->gdi.format);
	surface->gdi.scanline = x11_pad_scanline(surface->gdi.scanline,
	                                         WINPR_ASSERTING_INT_CAST(uint32_t, xfc->scanline_pad));
	size = 1ull * surface->gdi.scanline * surface->gdi.height;
	surface->gdi.data = (BYTE*)winpr_aligned_malloc(size, 16);

	if (!surface->gdi.data)
	{
		WLog_ERR(TAG, "unable to allocate GDI data");
		goto out_free;
	}

	ZeroMemory(surface->gdi.data, size);

	if (FreeRDPAreColorFormatsEqualNoAlpha(gdi->dstFormat, surface->gdi.format))
	{
		WINPR_ASSERT(xfc->depth != 0);
		surface->image = LogDynAndXCreateImage(
		    xfc->log, xfc->display, xfc->visual, WINPR_ASSERTING_INT_CAST(uint32_t, xfc->depth),
		    ZPixmap, 0, (char*)surface->gdi.data, surface->gdi.mappedWidth,
		    surface->gdi.mappedHeight, xfc->scanline_pad,
		    WINPR_ASSERTING_INT_CAST(int, surface->gdi.scanline));
	}
	else
	{
		UINT32 width = surface->gdi.width;
		UINT32 bytes = FreeRDPGetBytesPerPixel(gdi->dstFormat);
		surface->stageScanline = width * bytes;
		surface->stageScanline = x11_pad_scanline(
		    surface->stageScanline, WINPR_ASSERTING_INT_CAST(uint32_t, xfc->scanline_pad));
		size = 1ull * surface->stageScanline * surface->gdi.height;
		surface->stage = (BYTE*)winpr_aligned_malloc(size, 16);

		if (!surface->stage)
		{
			WLog_ERR(TAG, "unable to allocate stage buffer");
			goto out_free_gdidata;
		}

		ZeroMemory(surface->stage, size);
		WINPR_ASSERT(xfc->depth != 0);
		surface->image = LogDynAndXCreateImage(
		    xfc->log, xfc->display, xfc->visual, WINPR_ASSERTING_INT_CAST(uint32_t, xfc->depth),
		    ZPixmap, 0, (char*)surface->stage, surface->gdi.mappedWidth, surface->gdi.mappedHeight,
		    xfc->scanline_pad, WINPR_ASSERTING_INT_CAST(int, surface->stageScanline));
	}

	if (!surface->image)
	{
		WLog_ERR(TAG, "an error occurred when creating the XImage");
		goto error_surface_image;
	}

	surface->image->byte_order = LSBFirst;
	surface->image->bitmap_bit_order = LSBFirst;

	region16_init(&surface->gdi.invalidRegion);

	if (context->SetSurfaceData(context, surface->gdi.surfaceId, (void*)surface) != CHANNEL_RC_OK)
	{
		WLog_ERR(TAG, "an error occurred during SetSurfaceData");
		goto error_set_surface_data;
	}

	return CHANNEL_RC_OK;
error_set_surface_data:
	surface->image->data = NULL;
	XDestroyImage(surface->image);
error_surface_image:
	winpr_aligned_free(surface->stage);
out_free_gdidata:
	winpr_aligned_free(surface->gdi.data);
out_free:
	free(surface);
	return ret;
}

/**
 * Function description
 *
 * @return 0 on success, otherwise a Win32 error code
 */
static UINT xf_DeleteSurface(RdpgfxClientContext* context,
                             const RDPGFX_DELETE_SURFACE_PDU* deleteSurface)
{
	rdpCodecs* codecs = NULL;
	xfGfxSurface* surface = NULL;
	UINT status = 0;
	EnterCriticalSection(&context->mux);
	surface = (xfGfxSurface*)context->GetSurfaceData(context, deleteSurface->surfaceId);

	if (surface)
	{
		if (surface->gdi.windowMapped)
			IFCALL(context->UnmapWindowForSurface, context, surface->gdi.windowId);

#ifdef WITH_GFX_H264
		h264_context_free(surface->gdi.h264);
#endif
		surface->image->data = NULL;
		XDestroyImage(surface->image);
		winpr_aligned_free(surface->gdi.data);
		winpr_aligned_free(surface->stage);
		region16_uninit(&surface->gdi.invalidRegion);
		codecs = surface->gdi.codecs;
		free(surface);
	}

	status = context->SetSurfaceData(context, deleteSurface->surfaceId, NULL);

	if (codecs && codecs->progressive)
		progressive_delete_surface_context(codecs->progressive, deleteSurface->surfaceId);

	LeaveCriticalSection(&context->mux);
	return status;
}

static UINT xf_UpdateWindowFromSurface(RdpgfxClientContext* context, gdiGfxSurface* surface)
{
	WINPR_ASSERT(context);
	WINPR_ASSERT(surface);

	rdpGdi* gdi = (rdpGdi*)context->custom;
	WINPR_ASSERT(gdi);

	xfContext* xfc = (xfContext*)gdi->context;
	WINPR_ASSERT(gdi->context);

	if (freerdp_settings_get_bool(gdi->context->settings, FreeRDP_RemoteApplicationMode))
		return xf_AppUpdateWindowFromSurface(xfc, surface);

	WLog_WARN(TAG, "function not implemented");
	return CHANNEL_RC_OK;
}

void xf_graphics_pipeline_init(xfContext* xfc, RdpgfxClientContext* gfx)
{
	rdpGdi* gdi = NULL;
	const rdpSettings* settings = NULL;
	WINPR_ASSERT(xfc);
	WINPR_ASSERT(gfx);

	settings = xfc->common.context.settings;
	WINPR_ASSERT(settings);

	gdi = xfc->common.context.gdi;

	gdi_graphics_pipeline_init(gdi, gfx);

	if (!freerdp_settings_get_bool(settings, FreeRDP_SoftwareGdi))
	{
		gfx->UpdateSurfaces = xf_UpdateSurfaces;
		gfx->CreateSurface = xf_CreateSurface;
		gfx->DeleteSurface = xf_DeleteSurface;
	}
	gfx->UpdateWindowFromSurface = xf_UpdateWindowFromSurface;
}

void xf_graphics_pipeline_uninit(xfContext* xfc, RdpgfxClientContext* gfx)
{
	rdpGdi* gdi = NULL;

	WINPR_ASSERT(xfc);

	gdi = xfc->common.context.gdi;
	gdi_graphics_pipeline_uninit(gdi, gfx);
}
