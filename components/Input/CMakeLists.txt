idf_component_register(SRCS "new_component.c"
                        INCLUDE_DIRS "include"
                        REQUIRES driver esp_adc esp_timer log
                    )
