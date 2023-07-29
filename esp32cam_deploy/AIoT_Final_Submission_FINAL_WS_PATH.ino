// AIoT-Based Digits Classification using a CNN
// By Zain Amir Zaman
// Matriculation ID: 03754975

// IMPORTANT: If you face camera initialisation problems, simply reset the device.

#include "esp_camera.h"
#include <WebSocketsClient_Generic.h>
#include <MQTTPubSubClient_Generic.h>
#include <WiFiClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <EloquentTinyML.h>
#include "tflite_model.h"
#include "String.h"

// -- -- -- -- Debugging control knobs -- -- -- -- //

// To enable periodic capture (debugging), enable periodic_capture and set disable_recognise_command to true.
// To enable capture control by NODE-RED, set disable_recognise_command to false and set periodic_capture to true.

// The current implementation works for white foreground and black background. To recognise black foreground
// and white background, set image_inversion to true.
e
const bool skip_connection = false;               // Default: false
const bool disable_recognise_command = false;     // Default: false
const bool periodic_capture = false;              // Default: false
const bool enable_centering = true;               // Default: true
const bool image_inversion = true;                // Default: true

// -- -- -- -- Constants -- -- -- -- //

#define NUMBER_OF_INPUTS 784        // Length of input vector to TinyML model
#define NUMBER_OF_OUTPUTS 10        // Length of output vector
#define TENSOR_ARENA_SIZE 24*1024   // Determined by trial and error

Eloquent::TinyML::TfLite<NUMBER_OF_INPUTS, NUMBER_OF_OUTPUTS, TENSOR_ARENA_SIZE> ml;
float temp_downsized_image[NUMBER_OF_INPUTS];   // Stores the centered image

#define MQTT_PUBSUB_LOGLEVEL 1

// Select camera model
//#define CAMERA_MODEL_WROVER_KIT
//#define CAMERA_MODEL_ESP_EYE
//#define CAMERA_MODEL_M5STACK_PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE
#define CAMERA_MODEL_AI_THINKER

#define WS_SERVER "noderedlnxse20220516.o1jkfiv0l98.eu-de.codeengine.appdomain.cloud"
#define WS_PATH "/ws/mqtt"
#define WS_PORT  443               

#define FRAME_W 96                  // Width of raw image
#define FRAME_H 96                  // Height of raw image
#define ML_INPUT_W 28               // Width of image fed to model
#define ML_INPUT_H 28               // Height of image fed to model

// -- -- -- -- Camera configuration variables / library -- -- -- -- //

#include "camera_pins.h"
camera_config_t config;

// -- -- -- -- WiFi/MQTT server configuration variables -- -- -- -- //

// WiFi config
const char* ssid = "Advtopic";
const char* password = "Adv123Topic";

// MQTT config
const char* mqttServer = "192.168.0.107";
const char* HostName = "ESP32-CAM by Zain";
String pub_topic = "RESULT";
String sub_topic = "COMMAND1";

WiFiClient espClient;
WebSocketsClient client;
MQTTPubSub::PubSubClient <20000> mqttClient;

void init_camera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_GRAYSCALE;

  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  
  config.frame_size = FRAMESIZE_96X96;
  config.fb_count = 2;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.println("init failed");
  }
}

void init_wifi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  client.beginSSL(WS_SERVER, WS_PORT, WS_PATH); 
  client.setReconnectInterval(5000);
  
  mqttClient.begin(client);
    while (!mqttClient.connect(HostName))
  {
    Serial.print(".");
    delay(1000);
  }

  Serial.println(" connected!");
}

// Publish an MQTT message via JSON format

void publish_mqtt(String pred, String prob) {
  DynamicJsonDocument json_doc(10000);
  json_doc["id"] = "03754975";
  json_doc["prediction"] = pred;
  json_doc["probability"] = prob;
  String json_msg;
  serializeJson(json_doc, json_msg);
  delay(1);
  mqttClient.publish(pub_topic, json_msg);
  Serial.println(json_msg);
}

