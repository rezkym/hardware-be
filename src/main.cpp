// =========================
// ======= INCLUDES =======
// =========================
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <SPI.h>
#include <MFRC522.h>
#include <vector>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

// =========================
// ======= KONFIGURASI =======
// =========================

// Definisi Pin
#define BUZZER_PIN 13
#define LED_GREEN 4
#define LED_RED 2
#define LED_YELLOW 15

// Konfigurasi OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

// RFID Pin Configuration
#define RST_PIN 33
#define SS_PIN 5

// Buffer Configuration
#define MAX_BUFFER_SIZE 10 // Maksimum data yang bisa disimpan di buffer
#define READ_TIMEOUT 25    // Timeout untuk pembacaan dalam ms

// Inisialisasi objek OLED
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Enumerasi untuk status error
enum ErrorType
{
    NO_ERROR,
    WIFI_CONNECTION_FAILED,
    GOOGLE_SCRIPT_CONNECTION_FAILED,
    RFID_READ_ERROR,
    DATA_SEND_ERROR,
    FIRMWARE_UPDATE_ERROR
};

// Variabel global
ErrorType currentError = NO_ERROR;
bool isSending = false;

// =========================
// ======= KONFIGURASI WIFI =======
// =========================
const char *AP_SSID = "SDS Telkom Batam";
const char *AP_PASSWORD = "12345678";
const byte DNS_PORT = 53;
const unsigned long WIFI_TIMEOUT = 20000; // 20 detik timeout
const int WIFI_RETRY_DELAY = 500;
const int MAX_CONNECTION_ATTEMPTS = 3;

String previousSSID = "";
String previousPassword = "";

unsigned long lastWiFiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 30000; // 30 detik

// EEPROM Addresses
const int EEPROM_SSID_ADDR = 0;
const int EEPROM_PASS_ADDR = 50;

// Objek Global
DNSServer dnsServer;
WebServer server(80);
IPAddress apIP(192, 168, 4, 1);

// Status Variables
bool isAPMode = false;
String lastError = "";

// Struktur untuk kredensial WiFi
struct WiFiCredentials
{
    String ssid;
    String password;
} wifiCred;

// Oled Config Tracking
unsigned long lastOLEDUpdate = 0;
const unsigned long OLED_UPDATE_INTERVAL = 1000; // Update setiap 1 detik

// =========================
// ======= KONFIGURASI RFID =======
// =========================

// RFID Block Configuration
const byte blocks[] = {4, 5, 6}; // Blok yang akan dibaca: NISN, NIP, Nama
const byte total_blocks = sizeof(blocks) / sizeof(blocks[0]);
byte readBlockData[18];
byte bufferLen = 18;

