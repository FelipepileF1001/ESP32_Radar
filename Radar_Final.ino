#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include "pitches.h"

// ======================= PINOS =======================
#define buzzer     14   // Buzzer PASSIVO (+) -> GPIO14, (-) -> GND
#define LED        12   // LED vermelho (objeto ENCONTRADO) -> resistor 220R -> GND
#define LED2        2   // LED verde    (NENHUM objeto)     -> resistor 220R -> GND
#define PIN_TRIG   25   // HC-SR04 TRIG
#define PIN_ECHO   26   // HC-SR04 ECHO (use divisor de tensão 5V -> 3.3V!)
#define PIN_SERVO  13   // SG90 sinal (fio laranja/amarelo)

// ======================= WI-FI =======================
const char* ssid_ap  = "ESP32_Sensor";
const char* senha_ap = "12345678";


WebServer server(80);
Servo servo;

// ======================= PARÂMETROS =======================
const float DISTANCIA_LIMITE = 30.0;  // cm: abaixo disso = objeto encontrado
const int   PASSO_GRAUS      = 3;     // avanço do servo a cada leitura
const unsigned long INTERVALO_MS = 60; // tempo entre leituras/passos
const unsigned long HOLD_MS      = 400; // mantém alarme ativo após última detecção

// ======================= ESTADO =======================
int   anguloAtual = 90;
int   direcao = 1;
float distanciaAtual = 0.0;
bool  leituraValida = false;
bool  objetoNoAngulo = false;

unsigned long ultimaLeitura = 0;
unsigned long ultimoDetectado = 0;
bool alarmeAtivo = false;

unsigned long buzzerDesligaEm = 0;
unsigned long proximoBipObjeto = 0;

// ======================= BUZZER (não bloqueante) =======================
void bip(unsigned int freq, unsigned long ms) {
  tone(buzzer, freq);
  buzzerDesligaEm = millis() + ms;
}

