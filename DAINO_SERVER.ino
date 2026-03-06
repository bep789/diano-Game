#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>

WebServer server(80);

#define EEPROM_SIZE 8

// ===== WiFi =====
const char* ssid = "NAME_WIFI";
const char* password = "PASSWORD_WIFI";

// ===== Game State =====
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
const int gameInterval = 16; // ~60 FPS

// ================= GAME LOOP =================
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

  // collision
  for(int i=1;i<=2;i++){
    int dx = (i==1?60:120);

    if(cactusX < dx+30 && cactusX+20 > dx &&
       dinoY[i]+30 > 130){

      running = false;
      gameOver = true;
      winner = (i==1?2:1);
    }
  }
}

// ================= ROUTES =================

void handleState(){

  char json[512];  // ขยาย buffer

  snprintf(json,sizeof(json),
  "{\"p1y\":%d,"
  "\"p2y\":%d,"
  "\"cx\":%.1f,"
  "\"score\":%d,"
  "\"high\":%d,"
  "\"running\":%s,"
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
  running?"true":"false",
  gameOver?"true":"false",
  winner,
  playerName[1].c_str(),
  playerName[2].c_str(),
  playerCount
  );

  server.sendHeader("Cache-Control","no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma","no-cache");
  server.sendHeader("Expires","0");

  server.send(200,"application/json",json);
  server.client().stop();
}

void handleJump(){

  int p = server.arg("p").toInt();

  if(p >=1 && p<=2){
    if(!jumping[p] && running){
      velocity[p] = jumpForce;
      jumping[p] = true;
    }
  }

  server.send(200,"text/plain","OK");
  server.client().stop();
}

void handleStart(){

  if(playerCount >= 2){
    server.send(200,"text/plain","FULL");
    return;
  }

  playerCount++;

  if(playerCount == 1){
    playerName[1] = server.arg("name");
  } else {
    playerName[2] = server.arg("name");
  }

  if(playerCount == 2){

    score = 0;
    cactusX = 400;
    cactusSpeed = 6;

    dinoY[1]=groundY;
    dinoY[2]=groundY;
    velocity[1]=velocity[2]=0;
    jumping[1]=jumping[2]=false;

    running = true;
    gameOver = false;
    winner = 0;
  }

  server.send(200,"text/plain",String(playerCount));
  server.client().stop();
}

void handleReset(){

  score = 0;
  cactusX = 400;
  cactusSpeed = 6;

  running = false;
  gameOver = false;
  winner = 0;

  playerCount = 0;
  playerName[1] = "";
  playerName[2] = "";

  server.send(200,"text/plain","RESET");
  server.client().stop();
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
let started=false;
let myPlayer=0;

const canvas=document.getElementById("game");
const ctx=canvas.getContext("2d");

async function startGame(){
  const name=document.getElementById("name").value;
  const res=await fetch(`/start?name=${name}`);
  const text=await res.text();

  if(text=="FULL"){
    alert("Room Full");
    return;
  }

  myPlayer=parseInt(text);
  document.getElementById("nameScreen").style.display="none";
  started=true;
  loop();
}

function jump(){
  if(myPlayer!=0){
    fetch("/jump?p="+myPlayer);
  }
}

async function loop(){
  if(!started) return;

  const res=await fetch("/state",{cache:"no-store"});
  const data=await res.json();

  ctx.clearRect(0,0,400,200);
  ctx.fillRect(0,150,400,5);

  ctx.fillRect(60,data.p1y,30,30);
  ctx.fillRect(120,data.p2y,30,30);
  ctx.fillRect(data.cx,130,20,40);

  ctx.fillText("Score:"+data.score,10,20);
  ctx.fillText("High:"+data.high,10,40);

  if(data.count<2){
    ctx.fillText("Waiting for player...",120,80);
  }else{
    ctx.fillText(data.p1+" VS "+data.p2,100,60);
  }

  if(data.gameOver){
    alert(data.winner==1?data.p1+" Wins!":data.p2+" Wins!");
    fetch("/reset");
    started=false;
  }

  requestAnimationFrame(loop);
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
  WiFi.setSleep(false);   // ลด latency
  WiFi.begin(ssid,password);

  while(WiFi.status()!=WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnected!");
  Serial.println(WiFi.localIP());

  server.enableCORS(true);

  server.on("/",[](){
    server.send_P(200,"text/html",index_html);
  });

  server.on("/state",handleState);
  server.on("/jump",handleJump);
  server.on("/start",handleStart);
  server.on("/reset",handleReset);

  server.begin();
}

// ================= LOOP =================
void loop(){

  server.handleClient();
  updateGame();   // ⭐ แยกออกจาก HTTP
  yield();        // ⭐ ให้ WiFi task ทำงาน
}
