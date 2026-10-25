#ifndef M64P_STADIUM_COMPAT_H
#define M64P_STADIUM_COMPAT_H

#include "rom.h"
#include <string.h>

/* Limit the Transfer Pak backport to known US retail Stadium releases.
 * Do not use filenames or user-editable GoodName values for this gate. */
static inline int stadium_transferpak_compat(void)
{
    return strcmp(ROM_SETTINGS.MD5, "ED1378BC12115F71209A77844965BA50") == 0
        || strcmp(ROM_SETTINGS.MD5, "40E03EDA831C01E0A12294287FD240E2") == 0
        || strcmp(ROM_SETTINGS.MD5, "6DC6820CEF755FC1253D06DF45C9BD2A") == 0
        || strcmp(ROM_SETTINGS.MD5, "1561C75D11CEDF356A8DDB1A4A5F9D5D") == 0;
}

#endif
