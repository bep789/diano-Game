#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <EEPROM.h>

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

#define EEPROM_SIZE 8

// ===== WIFI =====
const char* ssid = "NAME_WIFI";
const char* password = "PASSWORD_WIFI";

// ===== GAME =====
int dinoY[3] = {0,110,110};
float velocity[3] = {0,0,0};
bool jumping[3] = {false,false,false};

float cactusX = 400;
float cactusSpeed = 6;

int score = 0;
int highScore = 0;

bool running=false;
bool gameOver=false;
int winner=0;

String playerName[3] = {"","",""};
int playerCount=0;

const int groundY=110;
const float gravity=0.6;
const float jumpForce=-10;

unsigned long lastGameUpdate=0;
const int gameInterval=16; // 60 FPS

// ================= GAME LOOP =================

void updateGame(){

  if(!running) return;
  if(playerCount<2) return;

  if(millis()-lastGameUpdate<gameInterval) return;
  lastGameUpdate=millis();

  cactusSpeed = 6 + score*0.3;
  if(cactusSpeed>18) cactusSpeed=18;

  cactusX -= cactusSpeed;

  for(int i=1;i<=2;i++){

    velocity[i]+=gravity;
    dinoY[i]+=velocity[i];

    if(dinoY[i]>=groundY){
      dinoY[i]=groundY;
      velocity[i]=0;
      jumping[i]=false;
    }
  }

  if(cactusX<-20){

    cactusX=400;
    score++;

    if(score>highScore){
      highScore=score;
      EEPROM.writeInt(0,highScore);
      EEPROM.commit();
    }
  }

  // collision
  for(int i=1;i<=2;i++){

    int dx=(i==1?60:120);

    if(cactusX<dx+30 && cactusX+20>dx &&
       dinoY[i]+30>130){

      running=false;
      gameOver=true;
      winner=(i==1?2:1);
    }
  }
}

// ================= SEND GAME STATE =================

void sendState(){

  char json[256];

  snprintf(json,sizeof(json),
  "{\"p1y\":%d,"
  "\"p2y\":%d,"
  "\"cx\":%.1f,"
  "\"score\":%d,"
  "\"high\":%d,"
  "\"running\":%d,"
  "\"gameOver\":%d,"
  "\"winner\":%d,"
  "\"p1\":\"%s\","
  "\"p2\":\"%s\","
  "\"count\":%d}",
  dinoY[1],
  dinoY[2],
  cactusX,
  score,
  highScore,
  running,
  gameOver,
  winner,
  playerName[1].c_str(),
  playerName[2].c_str(),
  playerCount
  );

  webSocket.broadcastTXT(json);
}

// ================= WEBSOCKET =================

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length){

  if(type==WStype_TEXT){

    String msg=(char*)payload;

    if(msg.startsWith("jump")){

      int p=msg.substring(4).toInt();

      if(!jumping[p] && running){
        velocity[p]=jumpForce;
        jumping[p]=true;
      }
    }

    if(msg.startsWith("join")){

      if(playerCount>=2) return;

      playerCount++;

      String name=msg.substring(4);

      if(playerCount==1) playerName[1]=name;
      else playerName[2]=name;

      if(playerCount==2){

        score=0;
        cactusX=400;
        cactusSpeed=6;

        dinoY[1]=groundY;
        dinoY[2]=groundY;

        velocity[1]=velocity[2]=0;
        jumping[1]=jumping[2]=false;

        running=true;
        gameOver=false;
        winner=0;
      }
    }

    if(msg=="reset"){

      score=0;
      cactusX=400;

      running=false;
      gameOver=false;
      winner=0;

      playerCount=0;
      playerName[1]="";
      playerName[2]="";
    }
  }
}

// ================= HTML =================

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>

body{
margin:0;
text-align:center;
font-family:Arial;
background:#eee
}

canvas{
background:#fff;
margin-top:10px
}

button{
padding:10px 20px;
font-size:16px
}

</style>
</head>

<body>

<h2>ESP32 Dino Battle</h2>

<div id="menu">

<input id="name" placeholder="your name">
<br><br>

<button onclick="join()">Join</button>

</div>

<canvas id="game" width="400" height="200"></canvas>
<br>
<button onclick="jump()">Jump</button>

<script>

let ws
let player=0

const canvas=document.getElementById("game")
const ctx=canvas.getContext("2d")

function join(){

 const name=document.getElementById("name").value

 ws=new WebSocket("ws://"+location.hostname+":81")

 ws.onopen=()=>{
   ws.send("join"+name)
 }

 ws.onmessage=(event)=>{

  const data=JSON.parse(event.data)

  ctx.clearRect(0,0,400,200)

  ctx.fillRect(0,150,400,5)

  ctx.fillRect(60,data.p1y,30,30)
  ctx.fillRect(120,data.p2y,30,30)

  ctx.fillRect(data.cx,130,20,40)

  ctx.fillText("Score:"+data.score,10,20)
  ctx.fillText("High:"+data.high,10,40)

  if(data.count<2){
   ctx.fillText("Waiting Player...",120,80)
  }else{
   ctx.fillText(data.p1+" VS "+data.p2,100,60)
  }

  if(data.gameOver){

   alert(data.winner==1?data.p1+" Wins!":data.p2+" Wins!")

   ws.send("reset")

  }

 }

 setInterval(()=>{
  ws.send("ping")
 },1000)

}

function jump(){

 if(ws){
  ws.send("jump"+player)
 }

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

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid,password);

  while(WiFi.status()!=WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnected");
  Serial.println(WiFi.localIP());

  server.on("/",[](){
    server.send_P(200,"text/html",index_html);
  });

  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

// ================= LOOP =================

void loop(){

  server.handleClient();
  webSocket.loop();

  updateGame();

  static unsigned long lastSend=0;

  if(millis()-lastSend>16){
    lastSend=millis();
    sendState();
  }
}
