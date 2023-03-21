idf_component_register(SRCS "mesh_main.c"
                            "mesh_netif.c"
                            "mqtt_app.c"
                            "st7789.c" "pngle.c" "fontx.c"
                    INCLUDE_DIRS "." "include")
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")