// Inisialisasi objek MFRC522
MFRC522 mfrc522(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;
MFRC522::StatusCode status;

// Struktur data RFID
struct RFIDData {
    String uid;
    String blockData[3]; // [NISN, NIP, Nama]
    unsigned long timestamp;
};

// Ring buffer untuk penyimpanan data RFID
struct RFIDBuffer
{
    RFIDData data[MAX_BUFFER_SIZE];
    int head;
    int tail;
    int count;

    // Constructor untuk inisialisasi
    RFIDBuffer() : head(0), tail(0), count(0) {}
};

// Deklarasi variabel global
RFIDBuffer rfidBuffer; // Inisialisasi buffer sebagai variabel global

// Status tracking untuk debouncing dan feedback
unsigned long lastSuccessfulRead = 0;
const unsigned long READ_COOLDOWN = 500; // 500ms cooldown antara pembacaan
bool isProcessing = false;

// =========================
// ======= GOOGLE APPS CONFIGURATION =======
// =========================

// Google Apps Script Configuration
const char *GScriptId = "AKfycbzwIL3gKKJb7DISmJlR_XRoDQxpt4l4uWp2zztHX1mvgIX4fIlnawIAws6Lxz6UkV0Hxw";
const char *GScriptHost = "script.google.com";
const int GScriptHttpsPort = 443;
String GScriptURL = String("/macros/s/") + GScriptId + "/exec";

// Connection Configuration
const int HTTP_TIMEOUT = 10000; // 10 detik timeout
const int MAX_RETRIES = 3;      // Maksimum percobaan koneksi
const int RETRY_DELAY = 1000;   // Delay antar percobaan (1 detik)

// Status Variables
bool isGScriptConnected = false;
unsigned long lastConnectionCheck = 0;
const unsigned long CONNECTION_CHECK_INTERVAL = 300000; // Check setiap 5 menit

const int MIN_BATCH_SIZE = 10;              // Minimal data sebelum dikirim
const unsigned long SEND_TIMEOUT = 60000;    // Timeout 1 menit
unsigned long lastDataTime = 0;              // Waktu data terakhir masuk

int gScriptConnectionFailureCount = 0;
const int MAX_GSCRIPT_CONNECTION_FAILURES = 5;

// =========================
// ======= OTA CONFIGURATION =======
// =========================

// OTA URLs
const char* VERSION_URL = "http://tabrizah-iot.my.id/firmware_version.txt";
const char* BINARY_URL = "http://tabrizah-iot.my.id/Attendance_SD_Telkom_Firmware.bin";

// OTA Authentication
const char* OTA_USERNAME = "admin";
const char* OTA_PASSWORD = "admin123";


// Current firmware version
const char* CURRENT_VERSION = "2.0";

// OTA Status tracking
bool isOTAInProgress = false;
String latestVersion = "";
bool updateAvailable = false;

bool versionCheckFailed = false;


// =========================
// ======= OTA DECLARATIONS =======
// =========================

// Function declarations for OTA
void checkFirmwareUpdate();
void performUpdate(Stream &updateSource, size_t updateSize);
void updateFirmware();
void handleOTAUpdate();
void handleCheckUpdate();

// =========================
// ======= GOOGLE APPS DECLARATIONS =======
// =========================

// Basic Connection Functions
bool initGoogleApps();             // Inisialisasi koneksi Google Apps
bool testGoogleScriptConnection(); // Test koneksi ke Google Script
void setupGoogleApps();            // Setup awal Google Apps
void handleGoogleApps();           // Handler utama untuk loop()
String getRedirectUrl(const String &response);

// Data Management Functions
bool sendBatchToGScript(const String &batchData); // Kirim batch data ke Google Script
bool processPendingData();                        // Proses data yang pending di buffer

// Connection Management Functions
void checkGScriptConnection(); // Cek status koneksi secara periodik

// Helper Functions
String getRedirectUrl(const String &response);                 // Ekstrak URL redirect dari response
bool handleHttpResponse(int httpCode, const String &response); // Handle HTTP response codes

// =========================
// ======= RFID DECLARATIONS =======
// =========================

// Basic RFID Functions
void initRFID();   // Inisialisasi modul RFID
void handleRFID(); // Handler utama RFID untuk loop()

// RFID Reading Functions
void processRFIDCard();               // Memproses kartu yang terdeteksi
String readRFIDBlock(byte blockAddr); // Membaca data dari blok spesifik

// Buffer Management Functions
bool addToBuffer(const RFIDData &data); // Menambahkan data ke buffer
String prepareDataForBatch();           // Menyiapkan data untuk pengiriman batch
String cleanString(const String &str);

// Helper Functions
bool isBufferFull();  // Mengecek apakah buffer penuh
bool isBufferEmpty(); // Mengecek apakah buffer kosong
void clearBuffer();   // Membersihkan buffer

// Status and Debug Functions
void printBufferStatus(); // Menampilkan status buffer ke Serial
void updateRFIDStatus();  // Update status RFID ke OLED/LED

// Error Handling Functions
bool validateRFIDData(const RFIDData &data); // Validasi data RFID
void handleRFIDError(const String &error);   // Penanganan error RFID

void resetRFIDModule();

// Constants and Configuration
constexpr byte RFID_BLOCKS[] = {4, 5, 6};
constexpr byte TOTAL_BLOCKS = sizeof(RFID_BLOCKS) / sizeof(RFID_BLOCKS[0]);

// External Variables Declaration
extern MFRC522 mfrc522;
extern MFRC522::MIFARE_Key key;
extern RFIDBuffer rfidBuffer;
extern bool isProcessing;
extern unsigned long lastSuccessfulRead;

// =========================
// ======= DEKLARASI FUNGSI =======
// =========================

// Manajemen OLED
void initOLED();
void clearOLED();
void updateOLEDStatus(const String &primaryText, const String &secondaryText = "", bool showDefaultDisplay = false);
void showDefaultOLEDDisplay();
void showErrorOLED(const String &errorMsg);

// Setup dan Inisialisasi Wifi
void initWiFi();
void setupAP();
void setupWebServer();

// Manajemen Mode Wifi
bool connectToWiFi(const String &ssid, const String &password);
void switchToAPMode();
bool testWiFiConnection();

// Handle connection wifi
void handleConnectionFailure();
void showConnectionProgress(int attempt, int maxAttempts);
void handleForget();
void checkAndUpdateWiFiStatus();

// Manajemen EEPROM
String readEEPROM(int startAddr, int maxLength);
void writeEEPROM(int startAddr, const String &data);
void loadWiFiCredentials();
void saveWiFiCredentials(const String &ssid, const String &password);
void resetWiFiCredentials();

// Handler Web Server
void handleRoot();
void handleWiFiScan();
void handleConnect();
void handleStatus();
void handleReset();

// Manajemen LED
void initLEDs();
void updateLEDStatus(ErrorType error);
void blinkLED(uint8_t pin, uint8_t times, uint16_t delayMs);
void setAllLEDs(bool state);

// Manajemen Buzzer
void initBuzzer();
void beep(uint8_t times, uint16_t durationMs);
void errorBeep();
void successBeep();

// =========================
// ======= OTA FUNCTIONS =======
// =========================

void checkFirmwareUpdate() {
    if (WiFi.status() != WL_CONNECTED) return;

    updateOLEDStatus("Checking Update", "Please wait...");
    digitalWrite(LED_YELLOW, HIGH);

    HTTPClient http;
    versionCheckFailed = false;

    Serial.println("Checking firmware update...");
    Serial.println("URL: " + String(VERSION_URL));

    if (http.begin(VERSION_URL)) {
        int httpCode = http.GET();
        
        Serial.println("HTTP Response code: " + String(httpCode));
        
        if (httpCode == HTTP_CODE_OK) {
            String newVersion = http.getString();
            newVersion.trim();
            latestVersion = newVersion;
            
            Serial.println("Latest version: " + latestVersion);
            Serial.println("Current version: " + String(CURRENT_VERSION));
            
            // Compare versions
            if (newVersion != String(CURRENT_VERSION)) {
                updateAvailable = true;
                updateOLEDStatus("Update Available", "Version: " + newVersion);
                blinkLED(LED_YELLOW, 3, 200);
                beep(2, 100);
            } else {
                updateAvailable = false;
                updateOLEDStatus("Firmware Updated", "Version: " + String(CURRENT_VERSION));
            }
        } else {
            versionCheckFailed = true;
            latestVersion = "";
            Serial.println("Failed to get version. HTTP Code: " + String(httpCode));
            updateOLEDStatus("Update Check Failed", "Error: " + String(httpCode));
        }
        http.end();
    } else {
        versionCheckFailed = true;
        latestVersion = "";
        Serial.println("Failed to connect to update server");
        updateOLEDStatus("Connection Failed", "Can't reach server");
    }
    
    digitalWrite(LED_YELLOW, LOW);
}

void performUpdate(Stream &updateSource, size_t updateSize) {
    if (Update.begin(updateSize)) {
        size_t written = Update.writeStream(updateSource);
        if (written == updateSize) {
            Serial.println("Written : " + String(written) + " successfully");
        } else {
            Serial.println("Written only : " + String(written) + "/" + String(updateSize) + ". Retry?");
        }
        if (Update.end()) {
            Serial.println("OTA done!");
            if (Update.isFinished()) {
                Serial.println("Update successfully completed. Rebooting.");
                ESP.restart();
            } else {
                Serial.println("Update not finished? Something went wrong!");
            }
        } else {
            Serial.println("Error Occurred. Error #: " + String(Update.getError()));
        }
    } else {
        Serial.println("Not enough space to begin OTA");
    }
}

void updateFirmware() {
    isOTAInProgress = true;
    updateOLEDStatus("OTA Update", "Downloading...");
    digitalWrite(LED_YELLOW, HIGH);

    HTTPClient http;
    
    Serial.println("Starting firmware update...");
    Serial.println("URL: " + String(BINARY_URL));
    
    if (http.begin(BINARY_URL)) {
        int httpCode = http.GET();
        
        Serial.println("HTTP Response code: " + String(httpCode));
        
        if (httpCode <= 0) {
            updateOLEDStatus("OTA Failed", "Download error: " + String(httpCode));
            Serial.println("Download failed. Code: " + String(httpCode));
            isOTAInProgress = false;
            digitalWrite(LED_YELLOW, LOW);
            return;
        }

        if (httpCode != HTTP_CODE_OK) {
            updateOLEDStatus("OTA Failed", "File not found");
            Serial.println("File not found. Code: " + String(httpCode));
            isOTAInProgress = false;
            digitalWrite(LED_YELLOW, LOW);
            return;
        }

        // Get file size
        int contentLength = http.getSize();
        
        if (contentLength <= 0) {
            updateOLEDStatus("OTA Failed", "Invalid size");
            Serial.println("Invalid content length: " + String(contentLength));
            isOTAInProgress = false;
            digitalWrite(LED_YELLOW, LOW);
            return;
        }

        Serial.println("Content Length: " + String(contentLength));
        
        // Start update
        if (Update.begin(contentLength)) {
            Serial.println("Starting OTA process...");
            
            // Create buffer for reading
            uint8_t buff[1024] = { 0 };
            WiFiClient * stream = http.getStreamPtr();
            size_t written = 0;

            // Read all data
            while (http.connected() && (written < contentLength)) {
                // Get available data size
                size_t available = stream->available();
                
                if (available) {
                    // Read up to 1024 bytes
                    size_t c = stream->readBytes(buff, ((available > sizeof(buff)) ? sizeof(buff) : available));
                    
                    // Write it to Update
                    if (Update.write(buff, c) != c) {
                        Serial.println("Written only: " + String(written) + "/" + String(contentLength));
                        break;
                    }
                    
                    written += c;
                    
                    // Calculate progress
                    int progress = (written * 100) / contentLength;
                    updateOLEDStatus("Updating", String(progress) + "%");
                }
                
                delay(1); // Give a chance to idle tasks
            }

            if (written == contentLength) {
                Serial.println("Written : " + String(written) + " successfully");
                if (Update.end()) {
                    Serial.println("OTA done!");
                    if (Update.isFinished()) {
                        updateOLEDStatus("Update Success", "Restarting...");
                        Serial.println("Update successfully completed");
                        delay(1000);
                        ESP.restart();
                    }
                }
            }
        }
    }
    
    updateOLEDStatus("Update Failed", "Please try again");
    Serial.println("Update failed");
    http.end();
    isOTAInProgress = false;
    digitalWrite(LED_YELLOW, LOW);
}

// =========================
// ======= OTA HANDLERS =======
// =========================

void handleOTAUpdate() {
    if (!server.authenticate(OTA_USERNAME, OTA_PASSWORD)) {
        return server.requestAuthentication();
    }

    String html = R"(
    <!DOCTYPE html>
    <html>
    <head>
        <meta name='viewport' content='width=device-width, initial-scale=1.0'>
        <title>OTA Update</title>
        <style>
            body { font-family: Arial; margin: 0; padding: 20px; background: #f0f0f0; }
            .container { max-width: 500px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
            .info-box { background: #e3f2fd; padding: 15px; border-radius: 4px; margin: 10px 0; }
            .update-box { background: #f1f8e9; padding: 15px; border-radius: 4px; margin: 10px 0; }
            .error-box { background: #ffebee; padding: 15px; border-radius: 4px; margin: 10px 0; }
            .warning { color: #856404; background: #fff3cd; padding: 15px; border-radius: 4px; margin: 10px 0; }
            .btn { background: #007bff; color: white; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; }
            .btn:hover { background: #0056b3; }
            .btn-danger { background: #dc3545; }
            .btn-danger:hover { background: #c82333; }
            .loading { display: none; text-align: center; padding: 20px; }
            .spinner { border: 4px solid #f3f3f3; border-top: 4px solid #3498db; border-radius: 50%; width: 40px; height: 40px; animation: spin 1s linear infinite; margin: 10px auto; }
            @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
        </style>
    </head>
    <body>
        <div class='container'>
            <h1>Firmware Update</h1>
            
            <div class='info-box'>
                <h3>Current Version</h3>
                <p>Installed: )";
    
    html += CURRENT_VERSION;
    html += R"(</p>
                <p>Latest Available: )";
    
    // Tampilkan status yang sesuai berdasarkan hasil pengecekan
    if (versionCheckFailed) {
        html += "<span style='color: #d32f2f;'>Gagal mengambil versi dari server</span>";
    } else {
        html += latestVersion.isEmpty() ? "Belum dilakukan pengecekan" : latestVersion;
    }
    
    html += R"(</p>
            </div>
            
            <div class=')";
    
    // Pilih class box yang sesuai berdasarkan status
    if (versionCheckFailed) {
        html += "error-box";
    } else {
        html += "update-box";
    }
    
    html += R"('>
                <h3>Update Status</h3>
                <p id='updateStatus'>)";
    
    // Tampilkan pesan status yang sesuai
    if (versionCheckFailed) {
        html += "Tidak dapat memeriksa pembaruan. Silakan coba lagi.";
    } else if (updateAvailable) {
        html += "Pembaruan tersedia!";
    } else if (!latestVersion.isEmpty()) {
        html += "Sistem sudah menggunakan versi terbaru";
    } else {
        html += "Silakan periksa pembaruan";
    }
    
    html += R"(</p>
            </div>

            <div class='warning'>
                <strong>Peringatan:</strong> Jangan matikan perangkat atau putuskan koneksi selama proses pembaruan berlangsung.
            </div>

            <div class='loading' id='loadingSection'>
                <div class='spinner'></div>
                <p>Sedang memproses...</p>
            </div>

            <div style='text-align: center; margin-top: 20px;'>
                <button onclick='checkUpdate()' class='btn'>Periksa Pembaruan</button>
                )";
    
    if (updateAvailable) {
        html += R"(<button onclick='startUpdate()' class='btn btn-danger'>Pasang Pembaruan</button>)";
    }
    
    html += R"(
                <button onclick='location.href="/"' class='btn'>Kembali ke Menu Utama</button>
            </div>
        </div>

        <script>
            function showLoading() {
                document.getElementById('loadingSection').style.display = 'block';
            }
            
            function hideLoading() {
                document.getElementById('loadingSection').style.display = 'none';
            }

            function checkUpdate() {
                showLoading();
                fetch('/check-update')
                    .then(response => response.json())
                    .then(data => {
                        hideLoading();
                        document.getElementById('updateStatus').textContent = data.message;
                        if (data.updateAvailable || data.error) {
                            location.reload();
                        }
                    })
                    .catch(error => {
                        hideLoading();
                        alert('Error saat memeriksa pembaruan: ' + error);
                    });
            }

            function startUpdate() {
                if (confirm('Anda yakin ingin memperbarui firmware? Perangkat akan restart setelah pembaruan selesai.')) {
                    showLoading();
                    fetch('/start-update', { method: 'POST' })
                        .then(response => response.text())
                        .then(data => {
                            alert(data);
                            if (data.includes('started')) {
                                setTimeout(() => {
                                    location.reload();
                                }, 30000);
                            } else {
                                hideLoading();
                            }
                        })
                        .catch(error => {
                            hideLoading();
                            alert('Error saat memulai pembaruan: ' + error);
                        });
                }
            }
        </script>
    </body>
    </html>
    )";

    server.send(200, "text/html", html);
}

void handleCheckUpdate() {
    checkFirmwareUpdate();
    
    String response;
    if (versionCheckFailed) {
        response = "{\"error\":true,\"message\":\"Gagal mengambil versi dari server\"}";
    } else {
        response = "{\"updateAvailable\":" + String(updateAvailable ? "true" : "false") + ",";
        if (updateAvailable) {
            response += "\"version\":\"" + latestVersion + "\",";
            response += "\"message\":\"Pembaruan tersedia: " + latestVersion + "\"";
        } else {
            response += "\"message\":\"Sistem sudah menggunakan versi terbaru\"";
        }
        response += "}";
    }
    
    server.send(200, "application/json", response);
}

void handleStartUpdate() {
    if (!server.authenticate("admin", "admin123")) {
        return server.requestAuthentication();
    }

    if (!updateAvailable) {
        server.send(400, "text/plain", "No update available");
        return;
    }

    server.send(200, "text/plain", "Update process started");
    delay(1000);
    updateFirmware();
}

// =========================
// ======= GOOGLE APPS FUNCTIONS =======
// =========================

bool initGoogleApps()
{
    updateOLEDStatus("Checking GScript", "Connecting...");
    digitalWrite(LED_YELLOW, HIGH);

    bool connected = false;
    int retries = 0;

    while (!connected && retries < MAX_RETRIES)
    {
        if (testGoogleScriptConnection())
        {
            connected = true;
            isGScriptConnected = true;
            updateOLEDStatus("GScript Ready", "Connected!");
            blinkLED(LED_GREEN, 2, 200);
            beep(1, 200); // Success beep
            digitalWrite(LED_YELLOW, LOW);
            Serial.println("Google Apps Script connected successfully");
            break;
        }

        retries++;
        if (retries < MAX_RETRIES)
        {
            String retryMsg = "Retry " + String(retries) + "/" + String(MAX_RETRIES);
            updateOLEDStatus("Connection Failed", retryMsg);
            blinkLED(LED_RED, 2, 200);
            beep(2, 100); // Error beep
            delay(RETRY_DELAY);
        }
    }

    if (!connected)
    {
        updateOLEDStatus("GScript Failed", "Check connection");
        digitalWrite(LED_RED, HIGH);
        digitalWrite(LED_YELLOW, LOW);
        beep(3, 200); // Critical error beep
        Serial.println("Failed to connect to Google Apps Script");
    }

    return connected;
}

String getRedirectUrl(const String &response)
{
    int startPos = response.indexOf("HREF=\"") + 6;
    int endPos = response.indexOf("\"", startPos);
    if (startPos > 5 && endPos > startPos)
    {
        String url = response.substring(startPos, endPos);
        // Replace HTML encoded &amp; with &
        url.replace("&amp;", "&");
        return url;
    }
    return "";
}

bool testGoogleScriptConnection()
{
    // Create secure WiFi client
    WiFiClientSecure client;
    client.setInsecure(); // Skip certificate verification

    // Initial URL
    String initialUrl = "https://script.google.com/macros/s/" + String(GScriptId) + "/exec";
    Serial.println("Initial URL: " + initialUrl);

    // Create HTTP client
    HTTPClient https;

    if (https.begin(client, initialUrl))
    {
        https.addHeader("Content-Type", "application/json");
        https.addHeader("Accept", "application/json");
        String payload = "{\"command\":\"test_connection\"}";

        int httpCode = https.POST(payload);
        Serial.println("First request response code: " + String(httpCode));

        if (httpCode == 302)
        { // Handle redirect
            String response = https.getString();
            String redirectUrl = getRedirectUrl(response);
            https.end();

            Serial.println("Redirect URL found: " + redirectUrl);

            if (https.begin(client, redirectUrl))
            {
                // Modified headers for second request
                https.addHeader("Content-Type", "application/json");
                https.addHeader("Accept", "application/json");
                https.addHeader("User-Agent", "Mozilla/5.0");
                https.addHeader("x-requested-with", "XMLHttpRequest");

                // Try GET instead of POST for the redirect
                httpCode = https.GET();
                Serial.println("Second request response code: " + String(httpCode));

                if (httpCode == 200)
                {
                    String finalResponse = https.getString();
                    Serial.println("Final Response: " + finalResponse);

                    if (finalResponse == "Success")
                    {
                        Serial.print("Response: ");
                        Serial.println(finalResponse);
                        return true;
                    }
                    else
                    {
                        Serial.println("Connection test failed - unexpected response");
                        return false;
                    }
                }
                else
                {
                    Serial.println("Error on second request");
                    Serial.println("Response: " + https.getString());
                    return false;
                }
            }
        }
        else
        {
            Serial.println("Unexpected response on first request");
            Serial.println("Response: " + https.getString());
        }

        https.end();
    } else {
        Serial.println("HTTPS connection failed");
        return false;
    }

    return false;
}

bool sendBatchToGScript(const String &batchData) {
    if (!isGScriptConnected || batchData.isEmpty()) {
        return false;
    }

    isSending = true;
    updateOLEDStatus("Sending Data", "Please wait...");
    digitalWrite(LED_YELLOW, HIGH);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;

    // Format payload
    String payload = "{\"command\":\"insert_rows\",\"sheet_name\":\"LOG_Attendance\",\"values\":" + batchData + "}";
    Serial.println("Sending payload: " + payload);

    // Initial URL
    String initialUrl = "https://script.google.com/macros/s/" + String(GScriptId) + "/exec";
    Serial.println("Initial URL: " + initialUrl);

    bool success = false;
    int retries = 0;

    while (!success && retries < MAX_RETRIES) {
        if (https.begin(client, initialUrl)) {
            // Headers for initial request
            https.addHeader("Content-Type", "application/json");
            https.addHeader("Accept", "application/json");

            int httpCode = https.POST(payload);
            Serial.println("First request response code: " + String(httpCode));

            if (httpCode == 302) {
                String response = https.getString();
                String redirectUrl = getRedirectUrl(response);
                https.end();

                Serial.println("Redirect URL found: " + redirectUrl);

                if (https.begin(client, redirectUrl)) {
                    // Modified headers for redirect request
                    https.addHeader("Content-Type", "application/json");
                    https.addHeader("Accept", "application/json");
                    https.addHeader("User-Agent", "Mozilla/5.0");
                    https.addHeader("x-requested-with", "XMLHttpRequest");

                    // Use GET for the redirect request
                    httpCode = https.GET();
                    Serial.println("Second request response code: " + String(httpCode));

                    if (httpCode == 200) {
                        String finalResponse = https.getString();
                        Serial.println("Final Response: " + finalResponse);

                        if (finalResponse.startsWith("Success")) {
                            success = true;
                            String insertCount = finalResponse.substring(finalResponse.lastIndexOf(" "));
                            updateOLEDStatus("Data Sent", insertCount + " rows");
                            blinkLED(LED_GREEN, 2, 200);
                            beep(1, 200);
                            
                            // Reset buffer state after successful send
                            rfidBuffer.head = 0;
                            rfidBuffer.tail = 0;
                            rfidBuffer.count = 0;
                        }
                    }
                }
            }
            https.end();
        }

        if (!success) {
            retries++;
            if (retries < MAX_RETRIES) {
                String retryMsg = "Retry " + String(retries) + "/" + String(MAX_RETRIES);
                updateOLEDStatus("Send Failed", retryMsg);
                blinkLED(LED_RED, 1, 200);
                beep(2, 100);
                delay(RETRY_DELAY);
            }
        }
    }

    digitalWrite(LED_YELLOW, LOW);
    isSending = false;

    if (!success) {
        updateOLEDStatus("Send Failed", "server error, hubungi IT");
        blinkLED(LED_RED, 3, 200);
        beep(3, 200);
    }

    return success;
}

void checkGScriptConnection()
{
    if (millis() - lastConnectionCheck >= CONNECTION_CHECK_INTERVAL)
    {
        lastConnectionCheck = millis();

        // Block RFID terlebih dahulu
        mfrc522.PCD_AntennaOff();
        isProcessing = true;  // Prevent RFID processing

        // Feedback visual
        digitalWrite(LED_YELLOW, HIGH);
        digitalWrite(LED_GREEN, LOW);
        beep(1, 100);

        // Update OLED
        updateOLEDStatus("Checking GScript", "Connecting...");

        // Cek apakah ada data di buffer sebelum melakukan pengecekan
        if (rfidBuffer.count > 0) {
            updateOLEDStatus("Buffer Not Empty", "Sending data first...");
            
            // Coba kirim data buffer terlebih dahulu
            String batchData = prepareDataForBatch();
            if (!batchData.isEmpty()) {
                if (sendBatchToGScript(batchData)) {
                    updateOLEDStatus("Buffer Sent", "Checking connection...");
                    successBeep();
                    blinkLED(LED_GREEN, 2, 200);
                } else {
                    updateOLEDStatus("Send Failed", "Checking connection...");
                    errorBeep();
                    blinkLED(LED_RED, 2, 200);
                }
            }
            delay(1000); // Berikan waktu untuk membaca pesan
        }

        if (!testGoogleScriptConnection())
        {
            isGScriptConnected = false;
            updateOLEDStatus("GScript Lost", "Reconnecting..."); 
            digitalWrite(LED_RED, HIGH);
            errorBeep();

            gScriptConnectionFailureCount++;
            if (gScriptConnectionFailureCount >= MAX_GSCRIPT_CONNECTION_FAILURES)
            {
                showErrorOLED("Hubungi tim IT");
                digitalWrite(LED_RED, HIGH);
                mfrc522.PCD_AntennaOff();  // Keep RFID disabled
                isProcessing = true;        // Keep RFID processing blocked
                return;  // Don't enable RFID if max failures reached
            }
            else if (initGoogleApps())
            {
                isGScriptConnected = true;
                digitalWrite(LED_RED, LOW);
                updateOLEDStatus("GScript", "Reconnected!");
                successBeep();
                gScriptConnectionFailureCount = 0;
                
                // Re-enable RFID only if reconnection successful
                mfrc522.PCD_AntennaOn();
                isProcessing = false;
            }
        }
        else
        {
            // Koneksi berhasil
            digitalWrite(LED_YELLOW, LOW);
            digitalWrite(LED_GREEN, HIGH);
            gScriptConnectionFailureCount = 0;

            // Tampilkan status default
            showDefaultOLEDDisplay();
            
            // Re-enable RFID only after successful check
            mfrc522.PCD_AntennaOn();
            isProcessing = false;
        }

        // Jika masih ada masalah koneksi, keep RFID disabled
        if (!isGScriptConnected) {
            mfrc522.PCD_AntennaOff();
            isProcessing = true;
            updateOLEDStatus("GScript Error", "RFID Disabled");
            digitalWrite(LED_RED, HIGH);
        }
    }
}

// Fungsi untuk mengirim data yang tersimpan di buffer
bool processPendingData()
{
    if (!isGScriptConnected) {
        return false;
    }

    unsigned long currentTime = millis();
    bool shouldSend = false;

    // Cek apakah sudah mencapai minimum batch size
    if (rfidBuffer.count >= MIN_BATCH_SIZE) {
        shouldSend = true;
        updateOLEDStatus("Buffer Full", "Sending data...");
    }
    // Cek apakah timeout tercapai dan ada data
    else if (rfidBuffer.count > 0 && (currentTime - lastDataTime) >= SEND_TIMEOUT) {
        shouldSend = true;
        updateOLEDStatus("Timeout", "Sending data...");
    }

    if (shouldSend) {
        String batchData = prepareDataForBatch();
        if (!batchData.isEmpty()) {
            bool result = sendBatchToGScript(batchData);
            if (result) {
                lastDataTime = currentTime;  // Reset timer
                return true;
            }
        }
    }

    return false;
}

// Function untuk dipanggil di setup()
void setupGoogleApps()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        initGoogleApps();
    }
}

// Function untuk dipanggil di loop()
void handleGoogleApps()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        checkGScriptConnection();
        processPendingData();
    }
}

// =========================
// ======= RFID FUNCTIONS =======
// =========================

void initRFID()
{
    SPI.begin();
    mfrc522.PCD_Init();

    // Initialize RFID key (default)
    for (byte i = 0; i < 6; i++)
    {
        key.keyByte[i] = 0xFF;
    }

    // Reset buffer state
    rfidBuffer.head = 0;
    rfidBuffer.tail = 0;
    rfidBuffer.count = 0;

    updateOLEDStatus("RFID Ready", "Waiting for card");
    Serial.println("RFID subsystem initialized");
}

bool addToBuffer(const RFIDData &data)
{
    if (rfidBuffer.count >= MAX_BUFFER_SIZE)
    {
        return false;
    }

    rfidBuffer.data[rfidBuffer.tail] = data;
    rfidBuffer.tail = (rfidBuffer.tail + 1) % MAX_BUFFER_SIZE;
    rfidBuffer.count++;
    lastDataTime = millis();  // Update waktu data terakhir

    return true;
}

// Update the readRFIDBlock function to properly handle the data
String readRFIDBlock(byte blockAddr) {
    status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockAddr, &key, &(mfrc522.uid));
    if (status != MFRC522::STATUS_OK) {
        return "";
    }

    status = mfrc522.MIFARE_Read(blockAddr, readBlockData, &bufferLen);
    if (status != MFRC522::STATUS_OK) {
        return "";
    }

    String data;
    for (uint8_t i = 0; i < 16; i++) {
        // Only add printable characters
        if (readBlockData[i] >= 32 && readBlockData[i] <= 126) {
            data += (char)readBlockData[i];
        }
    }
    
    return cleanString(data); // Clean the string before returning
}

void resetRFIDModule() {
    mfrc522.PCD_Reset();
    delay(50);
    mfrc522.PCD_Init();
    
    // Reset authentication state
    mfrc522.PCD_StopCrypto1();
    
    // Re-initialize key
    for (byte i = 0; i < 6; i++) {
        key.keyByte[i] = 0xFF;
    }
}

void processRFIDCard() {
    static uint8_t failureCount = 0;
    static unsigned long lastFailureTime = 0;
    const uint8_t MAX_FAILURES = 3;
    const unsigned long RECOVERY_TIME = 1000; // 1 detik untuk recovery

    if (isProcessing || isSending) {
        return;
    }

    // Basic presence check with timeout
    unsigned long startTime = millis();
    bool cardDetected = false;
    
    while (millis() - startTime < READ_TIMEOUT) {
        if (mfrc522.PICC_IsNewCardPresent()) {
            cardDetected = true;
            break;
        }
        delay(5);
    }

    if (!cardDetected) {
        return;
    }

    // Try to read serial with timeout
    startTime = millis();
    bool serialRead = false;
    
    while (millis() - startTime < READ_TIMEOUT) {
        if (mfrc522.PICC_ReadCardSerial()) {
            serialRead = true;
            break;
        }
        delay(5);
    }

    if (!serialRead) {
        failureCount++;
        lastFailureTime = millis();
        updateOLEDStatus("Read Error", "Please try again");
        errorBeep();
        blinkLED(LED_RED, 2, 200);

        // Cek jika failure count mencapai threshold
        if (failureCount >= MAX_FAILURES) {
            // Update OLED
            updateOLEDStatus("Critical Error", "Sending buffer...");
            digitalWrite(LED_RED, HIGH);
            errorBeep();
            
            // Kirim data yang ada di buffer
            if (rfidBuffer.count > 0) {
                String batchData = prepareDataForBatch();
                if (!batchData.isEmpty()) {
                    // Update OLED dengan status pengiriman
                    updateOLEDStatus("Sending Data", "Before restart...");
                    
                    if (sendBatchToGScript(batchData)) {
                        updateOLEDStatus("Data Sent", "Restarting...");
                        successBeep();
                        blinkLED(LED_GREEN, 2, 200);
                    } else {
                        updateOLEDStatus("Send Failed", "Restarting...");
                        errorBeep();
                        blinkLED(LED_RED, 3, 200);
                    }
                }
            } else {
                updateOLEDStatus("No Data", "Restarting...");
            }
            
            // Delay sebelum restart
            delay(2000);
            ESP.restart();
        }
        return;
    }

    // Check cooldown period
    if (millis() - lastSuccessfulRead < READ_COOLDOWN) {
        return;
    }

    isProcessing = true;
    digitalWrite(LED_YELLOW, HIGH);
    updateOLEDStatus("Reading Card", "Please wait...");

    // Prepare RFID data structure
    RFIDData newData;
    newData.timestamp = millis();

    // Read UID
    String uid = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
        uid += (mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
        uid += String(mfrc522.uid.uidByte[i], HEX);
    }
    newData.uid = uid;

    // Read all configured blocks dengan format baru: NISN, NIP, Nama
    bool readSuccess = true;
    for (byte i = 0; i < total_blocks; i++) {
        String blockData = readRFIDBlock(blocks[i]);
        if (blockData.length() > 0) {
            newData.blockData[i] = blockData;
            String blockType;
            switch(i) {
                case 0: blockType = "NISN"; break;
                case 1: blockType = "NIP"; break;
                case 2: blockType = "Nama"; break;
            }
            Serial.println(blockType + ": " + blockData);
        } else {
            readSuccess = false;
            failureCount++;
            lastFailureTime = millis();
            break;
        }
    }

    if (readSuccess) {
        if (addToBuffer(newData)) {
            // Reset failure count on success
            failureCount = 0;
            lastSuccessfulRead = millis();

            // Show the name that was just read (index 2 is Nama)
            String displayName = cleanString(newData.blockData[2]);
            if (displayName.length() > 16) {
                displayName = displayName.substring(0, 13) + "...";
            }

            // Display feedback sequence
            blinkLED(LED_GREEN, 1, 100);
            beep(1, 100);
            
            // Show name briefly
            updateOLEDStatus("Berhasil Scan", displayName);
            delay(1000);
            
            // Show buffer status
            updateOLEDStatus("Ready", "Buffer: " + String(rfidBuffer.count));
            Serial.println("Card read successful. Buffer count: " + String(rfidBuffer.count));

            // Additional debug info
            Serial.println("NISN: " + newData.blockData[0]);
            Serial.println("NIP: " + newData.blockData[1]);
            Serial.println("Nama: " + newData.blockData[2]);
        } else {
            // Buffer full
            blinkLED(LED_RED, 2, 100);
            beep(2, 100);
            updateOLEDStatus("Buffer Full!", "Please wait...");
            Serial.println("Buffer full - cannot add new data");
            
            // Try to send buffer immediately if full
            String batchData = prepareDataForBatch();
            if (!batchData.isEmpty()) {
                if (sendBatchToGScript(batchData)) {
                    updateOLEDStatus("Buffer Sent", "Scan Again");
                    successBeep();
                    blinkLED(LED_GREEN, 2, 200);
                }
            }
        }
    } else {
        // Read failed
        blinkLED(LED_RED, 2, 200);
        beep(2, 200);
        updateOLEDStatus("Read Failed", "Try again");
        Serial.println("Failed to read card data");
        
        // Cek threshold kegagalan
        if (failureCount >= MAX_FAILURES) {
            // Update OLED
            updateOLEDStatus("Critical Error", "Sending buffer...");
            digitalWrite(LED_RED, HIGH);
            errorBeep();
            
            // Kirim data yang ada di buffer
            if (rfidBuffer.count > 0) {
                String batchData = prepareDataForBatch();
                if (!batchData.isEmpty()) {
                    updateOLEDStatus("Sending Data", "Before restart...");
                    
                    if (sendBatchToGScript(batchData)) {
                        updateOLEDStatus("Data Sent", "Restarting...");
                        successBeep();
                        blinkLED(LED_GREEN, 2, 200);
                    } else {
                        updateOLEDStatus("Send Failed", "Restarting...");
                        errorBeep();
                        blinkLED(LED_RED, 3, 200);
                    }
                }
            } else {
                updateOLEDStatus("No Data", "Restarting...");
            }
            
            delay(2000);
            ESP.restart();
        }
    }

    digitalWrite(LED_YELLOW, LOW);
    isProcessing = false;

    // Always properly close the current card operation
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
}

// Fungsi untuk membersihkan string dari karakter null dan whitespace
String cleanString(const String &str) {
    String result;
    
    // Iterate through each character
    for (size_t i = 0; i < str.length(); i++) {
        // Skip null characters and non-printable characters
        if (str[i] != '\0' && str[i] >= 32 && str[i] <= 126) {
            result += str[i];
        }
    }
    
    // Trim leading whitespace
    while (result.length() > 0 && isSpace(result.charAt(0))) {
        result = result.substring(1);
    }
    
    // Trim trailing whitespace
    while (result.length() > 0 && isSpace(result.charAt(result.length() - 1))) {
        result = result.substring(0, result.length() - 1);
    }
    
    return result;
}

// Fungsi untuk mendapatkan data untuk pengiriman batch
String prepareDataForBatch() {
    if (rfidBuffer.count == 0) return "";

    String batchData = "[";
    int batchSize = min(MIN_BATCH_SIZE, rfidBuffer.count);

    for (int i = 0; i < batchSize; i++) {
        RFIDData &data = rfidBuffer.data[rfidBuffer.head];
        
        // Data array untuk satu baris: [NISN, NIP, Nama]
        batchData += "[";
        
        // Clean and add each field
        String cleanNISN = cleanString(data.blockData[0]);
        String cleanNIP = cleanString(data.blockData[1]);
        String cleanNama = cleanString(data.blockData[2]);
        
        // Add cleaned data with proper JSON escaping
        batchData += "\"" + cleanNISN + "\",";
        batchData += "\"" + cleanNIP + "\",";
        batchData += "\"" + cleanNama + "\"";
        
        batchData += "]";
        
        if (i < batchSize - 1) batchData += ",";
        
        rfidBuffer.head = (rfidBuffer.head + 1) % MAX_BUFFER_SIZE;
        rfidBuffer.count--;
    }
    
    batchData += "]";

    Serial.println("Prepared batch data: " + batchData);
    return batchData;
}



// Function to be called in main loop
// Update handleRFID untuk memberikan feedback yang lebih baik
void handleRFID()
{
    // Immediate return if WiFi is disconnected
    if (WiFi.status() != WL_CONNECTED) {
        if (!isProcessing) {  // Only disable once
            mfrc522.PCD_AntennaOff();
            isProcessing = true;
            updateOLEDStatus("WiFi Terputus", "RFID Dinonaktifkan");
            digitalWrite(LED_RED, HIGH);
            errorBeep();
        }
        return;
    }


    // Add additional check for connection check in progress
    if (!isProcessing && !isSending && isGScriptConnected)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            updateOLEDStatus("WiFi Disconnected", "RFID Disabled");
            blinkLED(LED_YELLOW, 2, 300);
            return;
        }

        if (rfidBuffer.count >= MIN_BATCH_SIZE) {
            updateOLEDStatus("Buffer Full", "Please wait...");
            return;
        }
        
        processRFIDCard();
    }
    else if (!isGScriptConnected) {
        updateOLEDStatus("GScript Error", "RFID Disabled");
        blinkLED(LED_RED, 1, 300);
    }
}

