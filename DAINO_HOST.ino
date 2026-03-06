#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <EEPROM.h>

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

#define EEPROM_SIZE 8

// ===== WIFI AP =====
const char* ssid = "ESP32-Dino";
const char* password = "12345678";

// ===== GAME =====
int dinoY[3] = {0,110,110};
float velocity[3] = {0,0,0};
bool jumping[3] = {false,false,false};

float cactusX = 400;
float cactusSpeed = 6;

int score = 0;
int highScore = 0;

bool running = false;
bool gameOver = false;
int winner = 0;

String playerName[3] = {"","",""};
int playerCount = 0;

const int groundY = 110;
const float gravity = 0.6;
const float jumpForce = -10;

unsigned long lastGameUpdate = 0;
unsigned long lastBroadcast = 0;

const int gameInterval = 16;
const int broadcastInterval = 30;

// ================= GAME =================

void updateGame(){

  if(!running) return;
  if(playerCount < 2) return;

  if(millis() - lastGameUpdate < gameInterval) return;
  lastGameUpdate = millis();

  cactusSpeed = 6 + (score * 0.3);
  if(cactusSpeed > 18) cactusSpeed = 18;

  cactusX -= cactusSpeed;

  for(int i=1;i<=2;i++){
    velocity[i] += gravity;
    dinoY[i] += velocity[i];

    if(dinoY[i] >= groundY){
      dinoY[i] = groundY;
      velocity[i] = 0;
      jumping[i] = false;
    }
  }

  if(cactusX < -20){
    cactusX = 400;
    score++;

    if(score > highScore){
      highScore = score;
      EEPROM.writeInt(0, highScore);
      EEPROM.commit();
    }
  }

  for(int i=1;i<=2;i++){
    int dx = (i==1?60:120);

    if(cactusX < dx+30 && cactusX+20 > dx &&
       dinoY[i]+30 > 130){

      running=false;
      gameOver=true;
      winner=(i==1?2:1);
    }
  }
}

// ================= SEND STATE =================

void broadcastGame(){

  if(millis() - lastBroadcast < broadcastInterval) return;
  lastBroadcast = millis();

  char json[256];

  snprintf(json,sizeof(json),
  "{\"p1y\":%d,"
  "\"p2y\":%d,"
  "\"cx\":%.1f,"
  "\"score\":%d,"
  "\"high\":%d,"
  "\"gameOver\":%s,"
  "\"winner\":%d,"
  "\"p1\":\"%s\","
  "\"p2\":\"%s\","
  "\"count\":%d}",
  dinoY[1],
  dinoY[2],
  cactusX,
  score,
  highScore,
  gameOver?"true":"false",
  winner,
  playerName[1].c_str(),
  playerName[2].c_str(),
  playerCount
  );

  webSocket.broadcastTXT(json);
}

// ================= WEBSOCKET =================

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length){

  if(type == WStype_TEXT){

    String msg = (char*)payload;

    if(msg == "jump1"){
      if(!jumping[1] && running){
        velocity[1]=jumpForce;
        jumping[1]=true;
      }
    }

    if(msg == "jump2"){
      if(!jumping[2] && running){
        velocity[2]=jumpForce;
        jumping[2]=true;
      }
    }
  }
}

// ================= ROUTES =================

void handleStart(){

  if(playerCount>=2){
    server.send(200,"text/plain","FULL");
    return;
  }

  playerCount++;

  if(playerCount==1){
    playerName[1]=server.arg("name");
  }else{
    playerName[2]=server.arg("name");
  }

  if(playerCount==2){

    score=0;
    cactusX=400;

    dinoY[1]=groundY;
    dinoY[2]=groundY;

    velocity[1]=velocity[2]=0;
    jumping[1]=jumping[2]=false;

    running=true;
    gameOver=false;
    winner=0;
  }

  server.send(200,"text/plain",String(playerCount));
}

void handleReset(){

  score=0;
  cactusX=400;

  running=false;
  gameOver=false;
  winner=0;

  playerCount=0;
  playerName[1]="";
  playerName[2]="";

  server.send(200,"text/plain","OK");
}

// ================= HTML =================

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body{margin:0;text-align:center;font-family:Arial;background:#eee}
canvas{background:#fff;margin-top:10px}
button{padding:10px 20px;font-size:16px;margin:5px}
</style>
</head>
<body>

<h2>ESP32 Dino Battle</h2>

<div id="nameScreen">
<input id="name" placeholder="Your name">
<br>
<button onclick="startGame()">Join</button>
</div>

<canvas id="game" width="400" height="200"></canvas>
<br>
<button onclick="jump()">Jump</button>

<script>

let socket
let myPlayer=0
let started=false

const canvas=document.getElementById("game")
const ctx=canvas.getContext("2d")

async function startGame(){

 const name=document.getElementById("name").value
 const res=await fetch("/start?name="+name)
 const text=await res.text()

 if(text=="FULL"){
   alert("Room Full")
   return
 }

 myPlayer=parseInt(text)

 socket=new WebSocket("ws://"+location.hostname+":81/")

 socket.onmessage=function(event){

   const data=JSON.parse(event.data)

   ctx.clearRect(0,0,400,200)
   ctx.fillRect(0,150,400,5)

   ctx.fillRect(60,data.p1y,30,30)
   ctx.fillRect(120,data.p2y,30,30)
   ctx.fillRect(data.cx,130,20,40)

   ctx.fillText("Score:"+data.score,10,20)
   ctx.fillText("High:"+data.high,10,40)

   if(data.count<2){
     ctx.fillText("Waiting...",150,80)
   }else{
     ctx.fillText(data.p1+" VS "+data.p2,120,60)
   }

   if(data.gameOver){
     alert(data.winner==1?data.p1+" Wins":data.p2+" Wins")
     fetch("/reset")
     started=false
   }
 }

 document.getElementById("nameScreen").style.display="none"
 started=true
}

function jump(){

 if(!started) return

 if(myPlayer==1) socket.send("jump1")
 if(myPlayer==2) socket.send("jump2")
}

</script>
</body>
</html>
)rawliteral";

// ================= SETUP =================

void setup(){

  Serial.begin(115200);

  EEPROM.begin(EEPROM_SIZE);
  highScore = EEPROM.readInt(0);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid,password);

  Serial.println(WiFi.softAPIP());

  server.on("/",[](){
    server.send_P(200,"text/html",index_html);
  });

  server.on("/start",handleStart);
  server.on("/reset",handleReset);

  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

// ================= LOOP =================

void loop(){

  server.handleClient();
  webSocket.loop();

  updateGame();
  broadcastGame();

  yield();
}
