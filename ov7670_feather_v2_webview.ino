/*
 * OV7670 camera on Adafruit ESP32 Feather V2  +  WiFi photo viewer
 * ------------------------------------------------------------
 *   - Send "1" over Serial  -> capture one photo per second
 *   - Send "0" over Serial  -> stop
 *   - Photos saved to LittleFS in a ring: /img0.jpg .. /img(RING_SIZE-1).jpg
 *     (oldest slot overwritten when it wraps -> flash never fills)
 *   - Open the board's IP in a browser to view photos:
 *       /            gallery + auto-refreshing "live" latest photo
 *       /latest      newest JPEG
 *       /photo?n=3   a specific slot
 *
 * The OV7670 is NOT an I2C camera: I2C/SCCB only configures it; pixels come
 * over the parallel bus D0-D7 + PCLK/VSYNC/HREF into the ESP32 camera DMA.
 *
 * Library: esp_camera ships with the arduino-esp32 core. WiFi + WebServer
 * are also bundled - no extra installs.
 * ------------------------------------------------------------
 */

#include "esp_camera.h"
#include "img_converters.h"
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>

// ---------------- WiFi credentials ----------------
const char* WIFI_SSID = "iPhone*";
const char* WIFI_PASS = "87654321";

// ---------------- Pin map (Feather V2 GPIO numbers) ----------------
#define PWDN_GPIO_NUM   -1     // OV7670 PWDN  -> GND
#define RESET_GPIO_NUM  -1     // OV7670 RESET -> 3V
#define XCLK_GPIO_NUM   27
#define SIOD_GPIO_NUM   SDA
#define SIOC_GPIO_NUM   SCL
#define Y2_GPIO_NUM     34     // D0
#define Y3_GPIO_NUM     39     // D1
#define Y4_GPIO_NUM     36     // D2
#define Y5_GPIO_NUM     37     // D3
#define Y6_GPIO_NUM     13     // D4
#define Y7_GPIO_NUM     14     // D5
#define Y8_GPIO_NUM     32     // D6
#define Y9_GPIO_NUM     33     // D7
#define VSYNC_GPIO_NUM  25     // A1
#define HREF_GPIO_NUM    4     // A5
#define PCLK_GPIO_NUM   26     // A0

// ---------------- Capture / storage settings ----------------
const uint32_t CAPTURE_INTERVAL_MS = 1000;
const int      RING_SIZE           = 20;
const uint8_t  JPEG_QUALITY        = 80;   // frame2jpg: 1-100, higher = better/bigger

bool      capturing   = false;
uint32_t  lastShotMs  = 0;
int       ringIndex   = 0;
int       newestIndex = -1;
WebServer server(80);

// ============================================================
//  Camera
// ============================================================
bool initCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk  = XCLK_GPIO_NUM;
  config.pin_pclk  = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href  = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;   // older cores: pin_sscb_sda
  config.pin_sccb_scl = SIOC_GPIO_NUM;   // older cores: pin_sscb_scl
  config.pin_pwdn  = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 10000000;
  config.pixel_format = PIXFORMAT_RGB565;
  config.frame_size   = FRAMESIZE_QVGA;
  config.fb_count     = 1;
  config.grab_mode    = CAMERA_GRAB_LATEST;
  config.fb_location  = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) { Serial.printf("Camera init failed: 0x%x\n", err); return false; }
  return true;
}

void takePhoto() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) { Serial.println("Capture failed"); return; }

  uint8_t *jpgBuf = NULL; size_t jpgLen = 0;
  bool ok = frame2jpg(fb, JPEG_QUALITY, &jpgBuf, &jpgLen);
  esp_camera_fb_return(fb);
  if (!ok) { Serial.println("JPEG encode failed"); return; }

  char path[24];
  snprintf(path, sizeof(path), "/img%d.jpg", ringIndex);
  File f = LittleFS.open(path, FILE_WRITE);
  if (f) {
    f.write(jpgBuf, jpgLen); f.close();
    newestIndex = ringIndex;
    Serial.printf("Saved %s (%u bytes)\n", path, (unsigned)jpgLen);
  } else {
    Serial.printf("Could not write %s\n", path);
  }
  free(jpgBuf);
  ringIndex = (ringIndex + 1) % RING_SIZE;
}

// ============================================================
//  Web handlers
// ============================================================
void serveSlot(int n) {
  char path[24];
  snprintf(path, sizeof(path), "/img%d.jpg", n);
  if (n < 0 || !LittleFS.exists(path)) { server.send(404, "text/plain", "no photo yet"); return; }
  File f = LittleFS.open(path, "r");
  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(f, "image/jpeg");
  f.close();
}

void handleLatest() { serveSlot(newestIndex); }

void handlePhoto() {
  int n = server.hasArg("n") ? server.arg("n").toInt() : newestIndex;
  serveSlot(n);
}

void handleRoot() {
  String html =
    "<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>OV7670</title><style>body{font-family:sans-serif;background:#111;color:#eee;margin:0;padding:16px}"
    "img{width:100%;max-width:480px;image-rendering:pixelated;border:1px solid #333}"
    ".grid{display:flex;flex-wrap:wrap;gap:6px;margin-top:16px}.grid img{width:150px}</style></head><body>"
    "<h2>OV7670 live (latest photo)</h2>"
    "<img id=live src='/latest'>"
    "<h3>Saved photos</h3><div class=grid id=grid></div>"
    "<script>"
    "setInterval(()=>{document.getElementById('live').src='/latest?t='+Date.now();},1000);"
    "let g=document.getElementById('grid');"
    "for(let i=0;i<" + String(RING_SIZE) + ";i++){let im=new Image();im.src='/photo?n='+i;"
    "im.onerror=()=>im.remove();g.appendChild(im);}"
    "</script></body></html>";
  server.send(200, "text/html", html);
}

// ============================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nOV7670 + Feather V2 + WiFi viewer");

  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");
  if (!initCamera()) { Serial.println("Halting - check wiring."); while (true) delay(1000); }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(400); Serial.print("."); }
  Serial.printf("\nConnected. Open  http://%s/  in a browser.\n", WiFi.localIP().toString().c_str());

  server.on("/",       handleRoot);
  server.on("/latest", handleLatest);
  server.on("/photo",  handlePhoto);
  server.begin();

  Serial.println("Send '1' to start (1 photo/sec), '0' to stop.");
}

void loop() {
  server.handleClient();

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '1')      { capturing = true;  lastShotMs = 0; Serial.println(">> capturing ON"); }
    else if (c == '0') { capturing = false;                 Serial.println(">> capturing OFF"); }
  }

  if (capturing && (millis() - lastShotMs >= CAPTURE_INTERVAL_MS)) {
    lastShotMs = millis();
    takePhoto();
  }
}
