/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - input_plugin_compat.c                                   *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2014 Bobby Smiles                                       *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "plugins_compat.h"

#include "api/m64p_plugin.h"
#include "backends/api/controller_input_backend.h"
#include "backends/api/rumble_backend.h"
#include "plugin/plugin.h"

#include "main/main.h"
#include "main/netplay.h"

#include <stdint.h>
#include <string.h>
#include <api/m64p_plugin.h>
#include <api/callbacks.h>
#include <SDL_timer.h>

/* One-shot media reload request from the Pak Swap menu, never an N64 button. */
enum { GB_CART_RELOAD_REQUEST = 0x8000, PAK_SWITCH_DELAY_MS = 350 };

static m64p_error input_plugin_get_input(void* opaque, uint32_t* input_)
{
    struct controller_input_compat* cin_compat = (struct controller_input_compat*)opaque;
    BUTTONS keys = { 0 };

    /* first poll controller */
    if (!netplay_is_init())
    {
        if (input.getKeys)
            input.getKeys(cin_compat->control_id, &keys);
    }
    else
    {
        int netplay_controller = netplay_get_controller(cin_compat->control_id);
        if (netplay_controller >= 0)
        {
            //Here we "trick" the input plugin
            //by passing it the controller number that is controlling the player during netplay
            uint8_t plugin = Controls[netplay_controller].Plugin;
            uint8_t present = Controls[netplay_controller].Present;
            if (input.getKeys)
                input.getKeys(netplay_controller, &keys);

            Controls[netplay_controller].Plugin = plugin;
            Controls[netplay_controller].Present = present;
        }
        cin_compat->last_input = keys.Value; //disable pak switching for netplay
        cin_compat->last_pak_type = Controls[cin_compat->control_id].Plugin; //disable pak switching for netplay
    }

    /* return an error if controller is not plugged */
    if (!Controls[cin_compat->control_id].Present) {
        return M64ERR_SYSTEM_FAIL;
    }

    if (!netplay_is_init() && cin_compat->cont != NULL
        && cin_compat->cont->flavor == &g_standard_controller_flavor)
    {
        const int media_changed = cin_compat->gb_cart_switch_enabled
            && (keys.Value & GB_CART_RELOAD_REQUEST)
            && !(cin_compat->last_input & GB_CART_RELOAD_REQUEST);
        if (cin_compat->last_pak_type != Controls[cin_compat->control_id].Plugin || media_changed)
        {
            if (cin_compat->cont->ipak != NULL) change_pak(cin_compat->cont, NULL, NULL);
            /* Polls include status/read/write commands, so count elapsed time,
             * not calls. Games must have time to observe the missing Pak. */
            cin_compat->pak_switch_delay = SDL_GetTicks() + PAK_SWITCH_DELAY_MS;
            if (cin_compat->pak_switch_delay == 0) cin_compat->pak_switch_delay = 1;
            cin_compat->gb_switch_delay |= media_changed;
            cin_compat->main_switch_pak = main_switch_plugin_pak;
        }
        if (cin_compat->pak_switch_delay != 0
            && (int32_t)(SDL_GetTicks() - cin_compat->pak_switch_delay) >= 0)
        {
            if (cin_compat->gb_switch_delay) main_change_gb_cart(cin_compat->control_id);
            cin_compat->main_switch_pak(cin_compat->control_id);
            cin_compat->pak_switch_delay = 0;
            cin_compat->gb_switch_delay = 0;
            cin_compat->main_switch_pak = NULL;
        }
    }

    cin_compat->last_pak_type = Controls[cin_compat->control_id].Plugin;
    cin_compat->last_input = keys.Value;

    *input_ = keys.Value & ~GB_CART_RELOAD_REQUEST;
    return M64ERR_SUCCESS;
}

const struct controller_input_backend_interface
    g_icontroller_input_backend_plugin_compat =
{
    input_plugin_get_input
};


static void input_plugin_rumble_exec(void* opaque, enum rumble_action action)
{
    int control_id = *(int*)opaque;

    if (input.controllerCommand == NULL) {
        return;
    }

    //This is for netplay, -1 means there is no local controller controlling this player
    if (control_id == -1) {
        return;
    }

    static const uint8_t rumble_cmd_header[] =
    {
        0x23, 0x01, /* T=0x23, R=0x01 */
        JCMD_PAK_WRITE,
        0xc0, 0x1b, /* address=0xc000 | crc=0x1b */
    };

    uint8_t cmd[0x26];

    uint8_t rumble_data = (action == RUMBLE_START)
        ? 0x01
        : 0x00;

    /* build rumble command */
    memcpy(cmd, rumble_cmd_header, 5);
    memset(cmd + 5, rumble_data, 0x20);
    cmd[0x25] = 0; /* dummy data CRC */

    input.controllerCommand(control_id, cmd);
}

const struct rumble_backend_interface
    g_irumble_backend_plugin_compat =
{
    input_plugin_rumble_exec
};


static void input_plugin_read_controller(void* opaque,
    const uint8_t* tx, const uint8_t* tx_buf,
    uint8_t* rx, uint8_t* rx_buf)
{
    int control_id = *(int*)opaque;

    if (input.readController == NULL) {
        return;
    }

    //This is for netplay, -1 means there is no local controller controlling this player
    if (control_id == -1) {
        return;
    }

    /* UGLY: use negative offsets to get access to non-const tx pointer */
    input.readController(control_id, rx - 1);
}

void input_plugin_controller_command(void* opaque,
    uint8_t* tx, const uint8_t* tx_buf,
    const uint8_t* rx, const uint8_t* rx_buf)
{
    int control_id = *(int*)opaque;

    if (input.controllerCommand == NULL) {
        return;
    }

    //This is for netplay, -1 means there is no local controller controlling this player
    if (control_id == -1) {
        return;
    }

    input.controllerCommand(control_id, tx);
}

const struct joybus_device_interface
    g_ijoybus_device_plugin_compat =
{
    NULL,
    input_plugin_read_controller,
    input_plugin_controller_command,
};
