#ifndef APPS_DEVICE_LINK_WINDOW_H
#define APPS_DEVICE_LINK_WINDOW_H

/**
 * @brief Total seconds of a Device Link pairing window, shared by the
 *        settings Bluetooth page and the setup wizard countdown rings.
 *
 * Mirrors the service default configured in main/app_product_config.c;
 * the apps layer cannot include main headers, so keep the two in sync.
 */
#define APPS_DEVICE_LINK_WINDOW_TOTAL_MS 120000U

#endif /* APPS_DEVICE_LINK_WINDOW_H */
