// ============================================================
//   SMART SOCKET PRO - FIXED VERSION
//   Fix: WiFi AP+STA, overload logic, tone() ESP32,
//        Blynk rate limit, PZEM stale data,
//        BUTTON_2, Blynk relay control
// ============================================================

// 1. PINDAHKAN KODE DARI BLYNK DASHBOARD (DEVICE INFO) KE SINI
#define BLYNK_TEMPLATE_ID   "MASUKKAN_ID_DISINI"
#define BLYNK_TEMPLATE_NAME "Smart Socket Pro"
#define BLYNK_AUTH_TOKEN    "MASUKKAN_TOKEN_DISINI"

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <LittleFS.h>
#include <PZEM004Tv30.h>
#include <BlynkSimpleEsp32.h>

// --- KONFIGURASI PIN ---
#define RELAY_1   18
#define RELAY_2   19
#define BUTTON_1  25
#define BUTTON_2  26
#define BUZZER    21

// --- KONFIGURASI BUZZER (ESP32 LEDC) ---
#define BUZZER_CHANNEL  0
#define BUZZER_RES      8       // resolusi 8-bit

// --- BLYNK VIRTUAL PIN ---
#define V_VOLT    V1
#define V_WATT    V2
#define V_BIAYA   V3
#define V_RELAY1  V4   // kontrol relay 1 dari app Blynk
#define V_RELAY2  V5   // kontrol relay 2 dari app Blynk
#define V_AMPERE  V6
#define V_KWH     V7

// --- OBJEK UTAMA ---
PZEM004Tv30    pzem(Serial2, 16, 17);
AsyncWebServer server(80);

// --- VARIABEL KONFIGURASI ---
float limit1 = 1300.0;
float limit2 = 900.0;
float tarif  = 1445.0;

// --- VARIABEL SENSOR ---
float volt    = 0;
float ampere  = 0;
float watt    = 0;
float kwh     = 0;
float biaya   = 0;

// --- STATUS RELAY ---
bool relay1State = true;   // true = ON
bool relay2State = true;

// --- TIMER ---
unsigned long lastSensorRead = 0;
unsigned long lastBlynkSend  = 0;
const unsigned long SENSOR_INTERVAL = 1000;   // baca sensor tiap 1 detik
const unsigned long BLYNK_INTERVAL  = 2000;   // kirim ke Blynk tiap 2 detik

// --- DEBOUNCE TOMBOL ---
unsigned long lastBtn1Press = 0;
unsigned long lastBtn2Press = 0;
const unsigned long DEBOUNCE_MS = 300;