// =========================
// ======= IMPLEMENTASI OLED =======
// =========================

void initOLED()
{
    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
    {
        Serial.println(F("Gagal menginisialisasi OLED"));
        return;
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.cp437(true);

    // Tampilan awal
    display.setCursor(0, 0);
    display.println(F("Inisialisasi..."));
    display.println(F("Sistem Absensi"));
    display.println(F("SDS Telkom Batam"));
    display.display();
    delay(2000);
}

void clearOLED()
{
    display.clearDisplay();
    display.display();
}

void updateOLEDStatus(const String &primaryText, const String &secondaryText, bool showDefaultDisplay)
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);

    if (showDefaultDisplay && WiFi.status() == WL_CONNECTED)
    {
        showDefaultOLEDDisplay();
    }
    else
    {
        display.println(primaryText);
        if (secondaryText.length() > 0)
        {
            display.println(secondaryText);
        }

        // Jika ada data di buffer, tampilkan total di bagian bawah
        if (rfidBuffer.count > 0)
        {
            display.println(); // Beri jarak
            display.print("Total Scanned: ");
            display.println(rfidBuffer.count);
        }
    }

    if (isSending)
    {
        display.setCursor(0, SCREEN_HEIGHT - 8);
        display.println("Mengirim data...");
    }

    display.display();
}

