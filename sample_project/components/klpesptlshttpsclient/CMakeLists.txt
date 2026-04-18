# Embed the server root certificate into the final binary
#
# (If this was a component, we would set COMPONENT_EMBED_TXTFILES here.)
idf_component_register(SRCS "klpesptlshttpsclient.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES esp_wifi nvs_flash esp_event esp-tls  esp_timer  esp_netif lwip)
                    # EMBED_TXTFILES server_root_cert.pem local_server_cert.pem)
