
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>
#include <LittleFS.h>
#include <time.h>


#define SS_PIN   D8
#define RST_PIN  D3
#define SERVO_PIN D1

const char* ssid = "vivo Y29";
const char* password = "00001111";
const String masterPassword = "1234";

MFRC522 mfrc522(SS_PIN, RST_PIN);
ESP8266WebServer server(80);
Servo doorServo;


String authorizedUIDs[] = {"DD7AD505", "D9F26A05"}; 
String authorizedNames[] = {"Danuja", "Kulindu"};
int totalCards = 2;

String lockStatus = "Locked";
String lastPerson = "None";
bool emergencyLock = false;


const char *LOG_PATH = "/access_log.csv";


const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;       
const int   daylightOffset_sec = 0;  


String getTimestamp() {
  time_t now = time(nullptr);
  if (now < 1600000000) { 
    unsigned long ms = millis() / 1000;
    unsigned long secs = ms % 60;
    unsigned long mins = (ms / 60) % 60;
    unsigned long hours = (ms / 3600) % 24;
    char buf[32];
    sprintf(buf, "uptime %02lu:%02lu:%02lu", hours, mins, secs);
    return String(buf);
  }
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  char buf[32];
  sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d",
          timeinfo.tm_year + 1900,
          timeinfo.tm_mon + 1,
          timeinfo.tm_mday,
          timeinfo.tm_hour,
          timeinfo.tm_min,
          timeinfo.tm_sec);
  return String(buf);
}


void appendLog(const String &name) {
  if (!LittleFS.begin()) {
    Serial.println("LittleFS.begin() failed - cannot log");
    return;
  }
  File f = LittleFS.open(LOG_PATH, "a");
  if (!f) {
    Serial.println("Failed to open log file for append");
    LittleFS.end();
    return;
  }
  String line = name + "," + getTimestamp();
  f.println(line);
  f.close();
  LittleFS.end();
}


String readLogAsJSON() {
  String json = "[";
  if (!LittleFS.begin()) {
    Serial.println("LittleFS.begin() failed - cannot read log");
    return "[]";
  }
  File f = LittleFS.open(LOG_PATH, "r");
  if (!f) {
    LittleFS.end();
    return "[]";
  }
  bool first = true;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int commaIndex = line.indexOf(',');
    String name = (commaIndex >= 0) ? line.substring(0, commaIndex) : "Unknown";
    String ts   = (commaIndex >= 0) ? line.substring(commaIndex + 1) : "";
    // escape quotes
    name.replace("\"", "\\\"");
    ts.replace("\"", "\\\"");
    if (!first) json += ",";
    json += "{\"name\":\"" + name + "\",\"time\":\"" + ts + "\"}";
    first = false;
  }
  f.close();
  LittleFS.end();
  json += "]";
  return json;
}


void unlockDoor(String name) {
  if (emergencyLock) {
    Serial.println("LOCKDOWN ACTIVE -> unlock blocked");
    return;
  }

  lockStatus = "Unlocked by " + name;
  lastPerson = name;

  // persistent log
  appendLog(name);

  doorServo.write(180);
  Serial.println("Door Unlocked by: " + name);
  delay(5000);
  doorServo.write(0);
  lockStatus = "Locked";
  Serial.println("Door Locked");
}


String checkAuthorization(String tagUID) {
  tagUID.toUpperCase();
  for (int i = 0; i < totalCards; i++) {
    if (tagUID == authorizedUIDs[i]) return authorizedNames[i];
  }
  return "";
}

