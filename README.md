# AIoT-Based Digits Classification on an ESP32-CAM

This is a project I worked on during my Master's degree at TUM for the advanced course in communications electronics related to the Artificial Intelligence of Things (AIoT).

Salient features:
* A CNN model was trained and a validation accuracy of 94.94% was achieved on a custom MNIST-style dataset.
* Inference time on ESP32-CAM: <0.11 sec.
* Communication with NODE-RED server over WiFi and MQTT.

Room for improvement:
* Increase model complexity and quantise TF Lite model before deployment.
