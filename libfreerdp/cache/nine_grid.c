/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * NineGrid Cache
 *
 * Copyright 2012 Marc-Andre Moreau <marcandre.moreau@gmail.com>
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

#include <stdio.h>

#include <winpr/crt.h>

#include <freerdp/update.h>
#include <freerdp/freerdp.h>
#include <winpr/stream.h>

#include "nine_grid.h"
#include "cache.h"

typedef struct
{
	void* entry;
} NINE_GRID_ENTRY;

struct rdp_nine_grid_cache
{
	pDrawNineGrid DrawNineGrid;           /* 0 */
	pMultiDrawNineGrid MultiDrawNineGrid; /* 1 */
	UINT32 paddingA[16 - 2];              /* 2 */

	UINT32 maxEntries;        /* 16 */
	UINT32 maxSize;           /* 17 */
	NINE_GRID_ENTRY* entries; /* 18 */
	UINT32 paddingB[32 - 19]; /* 19 */

	rdpContext* context;
};

static BOOL update_gdi_draw_nine_grid(rdpContext* context,
                                      const DRAW_NINE_GRID_ORDER* draw_nine_grid)
{
	rdpCache* cache = context->cache;
	return IFCALLRESULT(TRUE, cache->nine_grid->DrawNineGrid, context, draw_nine_grid);
}

static BOOL update_gdi_multi_draw_nine_grid(rdpContext* context,
                                            const MULTI_DRAW_NINE_GRID_ORDER* multi_draw_nine_grid)
{
	rdpCache* cache = context->cache;
	return IFCALLRESULT(TRUE, cache->nine_grid->MultiDrawNineGrid, context, multi_draw_nine_grid);
}

void nine_grid_cache_register_callbacks(rdpUpdate* update)
{
	rdpCache* cache = update->context->cache;

	cache->nine_grid->DrawNineGrid = update->primary->DrawNineGrid;
	cache->nine_grid->MultiDrawNineGrid = update->primary->MultiDrawNineGrid;

	update->primary->DrawNineGrid = update_gdi_draw_nine_grid;
	update->primary->MultiDrawNineGrid = update_gdi_multi_draw_nine_grid;
}

rdpNineGridCache* nine_grid_cache_new(rdpContext* context)
{
	rdpNineGridCache* nine_grid = NULL;
	rdpSettings* settings = NULL;

	WINPR_ASSERT(context);

	settings = context->settings;
	WINPR_ASSERT(settings);

	nine_grid = (rdpNineGridCache*)calloc(1, sizeof(rdpNineGridCache));
	if (!nine_grid)
		return NULL;

	nine_grid->context = context;

	nine_grid->maxSize = 2560;
	nine_grid->maxEntries = 256;

	if (!freerdp_settings_set_uint32(settings, FreeRDP_DrawNineGridCacheSize, nine_grid->maxSize))
		goto fail;
	if (!freerdp_settings_set_uint32(settings, FreeRDP_DrawNineGridCacheEntries,
	                                 nine_grid->maxEntries))
		goto fail;

	nine_grid->entries = (NINE_GRID_ENTRY*)calloc(nine_grid->maxEntries, sizeof(NINE_GRID_ENTRY));
	if (!nine_grid->entries)
		goto fail;

	return nine_grid;

fail:
	WINPR_PRAGMA_DIAG_PUSH
	WINPR_PRAGMA_DIAG_IGNORED_MISMATCHED_DEALLOC
	nine_grid_cache_free(nine_grid);
	WINPR_PRAGMA_DIAG_POP
	return NULL;
}

void nine_grid_cache_free(rdpNineGridCache* nine_grid)
{
	if (nine_grid != NULL)
	{
		if (nine_grid->entries != NULL)
		{
			for (size_t i = 0; i < nine_grid->maxEntries; i++)
				free(nine_grid->entries[i].entry);

			free(nine_grid->entries);
		}

		free(nine_grid);
	}
}
