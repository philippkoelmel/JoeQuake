/*
Copyright (C) 2023 Matthew Earl

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "../quakedef.h"
#include "demoparse.h"
#include "demosummary.h"


typedef struct
{
    FILE *demo_file;
    float level_time;
    demo_summary_t *demo_summary;
    int model_num;
    qboolean intermission_seen;
    qboolean any_intermission_seen;
} ds_ctx_t;


static qboolean
DS_Read_cb (void *dest, unsigned int size, void *ctx)
{
    ds_ctx_t *pctx = ctx;
    return fread (dest, size, 1, pctx->demo_file) != 0;
}


static dp_cb_response_t
DS_ServerInfoModel_cb (const char *model, void *ctx)
{
    char *map_name;
    ds_ctx_t *pctx = ctx;
    demo_summary_t *ds = pctx->demo_summary;
    int i;

    if (pctx->model_num == 0 && ds->num_maps < DS_MAX_MAP_NAMES) {
        map_name = ds->maps[ds->num_maps];
		Q_strncpyz(map_name, (char *)model, DS_MAP_NAME_SIZE);
        COM_StripExtension(COM_SkipPath(map_name), map_name);

        // `num_maps` is incremented on intermission, so that we don't list
        // start map.
    }

    pctx->model_num ++;

    return DP_CBR_CONTINUE;
}


static dp_cb_response_t
DS_ServerInfo_cb (int protocol, unsigned int protocol_flags,
                  const char *level_name, void *ctx)
{
    ds_ctx_t *pctx = ctx;

    pctx->model_num = 0;
    pctx->level_time = 0.0;
    pctx->intermission_seen = false;

    if (!pctx->any_intermission_seen) {
        memset(pctx->demo_summary->client_names, 0,
               sizeof(pctx->demo_summary->client_names));
    }
    return DP_CBR_CONTINUE;
}


static dp_cb_response_t
DS_UpdateName_cb (int client_num, const char *name, void *ctx)
{
    ds_ctx_t *pctx = ctx;

    // Take client names from the first level with an intermission.
    if (!pctx->any_intermission_seen
            && client_num >= 0 && client_num < DS_MAX_CLIENTS) {
        Q_strncpyz(pctx->demo_summary->client_names[client_num], (char *)name,
                   MAX_SCOREBOARDNAME);
    }

    return DP_CBR_CONTINUE;
}


static dp_cb_response_t
DS_Time_cb (float time, void *ctx)
{
    ds_ctx_t *pctx = ctx;

    pctx->level_time = time;
    return DP_CBR_CONTINUE;
}


static dp_cb_response_t
DS_Intermission_cb (void *ctx)
{
    ds_ctx_t *pctx = ctx;

    if (!pctx->intermission_seen) {
        pctx->demo_summary->total_time += pctx->level_time;
        pctx->demo_summary->num_maps ++;
        pctx->intermission_seen = true;
        pctx->any_intermission_seen = true;
    }

    return DP_CBR_CONTINUE;
}


qboolean
DS_GetDemoSummary (FILE *demo_file, demo_summary_t *demo_summary)
{
    qboolean ok = true;

    dp_err_t dprc;
    dp_callbacks_t callbacks = {
        .read = DS_Read_cb,
        .server_info_model = DS_ServerInfoModel_cb,
        .server_info = DS_ServerInfo_cb,
        .update_name = DS_UpdateName_cb,
        .time = DS_Time_cb,
        .intermission = DS_Intermission_cb,
        .finale = DS_Intermission_cb,
    };
    ds_ctx_t ctx = {
        .demo_file = demo_file,
        .level_time = 0.0,
        .demo_summary = demo_summary,
    };

    memset(demo_summary, 0, sizeof(*demo_summary));

    dprc = DP_ReadDemo(&callbacks, &ctx);

    if (dprc != DP_ERR_SUCCESS) {
        Con_Printf("Error parsing demo: %u\n", dprc);
        ok = false;
    }

    return ok;
}
