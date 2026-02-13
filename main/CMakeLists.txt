idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES BLE Upgrade app_update Display Input)
target_add_binary_data(${COMPONENT_TARGET} "ca_cert.pem" TEXT)
