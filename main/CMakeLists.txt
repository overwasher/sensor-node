idf_component_register(SRCS "telemetry.c" "overwatcher_communicator.c" "ow_events.c" "activity_detection.c" "main.c" "wifi_manager.c" "accelerometer.c"
                    INCLUDE_DIRS "include"
                    EMBED_TXTFILES overwatcher-ow-dcnick3-me.pem)