void showDefaultOLEDDisplay()
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);

    // Header
    display.println("SDS Telkom Batam");
    display.println("----------------");

    // Network Info
    display.print("SSID: ");
    display.println(WiFi.SSID());
    display.print("IP: ");
    display.println(WiFi.localIP().toString());

    // Blank line for spacing
    display.println();

    // Only show total scanned if buffer has data
    if (rfidBuffer.count > 0)
    {
        display.print("Total Scanned: ");
        display.println(rfidBuffer.count);
    }

    display.display();
}

void showErrorOLED(const String &errorMsg)
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("ERROR:");
    display.println(errorMsg);
    display.display();
}

// =========================
// ======= IMPLEMENTASI LED =======
// =========================

void initLEDs()
{
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_YELLOW, OUTPUT);
    setAllLEDs(false);
}

void updateLEDStatus(ErrorType error)
{
    switch (error)
    {
    case NO_ERROR:
        digitalWrite(LED_GREEN, WiFi.status() == WL_CONNECTED);
        digitalWrite(LED_RED, LOW);
        digitalWrite(LED_YELLOW, isSending);
        break;
    case WIFI_CONNECTION_FAILED:
    case GOOGLE_SCRIPT_CONNECTION_FAILED:
        digitalWrite(LED_GREEN, LOW);
        digitalWrite(LED_RED, HIGH);
        digitalWrite(LED_YELLOW, LOW);
        break;
    default:
        digitalWrite(LED_GREEN, LOW);
        digitalWrite(LED_RED, HIGH);
        digitalWrite(LED_YELLOW, isSending);
    }
}

