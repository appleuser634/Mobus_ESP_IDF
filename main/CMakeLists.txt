idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES ble ota_update app_update display input)
target_add_binary_data(${COMPONENT_TARGET} "ca_cert.pem" TEXT)
