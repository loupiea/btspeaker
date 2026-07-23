#include "bsp_mode.h"

bsp_mode_t bsp_mode_next(bsp_mode_t mode)
{
    switch (mode) {
    case BSP_MODE_BLUETOOTH:
        return BSP_MODE_USB;
    case BSP_MODE_USB:
        return BSP_MODE_AUX;
    case BSP_MODE_AUX:
    default:
        return BSP_MODE_BLUETOOTH;
    }
}

const char *bsp_mode_name(bsp_mode_t mode)
{
    switch (mode) {
    case BSP_MODE_BLUETOOTH:
        return "Bluetooth";
    case BSP_MODE_USB:
        return "U Disk";
    case BSP_MODE_AUX:
        return "AUX";
    default:
        return "Unknown";
    }
}