void subscribe_mqtt(void) {
  mqttClient.subscribe(sub_topic, [](const String & payload, const size_t size) {
    (void) size;
    Serial.println("Message received! Topic: ");
    Serial.print(sub_topic);
    Serial.print(", Message" + payload.substring(0, 30));
    Serial.println("");
  });
}

void messageReceived(String &topic, String &payload) {
 Serial.println("incoming: " + topic + " - " + payload);
}

void reconnect() {
  while (!mqttClient.connect(HostName)) {
    Serial.println("Reattempting MQTT connection...");
    if (mqttClient.connect(HostName)) {
      Serial.println("Connected!");
    } else {
      Serial.println("Failed, rc=");
      Serial.println(mqttClient.update());
      Serial.println("Trying again...");
      delay(5000);
    }
  }
}

void callback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  String messageTemp;
  
  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();
}

// Inference function. Prints out prediction and the confidence probabilities of each output class.

void perform_inference(float* x_test) {
  float y_pred_prob[10] = {0};          // Vector of probabilities
  uint32_t start = micros();

  ml.predict(x_test, y_pred_prob);      // Perform inference and retreive confidence probabilities for each class

  uint32_t timeit = micros() - start;

  Serial.print("It took ");
  Serial.print(timeit);
  Serial.println(" micros to run inference.");

  Serial.print("Predicted probabilities are: ");            // Print probabilities vector

  float max_prob = -1;
  for (int i = 0; i < 10; i++) {
      Serial.print(y_pred_prob[i]);
      if (max_prob == -1) max_prob = y_pred_prob[i];
      else if (y_pred_prob[i] > max_prob) max_prob = y_pred_prob[i];
      Serial.print(i == 9 ? '\n' : ',');
  }

  Serial.print("Predicting from probabilities vector: ");
  Serial.println(ml.probaToClass(y_pred_prob));             // Performing prediction by considering the class with the highest confidence probability
  Serial.print("Using predictClass() function: ");
  uint8_t pred = ml.predictClass(x_test);
  Serial.println(ml.predictClass(x_test));                  // Performing prediction using built-in function

  publish_mqtt(String(pred), String(max_prob));
}