void blinkLED(uint8_t pin, uint8_t times, uint16_t delayMs)
{
    for (uint8_t i = 0; i < times; i++)
    {
        digitalWrite(pin, HIGH);
        delay(delayMs / 2);
        digitalWrite(pin, LOW);
        delay(delayMs / 2);
    }
}

void setAllLEDs(bool state)
{
    digitalWrite(LED_GREEN, state);
    digitalWrite(LED_RED, state);
    digitalWrite(LED_YELLOW, state);
}

// =========================
// ======= IMPLEMENTASI BUZZER =======
// =========================

void initBuzzer()
{
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
}

void beep(uint8_t times, uint16_t durationMs)
{
    for (uint8_t i = 0; i < times; i++)
    {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(durationMs);
        digitalWrite(BUZZER_PIN, LOW);
        if (i < times - 1)
        {
            delay(durationMs);
        }
    }
}

void errorBeep()
{
    beep(3, 100); // 3 beep pendek untuk error
}

void successBeep()
{
    beep(1, 200); // 1 beep panjang untuk sukses
}

// =========================
// ======= IMPLEMENTASI FUNGSI =======
// =========================

// Inisialisasi WiFi
// Modifikasi fungsi initWiFi
void initWiFi()
{
    EEPROM.begin(512);
    loadWiFiCredentials();

    if (!wifiCred.ssid.isEmpty())
    {
        String connectingMsg = "Menghubungkan ke:";
        String ssidMsg = wifiCred.ssid;
        updateOLEDStatus(connectingMsg, ssidMsg);
        blinkLED(LED_YELLOW, 2, 200);

        int attempts = 0;
        bool connected = false;

        while (attempts < MAX_CONNECTION_ATTEMPTS && !connected)
        {
            showConnectionProgress(attempts + 1, MAX_CONNECTION_ATTEMPTS);
            connected = connectToWiFi(wifiCred.ssid, wifiCred.password);

            if (!connected)
            {
                attempts++;
                if (attempts < MAX_CONNECTION_ATTEMPTS)
                {
                    String attemptMsg = "Percobaan " + String(attempts + 1) + "/" + String(MAX_CONNECTION_ATTEMPTS);
                    updateOLEDStatus("Koneksi Gagal", attemptMsg);
                    errorBeep();
                    blinkLED(LED_RED, 2, 200);
                    delay(WIFI_RETRY_DELAY);
                }
            }
        }

        if (!connected)
        {
            handleConnectionFailure();
        }
        else
        {
            // Koneksi berhasil
            successBeep();
            blinkLED(LED_GREEN, 3, 200);
            String ipMsg = "IP: " + WiFi.localIP().toString();
            updateOLEDStatus("Terhubung!", ipMsg);

            // Penting: Setup web server setelah koneksi berhasil
            setupWebServer();
            delay(2000);
        }
    }
    else
    {
        updateOLEDStatus("Tidak ada WiFi", "Beralih ke mode AP");
        delay(1000);
        switchToAPMode();
    }
}

