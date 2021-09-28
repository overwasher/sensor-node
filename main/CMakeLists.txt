idf_component_register(SRCS "activity_detection.cpp" "telemetry.c" "overwatcher_communicator.c" "ow_events.c" "main.c" "wifi_manager.c" "accelerometer.c"
                    INCLUDE_DIRS "include"
                    EMBED_TXTFILES overwatcher-ow-dcnick3-me.pem)