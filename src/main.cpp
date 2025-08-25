#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_MLX90640.h>

const char* ssid     = "SUNSEA_2.4GHz";
const char* password = "00001111";

Adafruit_MLX90640 mlx;
float frame[32 * 24];
WebServer server(80);

// ค่าอุณหภูมิ
float avgTemp = 0;
float maxTemp = 0;
float minTemp = 100;
float sumTemp = 0;

// GPIO Pin
#define volt_BAT  34
#define LED_G     18
#define LED_B     19
#define LED_R     23
#define buzzer    25
#define lazer     26

// Alarm system (non-blocking)
unsigned long alarmPrevMillis = 0;
bool alarmState = false;
int alarmStep = 0;
bool alarmActive = false;

// Interval สำหรับการอ่านเซ็นเซอร์
unsigned long lastReadTime = 0;
const unsigned long readInterval = 500; // ms อ่านทุก 0.5s

// ตั้งค่าเวลา Buzzer/LED ให้ถี่ขึ้น
#define alarmOnDuration 100   // Buzzer ON 100ms
#define alarmOffDuration 50   // Buzzer OFF 50ms

///////////////////////////////////////////////////////
// Alarm Non-Blocking
///////////////////////////////////////////////////////
void startAlarm() {
  if (!alarmActive) {
    alarmActive = true;
    alarmStep = 0;
    alarmPrevMillis = millis();
    alarmState = false;
  }
}

void stopAlarm() {
  alarmActive = false;
  digitalWrite(buzzer, LOW);
  digitalWrite(LED_R, LOW);
  digitalWrite(LED_G, LOW);
}

void handleAlarm() {
  if (!alarmActive) return;

  unsigned long now = millis();

  if (alarmState) { // Alarm ON
    if (now - alarmPrevMillis >= alarmOnDuration) {
      alarmPrevMillis = now;
      alarmState = false;
      digitalWrite(buzzer, LOW);
      digitalWrite(LED_R, LOW);
      digitalWrite(LED_G, HIGH); // LED เขียวช่วงพัก
      alarmStep++;
      if (alarmStep >= 20) { // 10 cycles ON+OFF
        stopAlarm();
      }
    }
  } else { // Alarm OFF
    if (now - alarmPrevMillis >= alarmOffDuration) {
      alarmPrevMillis = now;
      alarmState = true;
      digitalWrite(buzzer, HIGH);
      digitalWrite(LED_R, HIGH);
      digitalWrite(LED_G, LOW);
    }
  }
}

///////////////////////////////////////////////////////
// อ่านค่าอุณหภูมิ (background)
///////////////////////////////////////////////////////
void checkTemperature() 
{
  if (millis() - lastReadTime < readInterval) return;
  lastReadTime = millis();

  if (mlx.getFrame(frame) != 0) {
    Serial.println("Read failed");
    return;
  }

  maxTemp = 0;    
  minTemp = 100;
  avgTemp = 0; 
  sumTemp = 0;

  for (int i = 0; i < 768; i++) {
    sumTemp += frame[i];
    if (frame[i] > maxTemp) maxTemp = frame[i];
    if (frame[i] < minTemp) minTemp = frame[i];
  }

  avgTemp = sumTemp / 768.0;

  Serial.printf("Avg: %.1f  Max: %.1f  Min: %.1f\n", avgTemp, maxTemp, minTemp);

  #define alarmTemp 50
  if (maxTemp < 20) {
    digitalWrite(LED_B, HIGH);
    digitalWrite(LED_G, LOW);
    digitalWrite(LED_R, LOW);
  } 
  else if (maxTemp < 30) {
    digitalWrite(LED_B, LOW);
    digitalWrite(LED_G, HIGH);
    digitalWrite(LED_R, LOW);
  } 
  else if (maxTemp < alarmTemp) {
    digitalWrite(LED_B, LOW);
    digitalWrite(LED_G, LOW);
    digitalWrite(LED_R, HIGH);
  }
  else {
    startAlarm();  // ถ้าเกิน alarmTemp → Alarm ทำงาน
  }
}