// Setup Mode AP
void setupAP()
{
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_SSID, AP_PASSWORD);

    dnsServer.start(DNS_PORT, "*", apIP);
    setupWebServer();

    isAPMode = true;
    updateOLEDStatus("Mode AP Aktif", String("SSID: ") + AP_SSID);
    blinkLED(LED_YELLOW, 3, 200);
    successBeep();
}

// Koneksi ke WiFi
bool connectToWiFi(const String &ssid, const String &password) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    unsigned long startAttemptTime = millis();
    
    updateOLEDStatus("Menghubungkan", "ke " + ssid);

    while (WiFi.status() != WL_CONNECTED && 
           millis() - startAttemptTime < WIFI_TIMEOUT) {
        delay(100);
        blinkLED(LED_YELLOW, 1, 100);
    }

    if (WiFi.status() == WL_CONNECTED) {
        // Update tampilan OLED dengan informasi koneksi
        updateOLEDStatus("Terhubung!", "IP: " + WiFi.localIP().toString());
        successBeep();
        
        // Start web server
        setupWebServer();
        return true;
    }

    return false;
}

void checkAndUpdateWiFiStatus() {
    if (isAPMode) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println("Mode AP Aktif");
        display.println("----------------");
        display.println("SSID: " + String(AP_SSID));
        display.println("Pass: " + String(AP_PASSWORD));
        display.println("IP: " + apIP.toString());
        display.display();
    } else if (WiFi.status() == WL_CONNECTED) {
        showDefaultOLEDDisplay();
    } else {
        updateOLEDStatus("Tidak Terhubung", "Mencoba koneksi ulang...");
    }
}

// Beralih ke Mode AP
void switchToAPMode() {
    // Matikan WiFi sebelum beralih ke mode AP
    WiFi.disconnect(true);
    delay(500);

    setupAP();
    
    // Update tampilan OLED untuk mode AP dengan informasi lengkap
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Mode AP Aktif");
    display.println("----------------");
    display.println("SSID: " + String(AP_SSID));
    display.println("Pass: " + String(AP_PASSWORD));
    display.println("IP: " + apIP.toString());
    display.display();

    successBeep();
}

// Setup Web Server
void setupWebServer() {
    // Basic routes
    server.on("/", HTTP_GET, handleRoot);
    server.on("/scan", HTTP_GET, handleWiFiScan);
    server.on("/connect", HTTP_POST, handleConnect);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/forget", HTTP_POST, handleForget);
    
    // OTA routes
    server.on("/ota", HTTP_GET, handleOTAUpdate);
    server.on("/check-update", HTTP_GET, handleCheckUpdate);
    server.on("/start-update", HTTP_POST, handleStartUpdate);
    
    // Start server
    server.begin();
    
    Serial.println("HTTP server started");
}

// Fungsi baru untuk menangani kegagalan koneksi
void handleConnectionFailure() {
    // Visual feedback
    updateOLEDStatus("Koneksi Gagal", "Beralih ke Mode AP...");
    blinkLED(LED_RED, 5, 300);
    errorBeep();
    delay(1000);

    // Hapus kredensial
    resetWiFiCredentials();

    // Update OLED sebelum restart
    updateOLEDStatus("Memulai Ulang", "Mode AP akan aktif");
    delay(2000);

    // Feedback akhir
    blinkLED(LED_YELLOW, 3, 500);
    beep(2, 300);

    ESP.restart();
}

// Fungsi untuk menampilkan progress koneksi
void showConnectionProgress(int attempt, int maxAttempts)
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Menghubungkan ke:");
    display.println(wifiCred.ssid);
    display.println();

    // Gambar progress bar
    int progressWidth = (attempt * (SCREEN_WIDTH - 4)) / maxAttempts;
    display.drawRect(0, 32, SCREEN_WIDTH, 8, SSD1306_WHITE);
    display.fillRect(2, 34, progressWidth, 4, SSD1306_WHITE);

    // Tampilkan nomor percobaan
    String attemptText = "Percobaan " + String(attempt) + "/" + String(maxAttempts);
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(attemptText, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 45);
    display.println(attemptText);

    display.display();
}

