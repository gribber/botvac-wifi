#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WebSocketsServer.h>
#include <FS.h>
#include <ESP8266HTTPUpdateServer.h>

#define FIRMWARE_VERSION "1.3"

#define SSID_FILE "etc/ssid"
#define PASSWORD_FILE "etc/pass"
#define HOSTNAME_FILE "etc/wifihostname"
#define SSID_MAX 32
#define PASSWD_MAX 64
#define HOSTNAME_MAX 32


#define CONNECT_TIMEOUT_SECS 30

#define AP_SSID "neato"

#define MAX_BUFFER 8192

char ssid[SSID_MAX+1];
char passwd[PASSWD_MAX+1];
char hostname[SSID_MAX+1];
int bufferSize = 0;
uint8_t currentClient = 0;
uint8_t serialBuffer[8193];
ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
ESP8266WebServer updateServer(82);
ESP8266HTTPUpdateServer httpUpdater;

void botDissconect() {
  // always disable testmode on disconnect
  Serial.println("TestMode off");
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) {
  switch (type) {
    case WStype_DISCONNECTED:
      // always disable testmode on disconnect
      botDissconect();
      break;
    case WStype_CONNECTED:
      webSocket.sendTXT(num, "connected to Neato");
      // allow only one concurrent client connection
      currentClient = num;
      // all clients but the last connected client are disconnected
      for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
        if (i != currentClient) {
          webSocket.disconnect(i);
        }
      }
      // reset serial buffer on new connection to prevent garbage
      serialBuffer[0] = '\0';
      bufferSize = 0;
      break;
    case WStype_TEXT:
      // send incoming data to bot
      Serial.printf("%s\n", payload);
      break;
    case WStype_BIN:
      webSocket.sendTXT(num, "binary transmission is not supported");
      break;
  }
}

void serverEvent() {
  // just a very simple websocket terminal, feel free to use a custom one
  server.send(200, "text/html", "<!DOCTYPE html><meta charset='utf-8' /><style>p{white-space:pre;word-wrap:break-word;font-family:monospace;}</style><title>Neato Console</title><script language='javascript' type='text/javascript'>var b='ws://'+location.hostname+':81/',c,d,e;function g(){d=new WebSocket(b);d.onopen=function(){h('[connected]')};d.onclose=function(){h('[disconnected]')};d.onmessage=function(a){h('<span style=\"color: blue;\">[response] '+a.data+'</span>')};d.onerror=function(a){h('<span style=\"color: red;\">[error] </span> '+a.data)}}\nfunction k(a){if(13==a.keyCode){a=e.value;if('/disconnect'==a)d.close();else if('/clear'==a)for(;c.firstChild;)c.removeChild(c.firstChild);else''!=a&&(h('[sent] '+a),d.send(a));e.value='';e.focus()}}function h(a){var f=document.createElement('p');f.innerHTML=a;c.appendChild(f);window.scrollTo(0,document.body.scrollHeight)}\nwindow.addEventListener('load',function(){c=document.getElementById('c');e=document.getElementById('i');g();document.getElementById('i').addEventListener('keyup',k,!1);e.focus()},!1);</script><h2>Neato Console</h2><div id='c'></div><input type='text' id='i' style=\"width:100%;font-family:monospace;\">\n");
}

void setupEvent() {
  server.send(200, "text/html", String() +
  "<!DOCTYPE html><html> <body>" +
  "<p>Neato Botvac 85 connected (IP: " + WiFi.localIP().toString() + ", FW: " + FIRMWARE_VERSION + ")</p>" +
  "<form action=\"\" method=\"post\" style=\"display: inline;\">" +
  "Access Point SSID:<br />" +
  "<input type=\"text\" name=\"ssid\" value=\"" + ssid + "\"> <br />" +
  "WPA2 Password:<br />" +
  "<input type=\"text\" name=\"password\" value=\"" + passwd + "\"> <br />" +
  "WIFI hostname:<br />" +
  "<input type=\"text\" name=\"hostname\" value=\"" + hostname + "\"> <br />" +
  "<br />" +
  "<input type=\"submit\" value=\"Submit\"> </form>" +
  "<form action=\"http://neato.local/reboot\" style=\"display: inline;\">" +
  "<input type=\"submit\" value=\"Reboot\" />" +
  "</form>" +
  "<p>Enter the details for your access point. After you submit, the controller will reboot to apply the settings.</p>" +
  "<p><a href=\"http://neato.local:82/update\">Update Firmware</a></p>" +
  "<p><a href=\"http://neato.local/console\">Neato Serial Console</a> - <a href=\"https://github.com/gribber/botvac-wifi/blob/master/doc/programmersmanual_20140305.pdf\">Command Documentation</a></p>" +
  "</body></html>\n");
}