///////////////////////////////////////////////////////
// Web Server Pages
///////////////////////////////////////////////////////
void handleRoot() {
  server.send(200, "text/html", R"rawliteral(
  <html>
  <head><title>Thermal View</title></head>
  <body>
    <h2>R-GUS Thermal Camera test.</h2>
    <div>
      <b>Average Temp:</b> <span id="avg"></span> &deg;C<br>
      <b>Max Temp:</b> <span id="max"></span> &deg;C<br>
      <b>Min Temp:</b> <span id="min"></span> &deg;C<br>
      <b>Date/Time:</b> <span id="datetime"></span>
    </div>
    <canvas id="thermal" width="640" height="480"></canvas>
    <script>
      const canvas = document.getElementById('thermal');
      const ctx = canvas.getContext('2d');
      const cols = 32, rows = 24;
      const w = canvas.width / cols;
      const h = canvas.height / rows;

      async function update() {
        const res = await fetch('/frame');
        const data = await res.text();
        const values = data.trim().split(',').map(parseFloat);

        for (let i = 0; i < values.length; i++) {
          const temp = values[i];
          const x = (i % cols);
          const y = Math.floor(i / cols);

          const color = getColor(temp);
          ctx.fillStyle = color;
          ctx.fillRect(x * w, y * h, w, h);
        }
      }

      async function updateStats() {
        const res = await fetch('/stats');
        const [avg, max, min] = (await res.text()).split(',');
        document.getElementById('avg').textContent = avg;
        document.getElementById('max').textContent = max;
        document.getElementById('min').textContent = min;
      }

      function getColor(t) {
        const minT = 20, maxT = 40;
        const ratio = Math.min(Math.max((t - minT) / (maxT - minT), 0), 1);
        const r = Math.floor(255 * ratio);
        const b = 255 - r;
        return `rgb(${r},0,${b})`;
      }
      
      function updateDateTime() {
        const now = new Date();
        const dateStr = now.toLocaleDateString();
        const timeStr = now.toLocaleTimeString();
        document.getElementById('datetime').textContent = dateStr + ' ' + timeStr;
      }

      setInterval(update, 500);
      setInterval(updateStats, 1000);
      setInterval(updateDateTime, 1000);
      updateDateTime();
    </script>
  </body>
  </html>
  )rawliteral");
}

void handleFrame() {
  String response = "";
  for (int i = 0; i < 768; i++) {
    response += String(frame[i], 1);
    if (i < 767) response += ",";
  }
  server.send(200, "text/plain", response);
}

void handleStats() {
  String stats = String(avgTemp, 1) + "," + String(maxTemp, 1) + "," + String(minTemp, 1);
  server.send(200, "text/plain", stats);
}

///////////////////////////////////////////////////////
// Setup
///////////////////////////////////////////////////////
void setup() {
  pinMode(LED_B, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_R, OUTPUT);
  pinMode(buzzer, OUTPUT);
  pinMode(lazer, OUTPUT);
  pinMode(volt_BAT, INPUT);

  Serial.begin(115200);
  Serial.println("Starting...");

  // Connect WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  Serial.println("\nConnected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Init MLX90640
  Wire.begin();
  if (!mlx.begin(0x33, &Wire)) {
    Serial.println("MLX90640 not found!");
    while (1);
  }
  mlx.setMode(MLX90640_INTERLEAVED);
  Serial.println("MLX90640 initialized successfully!");

  // Init Web Server
  server.on("/", handleRoot);
  server.on("/frame", handleFrame);
  server.on("/stats", handleStats);
  server.begin();
}

///////////////////////////////////////////////////////
// Loop
///////////////////////////////////////////////////////
void loop() {
  server.handleClient();   // เว็บเซิร์ฟเวอร์
  handleAlarm();           // จัดการ Buzzer/LED Alarm
  checkTemperature();      // อ่านอุณหภูมิแม้ไม่เปิดเว็บ

  // startAlarm();
  // handleAlarm();

}
