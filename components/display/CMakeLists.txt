idf_component_register(SRCS "component_stub.c" "src/font_headupdaisy14x16.cpp"
                            "src/font_headupdaisy14x8.cpp"
                            "src/font_misaki_gothic16.cpp"
                            "src/font_misaki_gothic8.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES ota_update ui application)
