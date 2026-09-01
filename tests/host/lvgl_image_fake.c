#include "app_manager.h"

lv_obj_t *lv_image_create(lv_obj_t *parent)
{
    return lv_obj_create(parent);
}

void lv_image_set_src(lv_obj_t *object, const lv_image_dsc_t *source)
{
    (void)object;
    (void)source;
}

esp_err_t app_manager_get_image(uint32_t semantic_id,
                                const lv_image_dsc_t **image)
{
    (void)semantic_id;
    if (image != NULL)
    {
        *image = NULL;
    }
    return ESP_ERR_NOT_FOUND;
}
