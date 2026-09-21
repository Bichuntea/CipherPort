idf_component_register(
    SRCS "main.c"
         "app_config.c"
         "password_manager_app.c"
         "password_manager_model.c"
         "lv_font_cipherport_14.c"
         "lv_font_cipherport_19.c"
         "lv_font_cipherport_cjk_12.c"
         "vault_store.c"
         "web_manager.c"
    INCLUDE_DIRS "."
    EMBED_FILES "web_assets/index.html.gz"
                "../web-manager/dist/assets/cipherport-logo.png"
    REQUIRES bsp esp_event esp_http_server esp_netif esp_timer esp_wifi json mbedtls nvs_flash
)
