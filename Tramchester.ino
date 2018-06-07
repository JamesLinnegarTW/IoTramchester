#include "WiFi.h"
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Wire.h>  // Only needed for Arduino 1.6.5 and earlier
#include "SSD1306.h" // alias for `#include "SSD1306Wire.h"`
#include "OLEDDisplayUi.h"
#include "config.h"

SSD1306  display(0x3c, 4, 15);
OLEDDisplayUi ui( &display );

WiFiClientSecure client; // wifi client object

    
// linked list of train structures
struct tram_t {
  tram_t * next;  // pointer to a tram
  char status;
  char carriages; 
  char destination[32];
  char when[6];
};

static tram_t * tram_list;


tram_t * tram_create()
{
  tram_t * t = (tram_t*) calloc(1, sizeof(*t));
  t->status = '?';
  t->carriages = '?';
  t->next = NULL;
  return t;
}

tram_t * tram_find(int index){
  int tramIndex = 0;
  tram_t * t = tram_list;
  while(t){
    if(tramIndex == index)
      return t;
    t = t->next;
  }

  return NULL;
  
}
void tram_insert(tram_t * nt)
{
  tram_t **t = &tram_list;
  while (*t)
  {
    t = &(*t)->next;
  }

  // found our spot or the list is empty
  nt->next = *t;
  *t = nt;
}

int Start_WiFi(const char* ssid, const char* password)
{
  int connAttempts = 0;
  Serial.println("\r\nConnecting to: " + String(ssid));
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED ) {
    delay(500);
    Serial.print(".");
    if (connAttempts > 20)
      return -5;
    connAttempts++;
  }

  Serial.println("WiFi connected\r\n");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  return 1;
}



void load_trams() {
  // put your main code here, to run repeatedly:
  const char* host = "www.tramchester.com";
  const int httpPort = 443;
  Serial.print("Connecting to ");
  Serial.println(host);
  

  //client.setCACert(test_root_ca);
  if(!client.connect(host, httpPort)) {
    Serial.println("connection failed");
    return;
  }

  client.println("GET /api/departures/station/9400ZZMASHU?notes=0 HTTP/1.0");
  client.println("Host: www.tramchester.com");
  client.println("Connection: close");
    
  if(client.println() == 0) {
    Serial.println(F("Failed to send request"));
  }

    // Check HTTP status
  char status[32] = {0};

  client.readBytesUntil('\r', status, sizeof(status));
  if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
    Serial.println(F("Unexpected response: "));
    Serial.println(status);
    Serial.println(F("-----"));
    return;
  }

  Serial.println("status fine");
    // Skip HTTP headers
  char endOfHeaders[] = "\r\n\r\n";
  if (!client.find(endOfHeaders)) {
    Serial.println(F("Invalid response"));
    return;
  }
  Serial.println("Headers skipped");
  
 const size_t capacity = JSON_ARRAY_SIZE(0) + JSON_ARRAY_SIZE(6) + JSON_OBJECT_SIZE(2) + 6*JSON_OBJECT_SIZE(5) + 60;
 

 DynamicJsonBuffer jsonBuffer(capacity);

  JsonObject& root = jsonBuffer.parseObject(client);
  if (!root.success()) {
    Serial.println(F("Parsing failed!"));
    return;
  }


    // Extract values

  int numberOfTrams = root["departures"].size();
  Serial.println(numberOfTrams);

  for(int i = 0; i < numberOfTrams; i++){
    JsonObject &tram = root["departures"][i];

    const char * destination = tram["destination"];
    const char * when = tram["when"];
  
    tram_t * t = tram_create();
  //  t->status = tram["status"][0];
  //  t->carriages = tram["carriages"][0];
    strlcpy(t->destination, destination, sizeof(t->destination));
    strlcpy(t->when, when, sizeof(t->when));
    tram_insert(t);
  }
  // Disconnect

  jsonBuffer.clear();
  client.stop();
  
  Serial.println();
  Serial.println("closing connection");
  
             
}


void draw_tram(tram_t *t, OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(ArialMT_Plain_10);
    display->drawString(x+2, y+50, t->when);
    display->drawString(x+37, y+50, t->destination);
}




void drawFrame1(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
   tram_t * t = tram_find(0);
   draw_tram(t, display, state, x, y);
}

void drawFrame2(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
   tram_t * t = tram_find(1);
   draw_tram(t, display, state, x, y);
}

void draw_next_tram(OLEDDisplay *display, OLEDDisplayUiState* state) {
   tram_t * t = tram_list;

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(ArialMT_Plain_10);
    display->drawString(2, 2, t->when);
    display->setFont(ArialMT_Plain_16);
    display->drawString(2, 14, t->destination);
}

FrameCallback frames[] = { drawFrame1, drawFrame2};
int frameCount = 2;


OverlayCallback overlays[] = { draw_next_tram };
const int overlaysCount = 1;

void setup(){
  // put your setup code here, to run once:
  pinMode(16,OUTPUT);
  digitalWrite(16, LOW);    // set GPIO16 low to reset OLED
  delay(50); 
  digitalWrite(16, HIGH); // while OLED is running, must set GPIO16 in high
  
  ui.setTargetFPS(30);
  ui.disableAllIndicators();
  ui.setFrameAnimation(SLIDE_LEFT);

  // Add frames
  ui.setFrames(frames, frameCount);
  ui.setOverlays(overlays, overlaysCount);

  // Initialising the UI will init the display too.
  ui.init();
  
  Serial.begin(115200);


  Start_WiFi(ssid, password);
  load_trams();
}


void draw_trams() {
    tram_t * t = tram_list;
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_10);
    int y = 0;
    while (t)
    {
          display.drawString(0, y, t->when);
          display.drawString(40, y, t->destination);
      t = t->next;
      y = y + 22;
    }
  
    display.display();
}


void loop(){
  
    int remainingTimeBudget = ui.update();

  if (remainingTimeBudget > 0) {
    // You can do some work here
    // Don't do stuff if you are below your
    // time budget.
    delay(remainingTimeBudget);
  }
  
  
}


