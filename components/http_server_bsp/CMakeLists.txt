idf_component_register(
  SRCS "server_bsp.cpp"
  PRIV_REQUIRES sdcard_bsp esp_timer driver esp_http_server button_bsp esp_wifi nvs_flash json espressif__mdns 78__esp-wifi-connect
  INCLUDE_DIRS "./")