void take_picture(void) {

  // Capture image and store
  
  Serial.println("Taking picture");
  camera_fb_t *tmpimage = esp_camera_fb_get();
  if (!tmpimage) Serial.println("Capture failed."); else Serial.println("Capture successful.");
  
  // Downsize and normalise 96x96 image to a 28x28 image

  const int row_ratio = FRAME_H/ML_INPUT_H;
  const int col_ratio = FRAME_W/ML_INPUT_W;
  
  float downsized_image[ML_INPUT_H*ML_INPUT_W];

  if (!image_inversion)
    for (int i = 0; i < ML_INPUT_H; i++) {
      for (int j = 0; j < ML_INPUT_W; j++) {
        // Perform thresholding to remove noise artifacts
        if (tmpimage->buf[i*row_ratio*FRAME_H+j*col_ratio] < 245) downsized_image[i*ML_INPUT_H+j] = 0.0;
        else downsized_image[i*ML_INPUT_H+j] = (float)tmpimage->buf[i*row_ratio*FRAME_H+j*col_ratio]/(float)255.0;
      }
    }
  else {
    for (int i = 0; i < ML_INPUT_H; i++) {
      for (int j = 0; j < ML_INPUT_W; j++) {
        // Perform thresholding to remove noise artifacts
        if (tmpimage->buf[i*row_ratio*FRAME_H+j*col_ratio] > 100) downsized_image[i*ML_INPUT_H+j] = 0.0;
        else downsized_image[i*ML_INPUT_H+j] = (255.0-(float)tmpimage->buf[i*row_ratio*FRAME_H+j*col_ratio])/(float)255.0;
  
        // Remove 2 px boundaries since the image has non-zero values here for some reason. Messes up the cropping algorithm.
        if ((i == 0) || (j == 0) || (i == ML_INPUT_H - 1) || (j == ML_INPUT_W - 1)) downsized_image[i*ML_INPUT_H+j] = 0.0;
        if ((i == 1) || (j == 1) || (i == ML_INPUT_H - 2) || (j == ML_INPUT_W - 2)) downsized_image[i*ML_INPUT_H+j] = 0.0;
      }
    }
  }

  esp_camera_fb_return(tmpimage);  // Frees memory allocated by esp_camera_fb_get()

  // Print (un-normalised) downsized image

  if (!image_inversion) {
    Serial.println("Printing image: ");
    for (int h = 0; h < ML_INPUT_H; h++) {
      for (int w = 0; w < ML_INPUT_H; w++) {
        if (tmpimage->buf[row_ratio*h*FRAME_H+col_ratio*w] < 245) Serial.print("0  ");
        else Serial.print(tmpimage->buf[row_ratio*h*FRAME_H+col_ratio*w]);
        Serial.print(",");
      }
      Serial.print("\n");
    }
    Serial.println("");
  }
  else {
    Serial.println("Printing image: ");
    for (int h = 0; h < ML_INPUT_H; h++) {
      for (int w = 0; w < ML_INPUT_H; w++) {
        if (tmpimage->buf[row_ratio*h*FRAME_H+col_ratio*w] > 100) Serial.print("0  ");
        else Serial.print(255-tmpimage->buf[row_ratio*h*FRAME_H+col_ratio*w]);
        Serial.print(",");
      }
      Serial.print("\n");
    }
    Serial.println("");
  }

  if (enable_centering) {

    // Extract digit from image
    
    int x1 = -1;
    int x2 = -1;
    int y1 = -1;
    int y2 = -1;
  
    for (int i = 0; i < ML_INPUT_H; i++) {
      for (int j = 0; j < ML_INPUT_W; j++) {
        // Find x1
        if (downsized_image[i*ML_INPUT_H+j] > 0) {
          if ((x1 == -1)) x1 = j;
          else if (j < x1) x1 = j;
        }
        // Find x2
        if (downsized_image[i*ML_INPUT_H+j] > 0) {
          if (x2 == -1) x2 = j;
          else if (j > x2) x2 = j;
        }
        // Find y1
        if (downsized_image[i*ML_INPUT_H+j] > 0) {
          if (y1 == -1) y1 = i;
          else if (i < y1) y1 = i;
        }
        // Find y2
        if (downsized_image[i*ML_INPUT_H+j] > 0) {
          if (y2 == -1) y2 = i;
          else if (i > y2) y2 = i;
        }
      }
    }
  
    // Shift digit to centre of 28x28 image
    
    int img_w = x2 - x1 + 1;
    int img_h = y2 - y1 + 1;
    int x_start = ML_INPUT_W/2 - img_w/2;
    int x_end = ML_INPUT_W/2 + img_w/2;
    int y_start = ML_INPUT_H/2 - img_h/2;
    int y_end = ML_INPUT_H/2 + img_h/2;
    
    for (int i = 0; i < NUMBER_OF_INPUTS; i++) {
        temp_downsized_image[i] = 0;
    }
  
    for (int i = y_start; i < y_end; i++) {
      for (int j = x_start; j < x_end+1; j++) {
        temp_downsized_image[i*ML_INPUT_H+j] = downsized_image[(i-y_start+y1)*ML_INPUT_H+(j-x_start+x1)];
      }
    }
  
    // Perform inference and print result
  
    perform_inference(temp_downsized_image);
  }
  else perform_inference(downsized_image);
  
  Serial.println("Picture sent");
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  init_camera();
  
  Serial.println("Camera Ready!");
  Serial.println("\0");
  
  if (!skip_connection) {
    init_wifi();
    mqttClient.subscribe(sub_topic, [](const String & payload, const size_t size) {
      (void) size;
      Serial.print("Subcribed to ");
      Serial.print(sub_topic); Serial.print(" => ");
      Serial.println(payload);
      if ((payload == "Recognize") && (!disable_recognise_command)) take_picture();
    });
  }

  ml.begin(tflite_model_cnn_28);   // Initialise model and let the magic begin
}

void loop() {
  if (!skip_connection) mqttClient.update();
  if (periodic_capture) {
    delay(4000);
    if (!image_inversion) take_picture();
    else take_picture();
  } else delay(500);
}
