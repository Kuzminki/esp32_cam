#include <Arduino.h>

#include <WiFi.h>
#include "esp_camera.h"
#include "esp_http_server.h"

// ==========================
// WIFI
// ==========================
const char* ssid = "FRITZ!Box 6660 Cable BK";
const char* password = "37434493370901593298";

// CAMERA AI THINKER
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5

#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define LED_PIN 4

unsigned long lastClientActivity = 0;
bool streamActive = false;

httpd_handle_t server = NULL;

bool manualLight = true;

// ======================
// PAGE WEB
// ======================
esp_err_t index_handler(httpd_req_t *req)
{
    const char* html =
    "<html><body>"
    "<h2>ESP32-CAM</h2>"
    "<img src='/stream' width='320'><br><br>"
    "<button onclick=\"fetch('/light/on')\">Light ON</button>"
    "<button onclick=\"fetch('/light/off')\">Light OFF</button>"
    "</body></html>";

    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

// ======================
// SNAPSHOT
// ======================
esp_err_t capture_handler(httpd_req_t *req)
{
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) return httpd_resp_send_500(req);

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_send(req, (const char*)fb->buf, fb->len);

    esp_camera_fb_return(fb);
    return ESP_OK;
}

// ======================
// STREAM
// ======================
esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;

    const char* boundary = "\r\n--frame\r\n";
    const char* part = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";
    char header[64];

    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");

    Serial.println("Stream START");

    // LED ON uniquement si pas forcée OFF manuellement
    digitalWrite(LED_PIN, HIGH);

    while (true)
    {
        fb = esp_camera_fb_get();
        if (!fb)
        {
            Serial.println("Camera capture failed");
            break;
        }

        int hlen = snprintf(header, 64, part, fb->len);

        // Si le client est déconnecté → ces fonctions échouent → break
        if (httpd_resp_send_chunk(req, boundary, strlen(boundary)) != ESP_OK) {
            esp_camera_fb_return(fb);
            break;
        }

        if (httpd_resp_send_chunk(req, header, hlen) != ESP_OK) {
            esp_camera_fb_return(fb);
            break;
        }

        if (httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len) != ESP_OK) {
            esp_camera_fb_return(fb);
            break;
        }

        esp_camera_fb_return(fb);

        delay(30); // contrôle FPS (~30fps max théorique)
    }

    Serial.println("Stream STOP");

    digitalWrite(LED_PIN, LOW);

    return ESP_OK;
}

// ======================
// LIGHT CONTROL
// ======================
esp_err_t light_on_handler(httpd_req_t *req)
{
    manualLight = true;
    digitalWrite(LED_PIN, HIGH);
    httpd_resp_send(req, "ON", 2);
    return ESP_OK;
}

esp_err_t light_off_handler(httpd_req_t *req)
{
    manualLight = false;
    digitalWrite(LED_PIN, LOW);
    httpd_resp_send(req, "OFF", 3);
    return ESP_OK;
}

// ======================
// START SERVER
// ======================
void startServer()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_start(&server, &config);

    httpd_uri_t index_uri = { "/", HTTP_GET, index_handler, NULL };
    httpd_uri_t stream_uri = { "/stream", HTTP_GET, stream_handler, NULL };
    httpd_uri_t capture_uri = { "/capture", HTTP_GET, capture_handler, NULL };
    httpd_uri_t light_on_uri = { "/light/on", HTTP_GET, light_on_handler, NULL };
    httpd_uri_t light_off_uri = { "/light/off", HTTP_GET, light_off_handler, NULL };

    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &stream_uri);
    httpd_register_uri_handler(server, &capture_uri);
    httpd_register_uri_handler(server, &light_on_uri);
    httpd_register_uri_handler(server, &light_off_uri);
}

// ======================
// CAMERA INIT
// ======================
void setupCamera()
{
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
    config.pixel_format = PIXFORMAT_JPEG;

    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 2;

    esp_camera_init(&config);
}

// ======================
// SETUP
// ======================
void setup()
{
    Serial.begin(115200);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println("");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    setupCamera();
    startServer();

    Serial.println("Ready:");
    Serial.println("/");
    Serial.println("/stream");
    Serial.println("/capture");
}

// ======================
void loop()
{
    delay(1000);
}