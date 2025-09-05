idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES BLE Upgrade)
target_add_binary_data(${COMPONENT_TARGET} "ca_cert.pem" TEXT)
