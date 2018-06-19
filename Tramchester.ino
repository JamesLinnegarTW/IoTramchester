#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "time.h"
#include <Wire.h>  // Only needed for Arduino 1.6.5 and earlier
#include "SSD1306.h" // alias for `#include "SSD1306Wire.h"`
#include "OLEDDisplayUi.h"
#include "config.h"
#define TZ_OFFSET 3600
#include "images.h"

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
  Serial.println(F("---------\nCurrent Trams\n---------"));
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
  Serial.println(F("---------"));

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

  client.println(F("GET /api/departures/station/9400ZZMASHU?notes=0 HTTP/1.0"));
  client.println(F("Host: www.tramchester.com"));
  client.println(F("Connection: close"));
    
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

  Serial.println(F("status fine"));
    // Skip HTTP headers
  char endOfHeaders[] = "\r\n\r\n";
  if (!client.find(endOfHeaders)) {
    Serial.println(F("Invalid response"));
    return;
  }
  Serial.println(F("Headers skipped"));
  
 
 DynamicJsonBuffer jsonBuffer(capacity);

  JsonObject& root = jsonBuffer.parseObject(client);
  if (!root.success()) {
    Serial.println(F("Parsing failed!"));
    return;
  }

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
  Serial.println(F("closing connection"));
  tram_list_all();
  
}



void show_time()
{

  static unsigned long start_millis;
  
  // just the time
  struct tm tm;
  if (!getLocalTime(&tm))
  {
    Serial.println(F("Failed to obtain time"));
    return;
  }

  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_10);

  char buf[32];

  snprintf(buf, sizeof(buf),
           "%02d:%02d:%02d",
           tm.tm_hour + 1,
           tm.tm_min,
           tm.tm_sec
          );

  display.setFont(ArialMT_Plain_10);
  display.drawString(2, 14, buf);

}

int get_current_time(){
    int current_time = NULL;
    struct tm tm;
    
    if (!getLocalTime(&tm)) {
      Serial.println(F("Failed to obtain time"));
      return -1;
    } else {
      current_time = (tm.tm_hour * 60) + tm .tm_min; 
      return current_time;
    }
}

void draw_all_trams() {
    tram_t * t = tram_list;
    int current_time = get_current_time();
    int number_of_trams = 3;
    int trams_rendered = 0;   

    display.clear();
    while(t != NULL && trams_rendered <= number_of_trams){
     if(strcmp(t->status, "Departing") != 0){
        draw_tram(trams_rendered, t, current_time);
        trams_rendered++;
      }
      t = t->next;
    }
    display.display();
}

void draw_tram(int i, tram_t * t, int current_time){  
    const int TRAM_HEIGHT = 20; 
    int row_position = 0;
    char arrival_time[6] = {0};
    
    int_to_time(arrival_time, t->arrival);
    
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_10);
    
    if(current_time == NULL){
      display.drawString(0, ((i * TRAM_HEIGHT) +5), arrival_time);
    } else {
      int time_till_arrival = t->arrival - current_time;
      if(time_till_arrival < 0){
        time_till_arrival = 0;
      }
      display.drawString(0, ((i * TRAM_HEIGHT) +5), String(time_till_arrival)+"m");
    }
    display.drawString(25, ((i * TRAM_HEIGHT) +5), t->destination);
}

int Start_WiFi(const char* ssid, const char* password)
{
  int connAttempts = 0;
  Serial.println("\r\nConnecting to: " + String(ssid));
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED ) {
    delay(1000);
    Serial.print(".");
    
    if (connAttempts > 30)
      return -5;
    connAttempts++;
  }

  Serial.println(F("WiFi connected\r\n"));
  Serial.print(F("IP address: "));
  Serial.println(WiFi.localIP());
  return 1;
}

void draw_string(const char* message){
    display.clear();
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(DISPLAY_WIDTH / 2, (DISPLAY_HEIGHT / 2)-5, message);
    display.display();
}

/* 
 *  RUN
 */
void setup(){

  // Screen reset
  pinMode(16,OUTPUT);
  digitalWrite(16, LOW);    
  delay(50); 
  digitalWrite(16, HIGH); 

  Serial.begin(115200);
  
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);

  display.drawXbm(34, 0, tramchester_width, tramchester_height, tramchester_bits);
  display.display();
  
  //draw_string(BOOT_MESSAGE);
  delay(3000);
  draw_string(WIFI_CONNECTING);
  
  int wifi_status = Start_WiFi(ssid, password);

  if(wifi_status == -5){
    draw_string(WIFI_ERROR);
    while(true){}
  }
  
  configTime(1, 3600, "pool.ntp.org");
  draw_string(DATA_LOADING);
  load_trams();
}

void loop(){
  
  if(millis() - last_tram_load > load_interval){
    load_trams();
  } else {
    if(millis() - last_tram_render > render_interval){
      draw_all_trams();
      last_tram_render = millis();
    }
  }
  
}