// Handler Web Server
void handleRoot() {
    String html = R"(
    <!DOCTYPE html>
    <html>
    <head>
        <meta name='viewport' content='width=device-width, initial-scale=1.0'>
        <title>SDS Telkom Batam WiFi Manager</title>
        <style>
            body { font-family: Arial; margin: 0; padding: 20px; background: #f0f0f0; }
            .container { max-width: 500px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
            .btn { background: #007bff; color: white; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; margin: 5px; }
            .btn:hover { background: #0056b3; }
            .btn-danger { background: #dc3545; }
            .btn-danger:hover { background: #c82333; }
            .btn-warning { background: #ffc107; color: #000; }
            .btn-warning:hover { background: #e0a800; }
            .status { margin: 20px 0; padding: 15px; border-radius: 4px; }
            .connected { background: #d4edda; color: #155724; }
            .disconnected { background: #f8d7da; color: #721c24; }
            .info { background: #cce5ff; color: #004085; padding: 10px; border-radius: 4px; margin: 10px 0; }
            .header { text-align: center; margin-bottom: 20px; }
            .loading { display: none; text-align: center; padding: 20px; }
            .spinner { border: 4px solid #f3f3f3; border-top: 4px solid #3498db; border-radius: 50%; width: 40px; height: 40px; animation: spin 1s linear infinite; margin: 10px auto; }
            @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
            .current-network { background: #e8f5e9; padding: 15px; border-radius: 4px; margin: 10px 0; }
            .update-available { background: #fff3cd; color: #856404; padding: 10px; border-radius: 4px; margin: 10px 0; display: none; }
        </style>
    </head>
    <body>
        <div class='container'>
            <div class='header'>
                <h1>WiFi Manager</h1>
                <h2>SDS Telkom Batam</h2>
            </div>
            
            <div id='currentNetwork' class='current-network'></div>
            
            <div class='info'>
                Anda dapat mengubah jaringan WiFi, menghapus koneksi saat ini, atau memperbarui firmware.
            </div>

            <div id='updateNotification' class='update-available'>
                <strong>Pembaruan Tersedia!</strong> 
                <span id='updateVersion'></span>
            </div>
            
            <div id='status' class='status'></div>
            
            <div class='loading' id='loadingSection'>
                <div class='spinner'></div>
                <p>Sedang memproses...</p>
            </div>
            
            <div style='text-align: center;'>
                <button class='btn' onclick='scanWiFi()'>Hubungkan ke WiFi Lain</button>
                <button class='btn btn-danger' onclick='forgetCurrentNetwork()'>Lupakan Jaringan Ini</button>
                <button class='btn btn-warning' onclick='checkFirmwareUpdate()'>Cek Pembaruan</button>
                <button class='btn' onclick='location.href="/ota"'>Kelola Firmware</button>
            </div>
        </div>
        <script>
            function showLoading() {
                document.getElementById('loadingSection').style.display = 'block';
            }
            
            function hideLoading() {
                document.getElementById('loadingSection').style.display = 'none';
            }
            
            function scanWiFi() {
                if(confirm('Anda akan mencari jaringan WiFi baru. Lanjutkan?')) {
                    showLoading();
                    location.href = '/scan';
                }
            }
            
            function forgetCurrentNetwork() {
                if(confirm('Anda yakin ingin melupakan jaringan ini? Perangkat akan mencoba masuk ke mode AP.')) {
                    showLoading();
                    fetch('/forget', { method: 'POST' })
                        .then(response => response.text())
                        .then(data => {
                            alert(data);
                            location.reload();
                        });
                }
            }

            function checkFirmwareUpdate() {
                showLoading();
                fetch('/check-update')
                    .then(response => response.json())
                    .then(data => {
                        hideLoading();
                        const updateNotification = document.getElementById('updateNotification');
                        const updateVersion = document.getElementById('updateVersion');
                        
                        if (data.updateAvailable) {
                            updateVersion.textContent = '(Versi ' + data.version + ' tersedia)';
                            updateNotification.style.display = 'block';
                        } else {
                            alert('Tidak ada pembaruan tersedia. Sistem Anda sudah versi terbaru.');
                        }
                    })
                    .catch(error => {
                        hideLoading();
                        alert('Error memeriksa pembaruan: ' + error.message);
                    });
            }
            
            function updateStatus() {
                fetch('/status')
                    .then(response => response.json())
                    .then(data => {
                        const statusDiv = document.getElementById('status');
                        const currentNetworkDiv = document.getElementById('currentNetwork');
                        
                        statusDiv.className = 'status ' + (data.connected ? 'connected' : 'disconnected');
                        let statusHtml = `<strong>Status:</strong> ${data.status}<br>`;
                        statusHtml += `<strong>IP:</strong> ${data.ip}`;
                        statusDiv.innerHTML = statusHtml;
                        
                        if(data.connected) {
                            currentNetworkDiv.innerHTML = `
                                <h3>Jaringan Saat Ini:</h3>
                                <strong>SSID:</strong> ${data.ssid}<br>
                                <strong>Kekuatan Sinyal:</strong> ${data.rssi} dBm<br>
                                <strong>IP Address:</strong> ${data.ip}
                            `;
                        } else {
                            currentNetworkDiv.innerHTML = '<p>Tidak terhubung ke jaringan WiFi</p>';
                        }
                        
                        hideLoading();
                    });
            }
            
            // Check for updates when page loads
            window.addEventListener('load', function() {
                updateStatus();
                checkFirmwareUpdate();
            });
            
            // Update status every 5 seconds
            setInterval(updateStatus, 5000);
        </script>
    </body>
    </html>
    )";
    server.send(200, "text/html", html);
}

// Tambahkan handler untuk melupakan jaringan
void handleForget()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        // Simpan kredensial sebelumnya
        previousSSID = wifiCred.ssid;
        previousPassword = wifiCred.password;

        // Reset kredensial
        resetWiFiCredentials();

        server.send(200, "text/plain", "Berhasil melupakan jaringan");

        // Tunggu sebentar sebelum restart
        delay(1000);
        ESP.restart();
    }
    else
    {
        server.send(400, "text/plain", "Tidak ada jaringan yang terhubung");
    }
}

void handleWiFiScan()
{
    int n = WiFi.scanNetworks();
    String html = R"(
    <!DOCTYPE html>
    <html>
    <head>
        <meta name='viewport' content='width=device-width, initial-scale=1.0'>
        <title>Jaringan WiFi Tersedia</title>
        <style>
            body { font-family: Arial; margin: 0; padding: 20px; background: #f0f0f0; }
            .container { max-width: 500px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
            .network { margin: 10px 0; padding: 15px; border: 1px solid #ddd; border-radius: 4px; cursor: pointer; display: flex; justify-content: space-between; align-items: center; }
            .network:hover { background: #f8f9fa; }
            .network.current { background: #e8f5e9; border-color: #4caf50; }
            .btn { background: #007bff; color: white; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; }
            .btn:hover { background: #0056b3; }
            .signal-strength { padding: 5px 10px; border-radius: 3px; color: white; font-size: 0.9em; }
            .signal-excellent { background: #4caf50; }
            .signal-good { background: #8bc34a; }
            .signal-fair { background: #ffc107; }
            .signal-poor { background: #ff5722; }
            .loading { display: none; text-align: center; padding: 20px; }
            .spinner { border: 4px solid #f3f3f3; border-top: 4px solid #3498db; border-radius: 50%; width: 40px; height: 40px; animation: spin 1s linear infinite; margin: 10px auto; }
            @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
            #password-input { display: none; margin-top: 20px; padding: 20px; background: #f8f9fa; border-radius: 4px; }
            #password-input input[type="password"] { width: 100%; padding: 10px; margin: 10px 0; border: 1px solid #ddd; border-radius: 4px; }
        </style>
    </head>
    <body>
        <div class='container'>
            <h1>Jaringan WiFi Tersedia</h1>
            <p>Pilih jaringan untuk menghubungkan:</p>
            <div id='networks'>)";

    // Tambahkan jaringan yang terdeteksi
    for (int i = 0; i < n; ++i)
    {
        String currentClass = (WiFi.SSID(i) == WiFi.SSID()) ? "network current" : "network";
        String signalStrength;
        String signalClass;

        // Kategorikan kekuatan sinyal
        int rssi = WiFi.RSSI(i);
        if (rssi >= -50)
        {
            signalStrength = "Excellent";
            signalClass = "signal-excellent";
        }
        else if (rssi >= -60)
        {
            signalStrength = "Good";
            signalClass = "signal-good";
        }
        else if (rssi >= -70)
        {
            signalStrength = "Fair";
            signalClass = "signal-fair";
        }
        else
        {
            signalStrength = "Poor";
            signalClass = "signal-poor";
        }

        html += "<div class='" + currentClass + "' onclick='selectNetwork(\"" + WiFi.SSID(i) + "\")'>";
        html += "<div class='network-info'>";
        html += "<strong>" + WiFi.SSID(i) + "</strong>";
        if (WiFi.SSID(i) == WiFi.SSID())
        {
            html += " (Current)";
        }
        html += "</div>";
        html += "<span class='signal-strength " + signalClass + "'>" + signalStrength + " (" + String(WiFi.RSSI(i)) + " dBm)</span>";
        html += "</div>";
    }

    html += R"(
            </div>
            <form action='/connect' method='POST' id='wifi-form'>
                <input type='hidden' name='ssid' id='ssid-input'>
                <div id='password-input'>
                    <h3 id='selected-network'></h3>
                    <input type='password' name='password' placeholder='Password WiFi' required>
                    <div style='text-align: right; margin-top: 10px;'>
                        <button type='button' class='btn' onclick='cancelSelection()'>Batal</button>
                        <button type='submit' class='btn'>Hubungkan</button>
                    </div>
                </div>
            </form>
            <div class='loading' id='loadingSection'>
                <div class='spinner'></div>
                <p>Memindai jaringan...</p>
            </div>
            <div style='margin-top: 20px;'>
                <button onclick='location.href="/"' class='btn'>Kembali ke Menu Utama</button>
                <button onclick='refreshNetworks()' class='btn'>Pindai Ulang</button>
            </div>
        </div>
        <script>
            function selectNetwork(ssid) {
                document.getElementById('ssid-input').value = ssid;
                document.getElementById('selected-network').textContent = 'Jaringan: ' + ssid;
                document.getElementById('password-input').style.display = 'block';
                // Scroll to password input
                document.getElementById('password-input').scrollIntoView({ behavior: 'smooth' });
            }
            
            function cancelSelection() {
                document.getElementById('password-input').style.display = 'none';
                document.getElementById('ssid-input').value = '';
            }
            
            function refreshNetworks() {
                document.getElementById('loadingSection').style.display = 'block';
                location.reload();
            }
            
            document.getElementById('wifi-form').onsubmit = function() {
                document.getElementById('loadingSection').style.display = 'block';
                document.getElementById('loadingSection').querySelector('p').textContent = 'Menghubungkan...';
            }
        </script>
    </body>
    </html>
    )";

    server.send(200, "text/html", html);
}

void handleConnect()
{
    if (!server.hasArg("ssid") || !server.hasArg("password"))
    {
        server.send(400, "text/plain", "SSID dan password diperlukan");
        return;
    }

    String newSSID = server.arg("ssid");
    String newPassword = server.arg("password");

    // Simpan kredensial lama sebelum mencoba yang baru
    previousSSID = wifiCred.ssid;
    previousPassword = wifiCred.password;

    // Coba koneksi ke jaringan baru
    updateOLEDStatus("Mencoba koneksi", "ke " + newSSID);
    WiFi.disconnect();
    WiFi.begin(newSSID.c_str(), newPassword.c_str());

    unsigned long startAttemptTime = millis();
    bool connected = false;

    while (millis() - startAttemptTime < WIFI_TIMEOUT)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            connected = true;
            break;
        }
        delay(500);
    }

    if (connected)
    {
        // Koneksi berhasil, simpan kredensial baru
        saveWiFiCredentials(newSSID, newPassword);
        isAPMode = false; // Penting: ubah mode

        // Update tampilan OLED dengan informasi baru
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println("Terhubung!");
        display.println("----------------");
        display.println("SSID: " + newSSID);
        display.println("IP: " + WiFi.localIP().toString());
        display.display();

        // Feedback sukses
        successBeep();
        blinkLED(LED_GREEN, 3, 200);

        String html = R"(
    <!DOCTYPE html>
    <html>
    <head>
        <meta name='viewport' content='width=device-width, initial-scale=1.0'>
        <meta http-equiv='refresh' content='5;url=/'>
        <title>Berhasil Terhubung</title>
        <style>
            body { font-family: Arial; margin: 0; padding: 20px; background: #f0f0f0; }
            .container { max-width: 500px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
            .success { color: #155724; background: #d4edda; padding: 15px; border-radius: 4px; }
        </style>
    </head>
    <body>
        <div class='container'>
            <div class='success'>
                <h2>Berhasil Terhubung!</h2>
                <p>Terhubung ke: )" +
                      newSSID + R"(</p>
                <p>IP: )" +
                      WiFi.localIP().toString() + R"(</p>
                <p>Silahkan akses kembali menggunakan IP address baru</p>
            </div>
            <p>Halaman akan dialihkan dalam 5 detik...</p>
        </div>
    </body>
    </html>
    )";

        server.send(200, "text/html", html);

        // Tampilkan pesan di OLED
        updateOLEDStatus("Koneksi Berhasil", "Restarting...");
        
        // Feedback sukses
        successBeep();
        blinkLED(LED_GREEN, 3, 200);
        
        // Tunggu sebentar agar response HTTP terkirim
        delay(2000);
        
        // Restart ESP32
        ESP.restart();
    }
    else
    {
        // Koneksi gagal, coba kembali ke jaringan sebelumnya
        updateOLEDStatus("Koneksi Gagal", "Kembali ke sebelumnya");
        WiFi.disconnect();

        if (!previousSSID.isEmpty())
        {
            WiFi.begin(previousSSID.c_str(), previousPassword.c_str());
            startAttemptTime = millis();

            while (millis() - startAttemptTime < WIFI_TIMEOUT)
            {
                if (WiFi.status() == WL_CONNECTED)
                {
                    connected = true;
                    break;
                }
                delay(500);
            }

            if (connected)
            {
                // Berhasil kembali ke jaringan sebelumnya
                String html = R"(
                <!DOCTYPE html>
                <html>
                <head>
                    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
                    <meta http-equiv='refresh' content='5;url=/'>
                    <title>Gagal Terhubung</title>
                    <style>
                        body { font-family: Arial; margin: 0; padding: 20px; background: #f0f0f0; }
                        .container { max-width: 500px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; }
                        .warning { color: #856404; background: #fff3cd; padding: 15px; border-radius: 4px; }
                    </style>
                </head>
                <body>
                    <div class='container'>
                        <div class='warning'>
                            <h2>Gagal Terhubung ke Jaringan Baru</h2>
                            <p>Kembali ke jaringan sebelumnya: )" +
                              previousSSID + R"(</p>
                        </div>
                        <p>Halaman akan dialihkan dalam 5 detik...</p>
                    </div>
                </body>
                </html>
                )";

                server.send(200, "text/html", html);
            }
            else
            {
                // Gagal total, masuk mode AP
                resetWiFiCredentials();
                String html = R"(
                <!DOCTYPE html>
                <html>
                <head>
                    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
                    <meta http-equiv='refresh' content='5;url=/'>
                    <title>Gagal Terhubung</title>
                    <style>
                        body { font-family: Arial; margin: 0; padding: 20px; background: #f0f0f0; }
                        .container { max-width: 500px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; }
                        .error { color: #721c24; background: #f8d7da; padding: 15px; border-radius: 4px; }
                    </style>
                </head>
                <body>
                    <div class='container'>
                        <div class='error'>
                            <h2>Gagal Terhubung</h2>
                            <p>Tidak dapat terhubung ke jaringan baru maupun sebelumnya.</p>
                            <p>Beralih ke mode AP...</p>
                        </div>
                    </div>
                </body>
                </html>
                )";

                server.send(200, "text/html", html);
                delay(1000);
                ESP.restart();
            }
        }
        else
        {
            // Tidak ada jaringan sebelumnya, langsung ke mode AP
            resetWiFiCredentials();
            String html = R"(
            <!DOCTYPE html>
            <html>
            <head>
                <meta name='viewport' content='width=device-width, initial-scale=1.0'>
                <meta http-equiv='refresh' content='5;url=/'>
                <title>Gagal Terhubung</title>
                <style>
                    body { font-family: Arial; margin: 0; padding: 20px; background: #f0f0f0; }
                    .container { max-width: 500px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; }
                    .error { color: #721c24; background: #f8d7da; padding: 15px; border-radius: 4px; }
                </style>
            </head>
            <body>
                <div class='container'>
                    <div class='error'>
                        <h2>Gagal Terhubung</h2>
                        <p>Tidak dapat terhubung ke jaringan baru.</p>
                        <p>Beralih ke mode AP...</p>
                    </div>
                </div>
            </body>
            </html>
            )";

            server.send(200, "text/html", html);
            delay(1000);
            ESP.restart();
        }
    }
}

void handleStatus()
{
    String status = "Tidak Terhubung";
    String ssid = "";
    String ip = "";
    String rssi = "";
    bool connected = false;

    if (WiFi.status() == WL_CONNECTED)
    {
        status = "Terhubung";
        ssid = WiFi.SSID();
        ip = WiFi.localIP().toString();
        rssi = String(WiFi.RSSI());
        connected = true;
    }
    else if (isAPMode)
    {
        status = "Mode AP";
        ssid = String(AP_SSID);
        ip = apIP.toString();
        rssi = "N/A";
    }

    String json = "{\"connected\":" + String(connected ? "true" : "false");
    json += ",\"status\":\"" + status + "\"";
    json += ",\"ssid\":\"" + ssid + "\"";
    json += ",\"ip\":\"" + ip + "\"";
    json += ",\"rssi\":\"" + rssi + "\"";
    json += "}";

    server.send(200, "application/json", json);
}

void handleReset()
{
    resetWiFiCredentials();
    server.send(200, "text/plain", "WiFi reset berhasil");
    delay(1000);
    ESP.restart();
}

// Fungsi EEPROM
void loadWiFiCredentials()
{
    wifiCred.ssid = readEEPROM(EEPROM_SSID_ADDR, 32);
    wifiCred.password = readEEPROM(EEPROM_PASS_ADDR, 64);
}

void saveWiFiCredentials(const String &ssid, const String &password)
{
    writeEEPROM(EEPROM_SSID_ADDR, ssid);
    writeEEPROM(EEPROM_PASS_ADDR, password);
    EEPROM.commit();
}

void checkWiFiConnection() {
    if (millis() - lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
        lastWiFiCheck = millis();

        if (!isAPMode && WiFi.status() != WL_CONNECTED) {
            // Immediately disable RFID
            mfrc522.PCD_AntennaOff();
            isProcessing = true; // Block RFID processing
            
            updateOLEDStatus("Koneksi Terputus", "RFID Dinonaktifkan");
            digitalWrite(LED_RED, HIGH);
            digitalWrite(LED_GREEN, LOW);
            errorBeep();

            WiFi.disconnect();
            WiFi.begin(wifiCred.ssid.c_str(), wifiCred.password.c_str());

            unsigned long startAttemptTime = millis();
            bool reconnected = false;
            int attemptCount = 0;
            const int MAX_ATTEMPTS = 3;

            while (attemptCount < MAX_ATTEMPTS && !reconnected) {
                startAttemptTime = millis();
                
                while (millis() - startAttemptTime < WIFI_TIMEOUT) {
                    if (WiFi.status() == WL_CONNECTED) {
                        updateOLEDStatus("WiFi Terhubung", "RFID Aktif Kembali");
                        digitalWrite(LED_RED, LOW);
                        digitalWrite(LED_GREEN, HIGH);
                        successBeep();
                        
                        // Re-enable RFID only after successful reconnection
                        mfrc522.PCD_AntennaOn();
                        isProcessing = false;
                        
                        reconnected = true;
                        setupWebServer();
                        break;
                    }
                    delay(500);
                }
                
                if (!reconnected) {
                    attemptCount++;
                    if (attemptCount < MAX_ATTEMPTS) {
                        updateOLEDStatus("Mencoba Koneksi", "Percobaan " + String(attemptCount + 1));
                        errorBeep();
                        delay(1000);
                    }
                }
            }

            if (!reconnected) {
                updateOLEDStatus("Gagal Terhubung", "Mode AP Aktif");
                resetWiFiCredentials();
                digitalWrite(LED_RED, LOW);
                digitalWrite(LED_GREEN, HIGH);
                
                // Ensure RFID remains disabled
                mfrc522.PCD_AntennaOff();
                isProcessing = true;
                
                delay(2000);
                ESP.restart();
            }
        }
    }
}

void resetWiFiCredentials()
{
    writeEEPROM(EEPROM_SSID_ADDR, "");
    writeEEPROM(EEPROM_PASS_ADDR, "");
    EEPROM.commit();
}

String readEEPROM(int startAddr, int maxLength)
{
    String data;
    for (int i = 0; i < maxLength; i++)
    {
        char c = EEPROM.read(startAddr + i);
        if (c == 0 || c == 255)
            break;
        data += c;
    }
    return data;
}

void writeEEPROM(int startAddr, const String &data)
{
    for (int i = 0; i < data.length(); i++)
    {
        EEPROM.write(startAddr + i, data[i]);
    }
    EEPROM.write(startAddr + data.length(), 0);
}

// =========================
// ======= LOOP HANDLER =======
// =========================
void handleWiFiLoop() {
    static unsigned long lastDisplayUpdate = 0;
    const unsigned long DISPLAY_UPDATE_INTERVAL = 5000;

    if (isAPMode) {
        dnsServer.processNextRequest();
        
        // Update tampilan OLED untuk mode AP
        if (millis() - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
            lastDisplayUpdate = millis();
            display.clearDisplay();
            display.setTextSize(1);
            display.setCursor(0, 0);
            display.println("Mode AP Aktif");
            display.println("----------------");
            display.println("SSID: " + String(AP_SSID));
            display.println("Pass: " + String(AP_PASSWORD));
            display.println("IP: " + apIP.toString());
            display.display();
        }
    } else {
        // Cek dan update status koneksi WiFi
        if (WiFi.status() == WL_CONNECTED) {
            if (millis() - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
                lastDisplayUpdate = millis();
                showDefaultOLEDDisplay();
            }
        } else {
            checkWiFiConnection();
        }
    }

    server.handleClient();
}

// =========================
// ======= SETUP & LOOP =======
// =========================

void setup()
{
    Serial.begin(115200);
    Wire.begin(33, 32);

    initLEDs();
    initBuzzer();
    initOLED();

    setAllLEDs(true);
    delay(500);
    setAllLEDs(false);
    successBeep();

    // Inisialisasi WiFi
    initWiFi();

    // Init RFID
    initRFID();

    // init Google Script
    setupGoogleApps();
}

void loop() {
    if (!isOTAInProgress) {
        // Check WiFi status first
        if (WiFi.status() != WL_CONNECTED && !isAPMode) {
            // Immediately disable RFID if WiFi disconnected
            if (!isProcessing) {
                mfrc522.PCD_AntennaOff();
                isProcessing = true;
                updateOLEDStatus("WiFi Terputus", "RFID Dinonaktifkan");
                digitalWrite(LED_RED, HIGH);
                errorBeep();
            }
        }

        handleWiFiLoop();
        updateLEDStatus(currentError);

        unsigned long currentMillis = millis();
        if (currentMillis - lastOLEDUpdate >= OLED_UPDATE_INTERVAL) {
            lastOLEDUpdate = currentMillis;
            checkAndUpdateWiFiStatus();
        }

        // Only run RFID and Google Apps if in STA mode and connected
        if (!isAPMode && WiFi.status() == WL_CONNECTED) {
            handleRFID();
            handleGoogleApps();
        }
    }
    
    server.handleClient();
    delay(100);
}