// ======================= PÁGINA 1: LEITURAS =======================
const char PAGINA_INICIO[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width, initial-scale=1.0'>
<title>Monitor ESP32</title>
<style>
body{font-family:Arial,sans-serif;text-align:center;background:#121212;color:#fff;margin-top:30px}
.card{background:#1e1e1e;border-radius:15px;padding:20px;margin:15px auto;max-width:320px;box-shadow:0 4px 8px rgba(0,0,0,.5)}
.valor{font-size:40px;font-weight:bold;color:#00e676;margin:8px 0}
.rotulo{font-size:16px;color:#aaa}
a.btn{display:inline-block;margin-top:10px;padding:14px 28px;background:#00e676;color:#000;font-weight:bold;border-radius:10px;text-decoration:none}
</style>
<script>
setInterval(()=>{fetch('/dados').then(r=>r.json()).then(d=>{
 document.getElementById('dist').innerText=d.valida?d.distancia.toFixed(1)+' cm':'Fora de alcance';
 document.getElementById('ang').innerText=d.angulo+' °';
 const s=document.getElementById('st');
 s.innerText=d.alarme?'OBJETO ENCONTRADO':'Nenhum objeto';
 s.style.color=d.alarme?'#ff1744':'#00e676';
});},300);
</script></head><body>
<h1>Leitura do Sensor</h1>
<div class='card'><div class='rotulo'>DISTÂNCIA</div><div class='valor' id='dist'>--</div></div>
<div class='card'><div class='rotulo'>ANGULAÇÃO DO SERVO</div><div class='valor' id='ang'>--</div></div>
<div class='card'><div class='rotulo'>STATUS</div><div class='valor' id='st' style='font-size:24px'>--</div></div>
<a class='btn' href='/radar'>Abrir Radar</a>
</body></html>
)rawliteral";

// ======================= PÁGINA 2: RADAR 180° =======================
const char PAGINA_RADAR[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width, initial-scale=1.0'>
<title>Radar ESP32</title>
<style>
body{font-family:Arial,sans-serif;text-align:center;background:#000;color:#0f0;margin:0;padding:10px}
canvas{background:#001a00;border-radius:12px;max-width:100%}
#info{margin:8px;font-size:18px}
a{color:#0f0}
</style></head><body>
<h2>Radar 180°</h2>
<canvas id='c'></canvas>
<div id='info'>--</div>
<a href='/'>&larr; Voltar</a>
<script>
const MAX=50;            // alcance exibido (cm)
const FADE=4000;         // tempo (ms) que o ponto vermelho permanece
const c=document.getElementById('c'),x=c.getContext('2d');
let W,H,cx,cy,R,pts=[],cur={angulo:90,distancia:0,valida:false,detectado:false};
function resize(){W=Math.min(window.innerWidth-20,640);H=W/2+30;c.width=W;c.height=H;cx=W/2;cy=H-20;R=W/2-20;}
resize();window.onresize=resize;
function pol(a,r){const t=a*Math.PI/180;return[cx+r*Math.cos(t),cy-r*Math.sin(t)];}
function draw(){
 x.clearRect(0,0,W,H);
 x.strokeStyle='#0a6';x.fillStyle='#0a6';x.lineWidth=1;x.font='11px Arial';
 for(let i=1;i<=4;i++){x.beginPath();x.arc(cx,cy,R*i/4,Math.PI,2*Math.PI);x.stroke();
  x.fillText(Math.round(MAX*i/4)+'cm',cx+R*i/4-26,cy+12);}
 for(let a=0;a<=180;a+=30){const p=pol(a,R);x.beginPath();x.moveTo(cx,cy);x.lineTo(p[0],p[1]);x.stroke();
  const q=pol(a,R+12);x.fillText(a+'°',q[0]-10,q[1]+4);}
 // feixe (rastro verde)
 for(let k=0;k<12;k++){const p=pol(cur.angulo-k*(cur.dir||1)*1.5,R);
  x.strokeStyle='rgba(0,255,0,'+(0.5-k*0.04)+')';x.lineWidth=3;
  x.beginPath();x.moveTo(cx,cy);x.lineTo(p[0],p[1]);x.stroke();}
 // objetos detectados
 const now=Date.now();pts=pts.filter(p=>now-p.t<FADE);
 for(const p of pts){const al=1-(now-p.t)/FADE;const q=pol(p.a,Math.min(p.d,MAX)/MAX*R);
  x.fillStyle='rgba(255,0,0,'+al+')';x.beginPath();x.arc(q[0],q[1],8,0,7);x.fill();
  x.strokeStyle='rgba(255,120,120,'+al+')';x.lineWidth=2;x.stroke();}
}
let last=90;
async function busca(){
 try{const d=await (await fetch('/dados')).json();
  cur.dir=d.angulo>=last?1:-1;last=d.angulo;Object.assign(cur,d);
  if(d.detectado)pts.push({a:d.angulo,d:d.distancia,t:Date.now()});
  if(pts.length>300)pts.shift();
  document.getElementById('info').innerText='Ângulo: '+d.angulo+'°  |  '+
   (d.valida?'Distância: '+d.distancia.toFixed(1)+' cm':'Sem eco')+
   (d.detectado?'  |  OBJETO!':'');
 }catch(e){}
 setTimeout(busca,70);
}
busca();setInterval(draw,50);
</script></body></html>
)rawliteral";

// ======================= ROTAS =======================
void enviarPagina() { server.send_P(200, "text/html", PAGINA_INICIO); }
void enviarRadar()  { server.send_P(200, "text/html", PAGINA_RADAR); }

void enviarDados() {
  String json = "{";
  json += "\"angulo\":" + String(anguloAtual) + ",";
  json += "\"distancia\":" + String(leituraValida ? distanciaAtual : 0.0, 1) + ",";
  json += "\"valida\":" + String(leituraValida ? "true" : "false") + ",";
  json += "\"detectado\":" + String(objetoNoAngulo ? "true" : "false") + ",";
  json += "\"alarme\":" + String(alarmeAtivo ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

// ======================= SENSOR =======================
void medirDistancia() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  unsigned long duracao = pulseIn(PIN_ECHO, HIGH, 30000UL);

  if (duracao == 0) {
    leituraValida = false;
    objetoNoAngulo = false;
  } else {
    leituraValida = true;
    distanciaAtual = duracao / 58.0;
    objetoNoAngulo = (distanciaAtual <= DISTANCIA_LIMITE);
  }
}

// ======================= SETUP =======================
void setup() {
  Serial.begin(115200);

  pinMode(LED, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(buzzer, OUTPUT);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);

  digitalWrite(PIN_TRIG, LOW);
  digitalWrite(LED, LOW);
  digitalWrite(LED2, HIGH);   // verde = nenhum objeto

  servo.setPeriodHertz(50);
  servo.attach(PIN_SERVO, 500, 2400);
  servo.write(anguloAtual);

  WiFi.softAP(ssid_ap, senha_ap);
  Serial.print("Rede Wi-Fi criada: ");
  Serial.println(ssid_ap);
  Serial.print("Acesse: http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", enviarPagina);
  server.on("/radar", enviarRadar);
  server.on("/dados", enviarDados);
  server.begin();
}

// ======================= LOOP =======================
void loop() {
  server.handleClient();
  unsigned long agora = millis();

  // Desliga o buzzer quando o bip termina
  if (buzzerDesligaEm && agora >= buzzerDesligaEm) {
    noTone(buzzer);
    buzzerDesligaEm = 0;
  }

  // Varredura: mede no ângulo atual e depois avança o servo
  if (agora - ultimaLeitura >= INTERVALO_MS) {
    ultimaLeitura = agora;

    medirDistancia();
    if (objetoNoAngulo) ultimoDetectado = agora;

    anguloAtual += direcao * PASSO_GRAUS;
    if (anguloAtual >= 180) { anguloAtual = 180; direcao = -1; }
    if (anguloAtual <= 0)   { anguloAtual = 0;   direcao = 1;  }
    servo.write(anguloAtual);
  }

  // Estado do alarme (com "hold" para não piscar entre ângulos)
  bool novoAlarme = (ultimoDetectado != 0) && (agora - ultimoDetectado < HOLD_MS);

  if (novoAlarme && !alarmeAtivo) {
    // Transição: objeto ENCONTRADO -> frequência 1 (aguda)
    bip(NOTE_A6, 200);
    proximoBipObjeto = agora + 500;
  } else if (!novoAlarme && alarmeAtivo) {
    // Transição: objeto PERDIDO -> frequência 2 (grave)
    bip(NOTE_C5, 250);
  }
  alarmeAtivo = novoAlarme;

  // Enquanto o objeto estiver presente, repete o bip agudo
  if (alarmeAtivo && agora >= proximoBipObjeto) {
    bip(NOTE_A6, 100);
    proximoBipObjeto = agora + 500;
  }

  // LEDs de retorno visual
  digitalWrite(LED,  alarmeAtivo ? HIGH : LOW);   // vermelho: objeto
  digitalWrite(LED2, alarmeAtivo ? LOW  : HIGH);  // verde: livre
}