// ===== HTML page =====
String htmlPage() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>RFID Door Dashboard</title><style>"
                "body{font-family:Arial;background:#0D1B2A;color:#fff;text-align:center;margin:0;padding:0}"
                ".card{background:#1B263B;margin:18px auto;padding:18px;border-radius:12px;width:92%;max-width:760px}"
                ".status{padding:12px;border-radius:8px;font-size:20px;margin-bottom:10px}"
                ".locked{background:#B00020}.unlocked{background:#2E7D32}.lockdown{background:#FFA000;color:#000}"
                "table{width:100%;border-collapse:collapse;margin-top:12px}th,td{padding:8px;border-bottom:1px solid #243447;text-align:left;font-size:14px}"
                "button{padding:10px 14px;margin:6px;border:none;border-radius:6px;cursor:pointer;color:#fff} .open{background:#2E7D32} .close{background:#B00020} .panic{background:#FFA000;color:#000}"
                "input{padding:8px;border-radius:6px;border:none;margin-left:6px}"
                "</style></head><body><div class='card'>"
                "<h2>RFID Door Dashboard</h2>"
                "<div id='status' class='status locked'>Loading...</div>"
                "<div><b>Last:</b> <span id='last'>-</span></div>"
                "<div style='margin-top:8px'>"
                "<button class='open' onclick=\"fetch('/unlock')\">Unlock</button>"
                "<button class='close' onclick=\"fetch('/lock')\">Lock</button>"
                "<button class='panic' onclick=\"fetch('/lockdown')\">Emergency Lockdown</button>"
                "</div>"

                "<div style='margin-top:14px'><b>Master Unlock (password)</b><br>"
                "<input id='pw' type='password' placeholder='Password'><button onclick=\"masterUnlock()\">Unlock</button>"
                "</div>"

                "<div style='margin-top:14px'><button onclick=\"downloadCSV()\">Download CSV</button></div>"

                "<h3 style='margin-top:18px'>Access History</h3>"
                "<div id='history'>Loading...</div>"

                "<script>"
                "function update(){fetch('/data').then(r=>r.json()).then(d=>{"
                "document.getElementById('status').innerText=d.status;"
                "document.getElementById('last').innerText=d.last;"
                "let sclass = 'locked'; if(d.class=='unlocked') sclass='unlocked'; else if(d.class=='lockdown') sclass='lockdown';"
                "document.getElementById('status').className='status '+sclass;"
                "});"
                "fetch('/logjson').then(r=>r.json()).then(arr=>{"
                "let html='<table><tr><th>Name</th><th>Date & Time</th></tr>';"
                "for(let i=arr.length-1;i>=0;i--){ html+='<tr><td>'+arr[i].name+'</td><td>'+arr[i].time+'</td></tr>'; }"
                "html+='</table>'; document.getElementById('history').innerHTML=html;"
                "});"
                "}"
                "setInterval(update,1000); update();"

                "function masterUnlock(){ let pw=document.getElementById('pw').value; fetch('/pwunlock?pw='+encodeURIComponent(pw)).then(r=>r.text()).then(t=>{ alert(t); document.getElementById('pw').value=''; }); }"
                "function downloadCSV(){ window.location.href='/download'; }"
                "</script>"

                "</div></body></html>";
  return html;
}


void handleRoot() { server.send(200, "text/html", htmlPage()); }

void handleData() {
  String cssClass = "locked";
  if (emergencyLock) cssClass = "lockdown";
  else if (lockStatus.startsWith("Unlocked")) cssClass = "unlocked";

  String json = "{";
  json += "\"status\":\"" + lockStatus + "\",";
  json += "\"last\":\"" + lastPerson + "\",";
  json += "\"class\":\"" + cssClass + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleUnlock() {
  if (!emergencyLock) unlockDoor("Manual Unlock");
  else Serial.println("Unlock blocked: lockdown active");
  server.send(200, "text/plain", "OK");
}

void handleLock() {
  doorServo.write(0);
  lockStatus = "Locked";
  server.send(200, "text/plain", "OK");
}

void handleLockdown() {
  emergencyLock = true;
  lockStatus = "🚨 EMERGENCY LOCKDOWN";
  server.send(200, "text/plain", "OK");
}


void handlePasswordUnlock() {
  String pw = server.arg("pw");
  if (pw == masterPassword) {
    emergencyLock = false;
    unlockDoor("Master Password");
    server.send(200, "text/plain", "Unlocked");
  } else {
    server.send(200, "text/plain", "Wrong Password!");
  }
}


void handleLogJson() {
  String json = readLogAsJSON();
  server.send(200, "application/json", json);
}


void handleDownload() {
  if (!LittleFS.begin()) { server.send(500, "text/plain", "FS init failed"); return; }
  if (!LittleFS.exists(LOG_PATH)) { LittleFS.end(); server.send(404, "text/plain", "No log"); return; }
  File f = LittleFS.open(LOG_PATH, "r");
  server.streamFile(f, "text/csv");
  f.close();
  LittleFS.end();
}


void setup() {
  Serial.begin(115200);
  SPI.begin();
  mfrc522.PCD_Init();

  doorServo.attach(SERVO_PIN);
  doorServo.write(0);

 
  if (LittleFS.begin()) {
    if (!LittleFS.exists(LOG_PATH)) {
      File f = LittleFS.open(LOG_PATH, "w");
      if (f) {
        f.println("Name,Timestamp");
        f.close();
      }
    }
    LittleFS.end();
  } else {
    Serial.println("LittleFS init failed");
  }

  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("WiFi connect failed");
  }

  
  configTime(19800, 0, ntpServer); 
  Serial.println("NTP configured (may take few seconds to sync)");

 
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/unlock", handleUnlock);
  server.on("/lock", handleLock);
  server.on("/lockdown", handleLockdown);
  server.on("/pwunlock", handlePasswordUnlock);
  server.on("/logjson", handleLogJson);
  server.on("/download", handleDownload);

  server.begin();
  Serial.println("HTTP server started");
}


void loop() {
  server.handleClient();

  if (emergencyLock) return; // disable RFID during lockdown

  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  String tagUID = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) tagUID += "0";
    tagUID += String(mfrc522.uid.uidByte[i], HEX);
  }
  tagUID.toUpperCase();

  Serial.println("Scanned UID: " + tagUID);
  String name = checkAuthorization(tagUID);
  if (name != "") {
    unlockDoor(name);
  } else {
    Serial.println("Access Denied!");
  }
  mfrc522.PICC_HaltA();
}