void saveEvent() {
  String user_ssid = server.arg("ssid");
  String user_password = server.arg("password");
  String user_hostname = server.arg("hostname");
  SPIFFS.format();
  SPIFFS.begin();
  if(user_ssid != "" && user_password != "" && user_hostname != "") {
    File ssid_file = SPIFFS.open(SSID_FILE, "w");
    if (!ssid_file) {
      server.send(200, "text/html", "<!DOCTYPE html><html> <body> Setting Access Point SSID failed!</body> </html>");
      return;
    }
    ssid_file.print(user_ssid);
    ssid_file.close();
    File passwd_file = SPIFFS.open(PASSWORD_FILE, "w");
    if (!passwd_file) {
      server.send(200, "text/html", "<!DOCTYPE html><html> <body> Setting Access Point password failed!</body> </html>");
      return;
    }
    passwd_file.print(user_password);
    passwd_file.close();
    File hostname_file = SPIFFS.open(HOSTNAME_FILE, "w");
    if (!hostname_file) {
      server.send(200, "text/html", "<!DOCTYPE html><html> <body> Setting Access Point hostname failed!</body> </html>");
      return;
    }
    hostname_file.print(user_hostname);
    hostname_file.close();

    server.send(200, "text/html", String() +
    "<!DOCTYPE html><html> <body>" +
    "Setting Access Point SSID / password was successful! <br />" +
    "<br />SSID was set to \"" + user_ssid + "\" with the password \"" + user_password + "\" and hostname \"" + user_hostname + "\". <br />" +
    "<br /> The controller will now reboot. Please re-connect to your Wi-Fi network.<br />" +
    "If the SSID or password was incorrect, the controller will return to Access Point mode." +
    "</body> </html>");
    ESP.reset();
  }
}

void rebootEvent() {
  server.send(200, "text/html", String() +
  "<!DOCTYPE html><html> <body>" +
  "The controller will now reboot.<br />" +
  "If the SSID or password is set but is incorrect, the controller will return to Access Point mode." +
  "</body> </html>");
  ESP.reset();
}

void serialEvent() {
  while (Serial.available() > 0) {
    char in = Serial.read();
    // there is no propper utf-8 support so replace all non-ascii 
    // characters (<127) with underscores; this should have no 
    // impact on normal operations and is only relevant for non-english 
    // plain-text error messages
    if (in > 127) {
      in = '_';
    }
    serialBuffer[bufferSize] = in;
    bufferSize++;
    // fill up the serial buffer until its max size (8192 bytes, see MAX_BUFFER)
    // or unitl the end of file marker (ctrl-z; \x1A) is reached
    // a worst caste lidar result should be just under 8k, so that MAX_BUFFER
    // limit should not be reached under normal conditions
    if (bufferSize > MAX_BUFFER - 1 || in == '\x1A') {
      serialBuffer[bufferSize] = '\0';
      bool allSend = false;
      uint8_t localBuffer[1464];
      int localNum = 0;
      int bufferNum = 0;
      while (!allSend) {
        localBuffer[localNum] = serialBuffer[bufferNum];
        localNum++;
        // split the websocket packet in smaller (1300 + x) byte packets
        // this is a workaround for some issue that causes data corruption 
        // if the payload is split by the wifi library into more than 2 tcp packets
        if (serialBuffer[bufferNum] == '\x1A' || (serialBuffer[bufferNum] == '\n' && localNum > 1300)) {
          localBuffer[localNum] = '\0';
          localNum = 0;
          webSocket.sendTXT(currentClient, localBuffer);
        }
        if (serialBuffer[bufferNum] == '\x1A') {
          allSend = true;
        }
        bufferNum++;
      }
      serialBuffer[0] = '\0';
      bufferSize = 0;
    }
  }
}

void setup() {
  // start serial
  // botvac serial console is 115200 baud, 8 data bits, no parity, one stop bit (8N1)
  Serial.begin(115200);

  //try to mount the filesystem. if that fails, format the filesystem and try again.
  if(!SPIFFS.begin()) {
    SPIFFS.format();
    SPIFFS.begin();
  }

  if(SPIFFS.exists(SSID_FILE) && SPIFFS.exists(PASSWORD_FILE)) {
    File ssid_file = SPIFFS.open(SSID_FILE, "r");
    ssid_file.readString().toCharArray(ssid, SSID_MAX);
    ssid_file.close();
    File passwd_file = SPIFFS.open(PASSWORD_FILE, "r");
    passwd_file.readString().toCharArray(passwd, PASSWD_MAX);
    passwd_file.close();
    File hostname_file = SPIFFS.open(HOSTNAME_FILE, "r");
    hostname_file.readString().toCharArray(hostname, HOSTNAME_MAX);
    hostname_file.close();
    SPIFFS.end();

    // attempt station connection
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.hostname(hostname);
    WiFi.begin(ssid, passwd);
    // clobber the password
    strcpy(passwd, "********");
    for(int i = 0; i < CONNECT_TIMEOUT_SECS * 20 && WiFi.status() != WL_CONNECTED; i++) {
      delay(50);
    }
  }


  //start AP mode if either the AP / password do not exist, or cannot be connected to within CONNECT_TIMEOUT_SECS seconds.
  if(WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    if(! WiFi.softAP(AP_SSID)) {
      ESP.reset(); //reset because there's no good reason for setting up an AP to fail
    }
  }

  // start websocket
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  httpUpdater.setup(&updateServer);
  updateServer.begin();

  // start webserver
  server.on("/console", serverEvent);
  server.on("/", HTTP_POST, saveEvent);
  server.on("/", HTTP_GET, setupEvent);
  server.on("/reboot", HTTP_GET, rebootEvent);
  server.onNotFound(serverEvent);
  server.begin();

  // start MDNS
  // this means that the botvac can be reached at http://neato.local or ws://neato.local:81
  if (!MDNS.begin("neato")) {
    ESP.reset(); //reset because there's no good reason for setting up MDNS to fail
  }
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("ws", "tcp", 81);
  MDNS.addService("http", "tcp", 82);
}

void loop() {
  webSocket.loop();
  server.handleClient();
  updateServer.handleClient();
  serialEvent();
}