// ============================================================
//   HTML DASHBOARD
// ============================================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>SmartSocket Pro</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link href="https://fonts.googleapis.com/css2?family=Orbitron:wght@500&family=Inter:wght@400;700&display=swap" rel="stylesheet">
  <style>
    :root{--accent:#00f2ff;--bg:#0b0e14;--card:rgba(255,255,255,0.03);--border:rgba(255,255,255,0.08);--gold:#ffdd57;--amp:#a29bfe;}
    *{box-sizing:border-box;}
    body{background:var(--bg);color:#fff;font-family:'Inter',sans-serif;margin:0;padding:20px;display:flex;flex-direction:column;align-items:center;}
    .header{text-align:center;margin-bottom:30px;border-bottom:2px solid var(--card);padding-bottom:15px;width:100%;max-width:1200px;}
    .header h1{font-family:'Orbitron';color:var(--accent);font-size:1.4rem;letter-spacing:4px;margin:0;}
    .dashboard{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:15px;width:100%;max-width:1300px;}
    .card{background:var(--card);border:1px solid var(--border);border-radius:20px;padding:15px;text-align:center;transition:.3s ease;}
    .card:hover{border-color:var(--accent);transform:translateY(-3px);}
    .gauge-c{position:relative;width:140px;height:75px;margin:0 auto 10px;overflow:hidden;}
    .gauge-bg{width:140px;height:140px;border-radius:50%;border:10px solid #161b22;border-bottom-color:transparent;border-left-color:#ff3e3e;border-top-color:var(--accent);transform:rotate(-45deg);}
    .needle{position:absolute;bottom:0;left:50%;width:2px;height:55px;background:#fff;transform-origin:bottom center;transform:translateX(-50%) rotate(-90deg);transition:1.5s cubic-bezier(.4,0,.2,1);filter:drop-shadow(0 0 3px var(--accent));}
    .n-cap{position:absolute;bottom:-4px;left:50%;transform:translateX(-50%);width:10px;height:10px;background:#fff;border-radius:50%;}
    .lbl{color:#666;font-size:.6rem;font-weight:700;text-transform:uppercase;margin-bottom:10px;display:block;}
    .val{font-family:'Orbitron';font-size:1.2rem;color:#fff;display:block;}
    .unt{color:#444;font-size:.65rem;font-weight:700;margin-top:3px;display:block;}
    .relay-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;width:100%;max-width:600px;margin-top:20px;}
    .relay-card{background:var(--card);border:1px solid var(--border);border-radius:16px;padding:16px;text-align:center;}
    .relay-label{font-size:.7rem;color:#888;font-weight:700;text-transform:uppercase;margin-bottom:8px;}
    .relay-btn{width:100%;padding:12px;border-radius:10px;border:none;font-weight:800;font-size:.85rem;cursor:pointer;transition:.2s;}
    .btn-on {background:#00f2ff;color:#000;}
    .btn-off{background:#ff3e3e;color:#fff;}
    .conf{width:100%;max-width:900px;margin-top:20px;background:var(--card);border-radius:20px;padding:25px;border:1px solid var(--border);}
    input{width:100%;background:#000;border:1px solid #222;border-radius:10px;color:var(--accent);padding:10px;font-family:'Orbitron';font-size:.8rem;outline:none;margin-bottom:10px;}
    .btn{width:100%;background:var(--accent);color:#000;border:none;padding:14px;border-radius:12px;font-weight:800;cursor:pointer;text-transform:uppercase;}
    .status-dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:5px;}
    .dot-on{background:#00ff88;box-shadow:0 0 6px #00ff88;}
    .dot-off{background:#ff3e3e;box-shadow:0 0 6px #ff3e3e;}
    .overload-banner{display:none;background:#ff3e3e22;border:1px solid #ff3e3e;border-radius:12px;padding:12px;text-align:center;color:#ff3e3e;font-weight:700;margin-bottom:15px;width:100%;max-width:1300px;}
  </style>
</head>
<body>
  <div class="header">
    <h1>SMART MONITOR <span style="font-weight:100">ISX</span></h1>
    <p>Data Elektrikal & Biaya Riil — Aziz Galih</p>
  </div>

  <div class="overload-banner" id="olBanner">⚠️ OVERLOAD DETECTED — RELAY DIMATIKAN OTOMATIS</div>

  <!-- GAUGE CARDS -->
  <div class="dashboard">
    <div class="card">
      <span class="lbl">Voltage</span>
      <div class="gauge-c"><div class="gauge-bg"></div><div class="needle" id="vn"></div><div class="n-cap"></div></div>
      <span class="val" id="v">0</span><span class="unt">VOLT</span>
    </div>
    <div class="card" style="border-color:var(--amp)">
      <span class="lbl" style="color:var(--amp)">Current</span>
      <div class="gauge-c"><div class="gauge-bg" style="border-top-color:var(--amp)"></div><div class="needle" id="an"></div><div class="n-cap"></div></div>
      <span class="val" id="a" style="color:var(--amp)">0</span><span class="unt">AMPERE</span>
    </div>
    <div class="card">
      <span class="lbl">Power</span>
      <div class="gauge-c"><div class="gauge-bg"></div><div class="needle" id="wn"></div><div class="n-cap"></div></div>
      <span class="val" id="w">0</span><span class="unt">WATTS</span>
    </div>
    <div class="card">
      <span class="lbl">Live Price</span>
      <div class="gauge-c"><div class="gauge-bg"></div><div class="needle" id="ln"></div><div class="n-cap"></div></div>
      <span class="val">Rp <span id="lv">0</span></span><span class="unt">PER HOUR</span>
    </div>
    <div class="card">
      <span class="lbl">Energy Usage</span>
      <div class="gauge-c"><div class="gauge-bg"></div><div class="needle" id="kn"></div><div class="n-cap"></div></div>
      <span class="val" id="k">0</span><span class="unt">KWH</span>
    </div>
    <div class="card" style="border-color:var(--gold)">
      <span class="lbl" style="color:var(--gold)">Total Cost</span>
      <div class="gauge-c"><div class="gauge-bg" style="border-top-color:var(--gold)"></div><div class="needle" id="cn"></div><div class="n-cap"></div></div>
      <span class="val" style="color:var(--gold)">Rp <span id="c">0</span></span><span class="unt">ACCUMULATED</span>
    </div>
  </div>

  <!-- RELAY CONTROL -->
  <div class="relay-grid">
    <div class="relay-card">
      <div class="relay-label"><span class="status-dot" id="dot1"></span>Socket 1</div>
      <button class="relay-btn" id="btn1" onclick="toggleRelay(1)">-</button>
    </div>
    <div class="relay-card">
      <div class="relay-label"><span class="status-dot" id="dot2"></span>Socket 2</div>
      <button class="relay-btn" id="btn2" onclick="toggleRelay(2)">-</button>
    </div>
  </div>

  <!-- CONFIG -->
  <div class="conf">
    <form action="/update">
      <div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px;">
        <div>Limit S1 (W): <input type="number" name="l1" value="%L1%"></div>
        <div>Limit S2 (W): <input type="number" name="l2" value="%L2%"></div>
        <div>Tarif (Rp/kWh): <input type="number" name="tr" value="%TR%"></div>
      </div>
      <button class="btn" type="submit">Update Configuration</button>
    </form>
    <button onclick="if(confirm('Reset akumulasi energi & biaya?')) window.location.href='/reset'"
      style="width:100%;background:none;color:#ff3e3e;border:1px solid #ff3e3e;padding:10px;border-radius:12px;margin-top:10px;cursor:pointer;font-weight:bold;">
      RESET TOTAL BULANAN
    </button>
  </div>

  <script>
    var limit1 = %L1%, tarif = %TR%;

    function mv(id, v, m) {
      var p = Math.min((v / m) * 100, 100);
      document.getElementById(id).style.transform = 'translateX(-50%) rotate(' + ((p * 1.8) - 90) + 'deg)';
    }

    function updateRelayUI(r1, r2) {
      var b1 = document.getElementById('btn1'), d1 = document.getElementById('dot1');
      var b2 = document.getElementById('btn2'), d2 = document.getElementById('dot2');
      if (r1) { b1.textContent='SOCKET 1 ON'; b1.className='relay-btn btn-on'; d1.className='status-dot dot-on'; }
      else     { b1.textContent='SOCKET 1 OFF'; b1.className='relay-btn btn-off'; d1.className='status-dot dot-off'; }
      if (r2) { b2.textContent='SOCKET 2 ON'; b2.className='relay-btn btn-on'; d2.className='status-dot dot-on'; }
      else     { b2.textContent='SOCKET 2 OFF'; b2.className='relay-btn btn-off'; d2.className='status-dot dot-off'; }
    }

    function toggleRelay(n) {
      fetch('/relay?n=' + n).then(r => r.json()).then(d => updateRelayUI(d.r1, d.r2));
    }

    setInterval(function(){
      fetch('/get_data').then(r => r.json()).then(d => {
        document.getElementById('v').innerText = d.v;
        document.getElementById('a').innerText = d.a;
        document.getElementById('w').innerText = d.w;
        document.getElementById('k').innerText = d.k;
        document.getElementById('c').innerText = d.c;
        var lv = (parseFloat(d.w) / 1000) * tarif;
        document.getElementById('lv').innerText = lv.toLocaleString('id-ID');
        mv('vn', parseFloat(d.v), 250);
        mv('an', parseFloat(d.a), 10);
        mv('wn', parseFloat(d.w), limit1);
        mv('ln', lv, (limit1/1000)*tarif);
        mv('kn', parseFloat(d.k), 100);
        mv('cn', parseFloat(d.c), 300000);
        updateRelayUI(d.r1, d.r2);
        document.getElementById('olBanner').style.display = d.ol ? 'block' : 'none';
      });
    }, 2000);
  </script>
</body></html>
)rawliteral";

// ============================================================
//   FUNGSI BUZZER (ESP32 LEDC, bukan tone())
// ============================================================
void buzzerBeep(int freq, int durationMs) {
  ledcWriteTone(BUZZER_CHANNEL, freq);
  delay(durationMs);
  ledcWriteTone(BUZZER_CHANNEL, 0);
}

// ============================================================
//   FUNGSI SET RELAY
// ============================================================
void setRelay1(bool on) {
  relay1State = on;
  digitalWrite(RELAY_1, on ? HIGH : LOW);
}

void setRelay2(bool on) {
  relay2State = on;
  digitalWrite(RELAY_2, on ? HIGH : LOW);
}

// ============================================================
//   SAVE / LOAD CONFIG
// ============================================================
void saveConfig() {
  File f = LittleFS.open("/config.bin", "w");
  if (f) {
    f.write((byte*)&limit1, 4);
    f.write((byte*)&limit2, 4);
    f.write((byte*)&tarif,  4);
    f.close();
  }
}

void loadConfig() {
  if (LittleFS.exists("/config.bin")) {
    File f = LittleFS.open("/config.bin", "r");
    if (f) {
      f.read((byte*)&limit1, 4);
      f.read((byte*)&limit2, 4);
      f.read((byte*)&tarif,  4);
      f.close();
    }
  }
}

// ============================================================
//   BLYNK — TERIMA KONTROL RELAY DARI APP
// ============================================================
BLYNK_WRITE(V_RELAY1) {
  int val = param.asInt();
  setRelay1(val == 1);
}

BLYNK_WRITE(V_RELAY2) {
  int val = param.asInt();
  setRelay2(val == 1);
}

// ============================================================
//   SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  // LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("[LittleFS] Mount GAGAL");
  }
  loadConfig();

  // PIN
  pinMode(RELAY_1,  OUTPUT);
  pinMode(RELAY_2,  OUTPUT);
  pinMode(BUTTON_1, INPUT_PULLUP);
  pinMode(BUTTON_2, INPUT_PULLUP);

  // Buzzer via LEDC
  ledcSetup(BUZZER_CHANNEL, 2000, BUZZER_RES);
  ledcAttachPin(BUZZER, BUZZER_CHANNEL);

  // Relay default ON
  setRelay1(true);
  setRelay2(true);

  // ── FIX #1: WiFi mode AP+STA agar dashboard lokal & Blynk bisa jalan bersamaan ──
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("Smart-Socket-Isx", "12345678");
  Serial.print("[AP] IP: "); Serial.println(WiFi.softAPIP());

  // ── Web Server Routes ──

  // Halaman utama
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String h = index_html;
    h.replace("%L1%", String(limit1, 0));
    h.replace("%L2%", String(limit2, 0));
    h.replace("%TR%", String(tarif,  0));
    request->send(200, "text/html", h);
  });

  // Update konfigurasi
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("l1")) limit1 = request->getParam("l1")->value().toFloat();
    if (request->hasParam("l2")) limit2 = request->getParam("l2")->value().toFloat();
    if (request->hasParam("tr")) tarif  = request->getParam("tr")->value().toFloat();
    saveConfig();
    request->redirect("/");
  });

  // Data JSON sensor
  server.on("/get_data", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool overload = (!relay1State && watt > limit1) || (!relay2State && watt > limit2);
    String json = "{";
    json += "\"v\":\"" + String(volt,   1) + "\",";
    json += "\"a\":\"" + String(ampere, 2) + "\",";
    json += "\"w\":\"" + String(watt,   1) + "\",";
    json += "\"k\":\"" + String(kwh,    3) + "\",";
    json += "\"c\":\"" + String(biaya,  0) + "\",";
    json += "\"r1\":"  + String(relay1State ? "true" : "false") + ",";
    json += "\"r2\":"  + String(relay2State ? "true" : "false") + ",";
    json += "\"ol\":"  + String(overload    ? "true" : "false");
    json += "}";
    request->send(200, "application/json", json);
  });

  // Toggle relay dari dashboard web
  server.on("/relay", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("n")) {
      int n = request->getParam("n")->value().toInt();
      if (n == 1) setRelay1(!relay1State);
      if (n == 2) setRelay2(!relay2State);
    }
    String json = "{\"r1\":" + String(relay1State ? "true" : "false") +
                  ",\"r2\":" + String(relay2State ? "true" : "false") + "}";
    request->send(200, "application/json", json);
  });

  // Reset akumulasi energi
  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request) {
    pzem.resetEnergy();
    request->redirect("/");
  });

  server.begin();
  Serial.println("[Server] Started");

  // Blynk — gunakan WiFi rumah agar online
  Blynk.begin(BLYNK_AUTH_TOKEN, "SSID_WIFI", "PASS_WIFI");
}

// ============================================================
//   LOOP
// ============================================================
void loop() {
  Blynk.run();

  unsigned long now = millis();

  // ── FIX #5: Baca sensor PZEM setiap 1 detik di loop() ──
  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = now;

    float v = pzem.voltage();
    float a = pzem.current();
    float w = pzem.power();
    float k = pzem.energy();

    volt   = isnan(v) ? 0 : v;
    ampere = isnan(a) ? 0 : a;
    watt   = isnan(w) ? 0 : w;
    kwh    = isnan(k) ? 0 : k;
    biaya  = kwh * tarif;

    // ── FIX #2: Overload protection per relay ──
    if (watt > limit1 && relay1State) {
      setRelay1(false);
      Serial.println("[OVL] Relay 1 OFF — overload!");
      buzzerBeep(1500, 300);
    }
    if (watt > limit2 && relay2State) {
      setRelay2(false);
      Serial.println("[OVL] Relay 2 OFF — overload!");
      buzzerBeep(1500, 300);
    }
  }

  // ── FIX #4: Kirim ke Blynk setiap 2 detik (bukan setiap loop) ──
  if (now - lastBlynkSend >= BLYNK_INTERVAL) {
    lastBlynkSend = now;
    Blynk.virtualWrite(V_VOLT,   volt);
    Blynk.virtualWrite(V_AMPERE, ampere);
    Blynk.virtualWrite(V_WATT,   watt);
    Blynk.virtualWrite(V_KWH,    kwh);
    Blynk.virtualWrite(V_BIAYA,  biaya);
    // Kirim juga status relay ke Blynk app (untuk sinkronisasi tombol)
    Blynk.virtualWrite(V_RELAY1, relay1State ? 1 : 0);
    Blynk.virtualWrite(V_RELAY2, relay2State ? 1 : 0);
  }

  // ── FIX #6: BUTTON_1 toggle Relay 1 ──
  if (!digitalRead(BUTTON_1) && (now - lastBtn1Press > DEBOUNCE_MS)) {
    lastBtn1Press = now;
    setRelay1(!relay1State);
    Serial.println("[BTN1] Relay 1 toggled");
  }

  // ── FIX #6: BUTTON_2 toggle Relay 2 ──
  if (!digitalRead(BUTTON_2) && (now - lastBtn2Press > DEBOUNCE_MS)) {
    lastBtn2Press = now;
    setRelay2(!relay2State);
    Serial.println("[BTN2] Relay 2 toggled");
  }
}
