#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "time.h"
#include <Wire.h>  // Only needed for Arduino 1.6.5 and earlier
#include "SSD1306.h" // alias for `#include "SSD1306Wire.h"`
#include "OLEDDisplayUi.h"
#include "config.h"
#define TZ_OFFSET 3600

unsigned long last_tram_load = 0;
unsigned int load_interval = 20000;


unsigned long last_tram_render = 0;
unsigned int render_interval = 500;

SSD1306  display(0x3c, 4, 15);
WiFiClientSecure client; // wifi client object

time_t last_update_sec;
    
// linked list of train structures
typedef struct tram {
  tram * next;  // pointer to a tram
  char status[10];
  char carriages; 
  char destination[32];
  int  arrival;
} tram_t;

tram_t * tram_list;


int time_to_int(char * s){

  int hour = 0;
  int minute = 0;
  hour = atoi(s);
  Serial.println(hour);
  minute = atoi(s+4);
  Serial.println(s);
  return ((hour * 60)+minute);
}

void int_to_time(char * str, int time){
  sprintf(str, "%02d:%02d", ((time - (time%60))/60), time %60);
}

tram_t * tram_create(int arrival){
  tram_t * t = (tram_t*) calloc(1, sizeof(*t));
  t->arrival = arrival;
  t->next = NULL;
  t->carriages = '?';
  return t;
}

bool trams_are_equal(tram_t * trama, tram_t * tramb){
  return (trama->arrival == tramb->arrival) && (strcmp(trama->destination, tramb->destination)==0);
}

void tram_insert(tram_t * nt){
  tram_t **t = &tram_list;
  while(*t){
    if(trams_are_equal((*t), nt)){
      free(nt); // bin the assigned tram
      nt = NULL; // required?
      return;
    }
    if((*t)->arrival > nt->arrival)
      break;

    t = &(*t)->next;
  }
  nt->next = *t;
  *t = nt;
}

int pop(){
  tram_t * current = tram_list;
  tram_t * next_node = NULL;

  int retval = -1;

  if(current == NULL){
    return retval;
  }
  next_node = current->next;
  free(current);
  tram_list = next_node;
  return 1;
}

int tram_remove(int search_index){
    tram_t * current = tram_list;
    tram_t * previous = NULL;
    search_index--;
    int retval = -1;
    if(search_index == 0){
      return pop();
    } else {

      int index = 0;
      while(current != NULL){
        if(index == search_index)
          break;

        previous = current;
        current = current->next;
        index++;
      }

      if(current != NULL){
        if(current->next != NULL){
          previous->next = current->next;
        } else {
          previous->next = NULL;
        }
        free(current);
        return 1;
      }
    }
    return retval;
}

void tram_remove_all(){
  Serial.println("remove all trams");
  tram_t * current = tram_list;
  tram_t * temp_node = NULL;

  while (current != NULL) {
      temp_node = current;
      free(current);
      current = temp_node->next;
      temp_node = NULL;
  }
  
  tram_list = NULL;
}


tram_t * tram_find(int search_index){
    tram_t * current = tram_list;
    search_index--; // 0index
    int index = 0;
    while(current != NULL){
      if(index == search_index)
        break;
      current = current->next;
      index++;
    }
    return current;
}

void tram_list_all(){
  tram_t * current = tram_list;
  Serial.println("---------\nCurrent Trams\n---------");
  while(current != NULL){
    static char arrival_time[6] = {0};
    int_to_time(arrival_time,current->arrival);
    
    Serial.print(arrival_time);
    Serial.print(" ");
    Serial.print(current->destination);
    Serial.print(" ");
    Serial.println(current->status);
    
    current = current->next;
  }
  Serial.println("---------");

}

void load_trams() {
  // put your main code here, to run repeatedly:
  last_tram_load = millis();   
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
  tram_remove_all();
  for(int i = 0; i < numberOfTrams; i++){
    JsonObject &tram = root["departures"][i];  

    const char * when = tram["when"];
    const char * status = tram["status"];
    const char * carriages = tram["carriages"];
    
    int hour = atoi(when);
    int minute = atoi(when+3);
    int time = ((hour * 60)+minute);
    
    const char * destination = tram["destination"];
    
    tram_t * t = tram_create(time);
    t->carriages = tram["carriages"][0];
    strlcpy(t->destination, destination, sizeof(t->destination)); 
    strlcpy(t->status, status, sizeof(t->status));  
    tram_insert(t);
    
  }
  
  // Disconnect

  jsonBuffer.clear();
  client.stop();
  
  Serial.println();
  Serial.println("closing connection");
  
}


/*
void show_time(OLEDDisplay *display, OLEDDisplayUiState* state)
{

  static unsigned long start_millis;

  if (start_millis == 0)
    start_millis = millis();
  else if (millis() - start_millis > 1500
           && last_update_sec != 0)
  {
    start_millis = 0;
    ui.nextFrame();
  }
  
  // just the time
  struct tm tm;
  if (!getLocalTime(&tm))
  {
    Serial.println("Failed to obtain time");
    return;
  }

  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);

  char buf[32];

  snprintf(buf, sizeof(buf),
           "%02d:%02d:%02d",
           tm.tm_hour + 1,
           tm.tm_min,
           tm.tm_sec
          );

  display->setFont(ArialMT_Plain_10);
  display->drawString(2, 14, buf);

}
*/


void draw_next_tram() {
    tram_t * t = NULL;
    display.clear();
    for(int i = 0; i <= 4; i++){
       t = tram_find(i);
       if(t != NULL) { 
          draw_tram(i, t);
       }
    }
    display.display();
}

void draw_tram(int i, tram_t * t){  
    bool is_arriving = strcmp(t->status, "Arriving") == 0;
    char arrivalTime[6] = {0};
    int_to_time(arrivalTime, t->arrival);

    display.setColor((is_arriving)?WHITE:BLACK);
    display.fillRect(0, (i*14)-12, DISPLAY_WIDTH, (i*14)+2);
    display.setColor((is_arriving)? BLACK:WHITE); // alternate colors
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_10);
    display.drawString(2, (i * 14)-12, arrivalTime);
    display.drawString(35, (i*14)-12, t->destination);
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


/* 
 *  RUN
 */
void setup(){
  // put your setup code here, to run once:
  pinMode(16,OUTPUT);
  digitalWrite(16, LOW);    // set GPIO16 low to reset OLED
  delay(50); 
  digitalWrite(16, HIGH); // while OLED is running, must set GPIO16 in high

  Serial.begin(115200);
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);

  Start_WiFi(ssid, password);
  configTime(1, 3600, "pool.ntp.org");
  load_trams();
}

void loop(){
  
  if(millis() - last_tram_load > load_interval){
    load_trams();
    tram_list_all();
  } else {
    if(millis() - last_tram_render > render_interval){
      draw_next_tram();
      last_tram_render = millis();
    }
  }
  
}


