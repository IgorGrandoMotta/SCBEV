#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>
#include <esp_system.h>
#include <esp_timer.h>
#include "Seguranca.h"

const char *NOME_WIFI = "SEU_WIFI", *SENHA_WIFI = "SUA_SENHA";
const int SENSOR = 34, RELE = 19;
WebServer servidor(80);
Seguranca estado;
portMUX_TYPE trava = portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t fila;
bool memoriaOk = false, servidorAtivo = false, horaRegistrada = false;
volatile bool filaCheia = false;
String token, boot;
uint32_t tentativa = 0;
struct Registro { uint64_t ms; time_t hora; char nivel[8], texto[160]; };

Seguranca consultar() {
  portENTER_CRITICAL(&trava);
  Seguranca copia = estado;
  portEXIT_CRITICAL(&trava);
  return copia;
}

void falhaMemoria() {
  portENTER_CRITICAL(&trava);
  memoriaOk = false;
  estado.memoriaOk = false;
  estado.fechar();
  digitalWrite(RELE, LOW);
  portEXIT_CRITICAL(&trava);
  Serial.println("ERRO: falha no historico; abertura bloqueada.");
}

void registrar(const char *nivel, const char *texto) {
  Registro r{};
  r.ms = esp_timer_get_time() / 1000;
  r.hora = time(nullptr);
  snprintf(r.nivel, sizeof(r.nivel), "%s", nivel);
  snprintf(r.texto, sizeof(r.texto), "%s", texto);
  if (!fila || xQueueSend(fila, &r, 0) != pdTRUE) filaCheia = true;
}

String dataHora(time_t hora) {
  if (hora < 1704067200) return "SEM_HORA";
  struct tm t;
  localtime_r(&hora, &t);
  char texto[24];
  strftime(texto, sizeof(texto), "%d/%m/%Y %H:%M:%S", &t);
  return texto;
}

void salvarRegistros() {
  Registro r;
  while (fila && xQueueReceive(fila, &r, 0) == pdTRUE) {
    String linha = dataHora(r.hora) + " | " + r.nivel + " | " + r.texto;
    linha += " | boot=" + boot + " ms=" + String((unsigned long long)r.ms) + "\n";
    Serial.print(linha);
    if (!memoriaOk) continue;
    File f = LittleFS.open("/simples.txt", FILE_APPEND);
    if (!f) { falhaMemoria(); continue; }
    if (f.size() + linha.length() > 32768) {
      f.close();
      if ((LittleFS.exists("/simples_antigo.txt") && !LittleFS.remove("/simples_antigo.txt")) ||
          !LittleFS.rename("/simples.txt", "/simples_antigo.txt")) { falhaMemoria(); continue; }
      f = LittleFS.open("/simples.txt", FILE_APPEND);
    }
    if (!f || f.print(linha) != linha.length()) falhaMemoria();
    f.close();
  }
  if (filaCheia) { filaCheia = false; falhaMemoria(); }
}

void lerSensor(void *) {
  TickType_t proxima = xTaskGetTickCount();
  for (;;) {
    int leitura = analogRead(SENSOR);
    bool wifi = WiFi.status() == WL_CONNECTED;
    portENTER_CRITICAL(&trava);
    uint32_t eventos = estado.atualizar(leitura, millis(), wifi, memoriaOk);
    digitalWrite(RELE, estado.aberta ? HIGH : LOW);
    portEXIT_CRITICAL(&trava);
    if (eventos & Seguranca::AGUA) registrar("AVISOS", "Vazamento: fechamento automatico e alarme retido");
    if (eventos & Seguranca::SECO) registrar("INFOS", "Sensor seco; rearme e abertura dependem do usuario");
    if (eventos & Seguranca::REDE_CAIU) registrar("AVISOS", "Wi-Fi caiu; fechamento solicitado");
    if (eventos & Seguranca::REDE_VOLTOU) registrar("INFOS", "Wi-Fi conectado/reconectado; abertura manual");
    vTaskDelayUntil(&proxima, pdMS_TO_TICKS(50));
  }
}

String cabecalho(const char *titulo) {
  String p = R"HTML(<!doctype html><html lang="pt-BR"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>SCBEV</title><style>
*{box-sizing:border-box}body{margin:0;background:#0b141b;color:#e3ecef;font:15px/1.6 system-ui,sans-serif}header{background:#102c39;border-bottom:1px solid #24404b;padding:30px 24px}header>div{max-width:1140px;margin:auto;display:flex;align-items:center;justify-content:space-between;gap:24px}h1{font-size:26px;letter-spacing:4px;margin:0}header small{color:#b0c5ce}.badge{border:1px solid #43616d;border-radius:30px;padding:7px 15px;font-size:11px;white-space:nowrap}main{max-width:1188px;margin:auto;padding:28px 24px;display:grid;grid-template-columns:1.25fr 1fr;gap:22px}.page-title{grid-column:1/-1;color:#8fa9b5;font-size:13px;letter-spacing:1px}section{min-width:0;background:#121f27;border:1px solid #2c414c;border-radius:18px;padding:26px}h2{margin:0 0 16px;font-size:11px;font-weight:650;letter-spacing:2px;text-transform:uppercase;color:#9eb4bf}strong{display:block;font-size:27px;line-height:1.3;margin:12px 0}p{margin:12px 0}small{display:block;font-size:12px;color:#9fb2bd}.ok{color:#7ae0c4}.aviso{color:#f0c778}.erro{color:#f0787e}.monitor.ok{border-color:#39bfa4}.monitor.aviso{border-color:#ae8439}.monitor.erro{border-color:#c95461}.actions{display:flex;flex-wrap:wrap;align-items:center;gap:8px;margin-top:18px}form{display:inline-block;margin:0}button,.button{display:inline-block;font:inherit;font-size:13px;text-decoration:none;border:1px solid #45606d;color:#e2eef3;background:#1c3542;border-radius:9px;padding:10px 13px;cursor:pointer}button:hover:not(:disabled),.button:hover{background:#284c5c}button:disabled{opacity:.4;cursor:default}.primary{background:#064c43;border-color:#21b699}.primary:hover:not(:disabled){background:#086556}button:focus-visible,a:focus-visible{outline:3px solid #f0c778;outline-offset:3px}a{color:#8edbd0}.section-head{display:flex;justify-content:space-between;gap:16px;flex-wrap:wrap;align-items:center}.section-head .actions{margin:0}.reading{display:flex;gap:12px;align-items:baseline;margin:12px 0}.reading b{font-size:42px;line-height:1.2}.reading span{color:#8fa9b5;font-size:13px}canvas{display:block;width:100%;height:220px}.legend{display:flex;flex-wrap:wrap;gap:16px;font-size:11px;color:#a5bbc5;margin:10px 0}.legend i{display:inline-block;width:14px;height:2px;background:#62ddbf;vertical-align:middle;margin-right:5px}.legend .water{background:#f0787e}.legend .dry{background:#f0c778}dl{display:grid;grid-template-columns:1fr 1fr;gap:20px 18px;margin:22px 0}dt{font-size:10px;text-transform:uppercase;letter-spacing:1px;color:#8fa9b5}dd{margin:5px 0 0;font-size:13px;overflow-wrap:anywhere}.largo{grid-column:1/-1}.table-wrap{max-height:340px;overflow:auto;margin:16px 0}table{width:100%;border-collapse:collapse;font-size:12px;text-align:left}th{position:sticky;top:0;background:#152630;color:#a9bec8;font-weight:500}th,td{padding:12px 10px;border-bottom:1px solid #2c3d47;vertical-align:top}td:first-child{white-space:nowrap;width:190px}.nivel{font-size:10px;letter-spacing:.5px;font-weight:650}td:last-child{overflow-wrap:anywhere}pre{font:12px/1.8 monospace;white-space:pre-wrap;overflow-wrap:anywhere}#resultado{font-size:12px;color:#b5c9d3;min-height:20px}@media(max-width:720px){main{grid-template-columns:1fr;padding:18px 14px;gap:16px}header{padding:22px 16px}header>div{align-items:flex-start}.badge{display:none}section{padding:22px 18px;border-radius:14px}strong{font-size:23px}h1{font-size:23px}.section-head{display:block}.section-head .actions{margin-top:12px}dl{gap:18px 12px}td:first-child{white-space:normal;min-width:110px}.table-wrap{overflow:auto}table{min-width:540px}}
</style></head><body><header><div><div><h1>SCBEV</h1><small>Sistema Ciberfísico de Bloqueio Emergencial de Vazamentos</small></div><span class="badge">ESP32 · HTTP / TCP 80</span></div></header><main>)HTML";
  return p + "<div class='page-title'>" + titulo + "</div>";
}

void dados() {
  Seguranca s = consultar(), teste = s;
  String j = "{\"leitura\":" + String(s.leitura);
  j += ",\"molhado\":" + String(s.molhado ? "true" : "false");
  j += ",\"alarme\":" + String(s.alarme ? "true" : "false");
  j += ",\"seco\":" + String(s.secoConfirmado ? "true" : "false");
  j += ",\"aberta\":" + String(s.aberta ? "true" : "false");
  j += ",\"wifi\":" + String(s.conectado ? "true" : "false");
  j += ",\"memoria\":" + String(s.memoriaOk ? "true" : "false");
  j += ",\"abrir\":" + String(s.podeAbrir(millis()) ? "true" : "false");
  j += ",\"rearmar\":" + String(s.alarme && teste.rearmar(millis()) ? "true" : "false");
  j += ",\"sinal\":" + String(WiFi.RSSI());
  j += ",\"tempo\":" + String((unsigned long long)(esp_timer_get_time() / 1000000));
  j += ",\"uso\":" + String(memoriaOk ? LittleFS.usedBytes() : 0);
  j += ",\"ip\":\"" + WiFi.localIP().toString() + "\",\"mac\":\"" + WiFi.macAddress();
  j += "\",\"hora\":\"" + dataHora(time(nullptr)) + "\",\"token\":\"" + token + "\"}";
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send(200, "application/json; charset=utf-8", j);
}

void pagina() {
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.sendHeader("X-Frame-Options", "DENY");
  servidor.send(200, "text/html; charset=utf-8", cabecalho("Painel de monitoramento") + R"HTML(
<section id="monitor" class="monitor">
<h2>Monitoramento do ambiente</h2><strong id="estado">Conectando ao ESP32</strong><p id="alarme">Aguardando leitura do sensor.</p><small id="conexao" role="status">Aguardando atualização</small>
</section>
<section><h2>Comando da válvula</h2><strong id="valvula">—</strong><small>Comando elétrico do relé · sem sensor de posição</small>
<div class="actions"><form method="post" action="/abrir"><button id="abrir" class="primary" disabled>Abrir válvula</button></form><form method="post" action="/fechar"><button id="fechar" disabled>Fechar válvula</button></form><form method="post" action="/rearmar"><button id="rearmar" disabled>Rearmar alarme</button></form></div>
<p id="resultado" role="status" aria-live="polite"></p></section>
<section><div class="section-head"><h2>Leitura do sensor · GPIO34</h2><small>Últimas 60 leituras desta página</small></div>
<div class="reading"><b id="leitura">—</b><span>/ 4095</span></div>
<canvas id="grafico" role="img" aria-label="Gráfico das leituras recentes do sensor, de zero a 4095"></canvas>
<div class="legend"><span><i></i>Leitura ADC</span><span><i class="water"></i>Água &lt; 4000</span><span><i class="dry"></i>Seco &gt; 4050</span></div>
<small>Leitura bruta do sensor. Confirmação de seco: 2 segundos.</small></section>
<section><h2>Rede e dispositivo</h2><dl><div><dt>Endereço IP</dt><dd id="ip">—</dd></div><div><dt>MAC</dt><dd id="mac">—</dd></div><div><dt>Wi-Fi</dt><dd id="wifi">—</dd></div><div><dt>Sinal</dt><dd id="sinal">—</dd></div><div><dt>Data e hora · Brasília</dt><dd id="hora">—</dd></div><div><dt>Tempo ligado</dt><dd id="tempo">—</dd></div><div><dt>Histórico</dt><dd id="memoria">—</dd></div><div><dt>Armazenamento usado</dt><dd id="uso">—</dd></div></dl>
<form method="post" action="/hora"><button id="acertar" disabled>Sincronizar horário</button></form></section>
<section class="largo"><div class="section-head"><div><h2>Histórico de eventos</h2><small>Registros armazenados no dispositivo</small></div><div class="actions"><button id="atualizar-log">Atualizar</button><button id="csv" disabled>Baixar CSV</button><a class="button" href="/registros">Registros originais</a></div></div>
<p id="log-status" role="status">Carregando histórico...</p><div class="table-wrap"><table><thead><tr><th>Data e hora</th><th>Nível</th><th>Ocorrência</th></tr></thead><tbody id="eventos"></tbody></table></div><small>* Horário reconstruído após sincronização do mesmo período de funcionamento.</small></section>
</main><script>
const $=id=>document.getElementById(id), amostras=[];
let token='',ocupado=false,registros=[],carregandoLog=false;
const texto=(id,valor)=>$(id).textContent=valor;
function desenhar(){
  const c=$('grafico'),w=c.clientWidth,h=220,dpr=window.devicePixelRatio||1;
  c.width=Math.round(w*dpr);c.height=Math.round(h*dpr);
  const g=c.getContext('2d');g.scale(dpr,dpr);
  const l=42,r=w-12,t=14,b=h-28,y=v=>b-(b-t)*v/4095;
  g.font='11px system-ui';g.textAlign='right';
  [0,2048,4095].forEach(v=>{g.strokeStyle='#2d3a43';g.beginPath();g.moveTo(l,y(v));g.lineTo(r,y(v));g.stroke();g.fillStyle='#99aab4';g.fillText(v,l-8,y(v)+4);});
  [[4000,'#f0787e'],[4050,'#f0c778']].forEach(([v,cor])=>{g.strokeStyle=cor;g.setLineDash([4,4]);g.beginPath();g.moveTo(l,y(v));g.lineTo(r,y(v));g.stroke();});g.setLineDash([]);
  if(amostras.length){g.strokeStyle='#62ddbf';g.lineWidth=2;g.beginPath();amostras.forEach((v,i)=>{const x=l+(r-l)*i/Math.max(1,amostras.length-1);i?g.lineTo(x,y(v)):g.moveTo(x,y(v));});g.stroke();if(amostras.length===1){g.beginPath();g.arc(l,y(amostras[0]),3,0,Math.PI*2);g.fillStyle='#62ddbf';g.fill();}}
  g.fillStyle='#99aab4';g.textAlign='left';g.fillText('mais antiga',l,h-6);g.textAlign='right';g.fillText('agora',r,h-6);
}
function botoes(s){$('abrir').disabled=ocupado||!s.abrir;$('rearmar').disabled=ocupado||!s.rearmar;$('fechar').disabled=ocupado;$('acertar').disabled=ocupado;}
async function atualizar(){
  try{
    const r=await fetch('/estado',{cache:'no-store',signal:AbortSignal.timeout(4000)});if(!r.ok)throw Error();const s=await r.json();
    if(!Number.isInteger(s.leitura)||s.leitura<0||s.leitura>4095||typeof s.token!=='string')throw Error();
    token=s.token;const cor=s.molhado?'erro':s.alarme||!s.seco?'aviso':'ok';$('monitor').className='monitor '+cor;
    texto('estado',s.molhado?'Vazamento detectado':s.alarme?'Alarme aguardando rearme':s.seco?'Sensor seco':'Confirmando leitura');
    texto('alarme',s.alarme?'Alarme retido · abertura bloqueada':s.seco?'Alarme livre · abertura por comando manual':'Aguardando estabilização do sensor');
    texto('conexao','Atualizado às '+new Date().toLocaleTimeString('pt-BR'));texto('valvula',s.aberta?'ABERTURA SOLICITADA':'FECHAMENTO SOLICITADO');
    texto('leitura',s.leitura);texto('ip',s.ip);texto('mac',s.mac);texto('wifi',s.wifi?'Conectado':'Desconectado');texto('sinal',s.sinal+' dBm');texto('hora',s.hora==='SEM_HORA'?'Não sincronizado':s.hora);
    texto('tempo',Math.floor(s.tempo/3600)+' h '+Math.floor(s.tempo%3600/60)+' min');texto('memoria',s.memoria?'Disponível':'Falha');texto('uso',Math.ceil(s.uso/1024)+' KiB');
    amostras.push(s.leitura);if(amostras.length>60)amostras.shift();desenhar();botoes(s);
  }catch(e){$('monitor').className='monitor aviso';texto('conexao','Sem resposta do ESP32 · dados anteriores');$('abrir').disabled=true;$('rearmar').disabled=true;}
}
async function ciclo(){await atualizar();setTimeout(ciclo,2000);}
document.querySelectorAll('form').forEach(f=>f.addEventListener('submit',async e=>{
  e.preventDefault();if(ocupado||!token)return;ocupado=true;document.querySelectorAll('form button').forEach(b=>b.disabled=true);
  const dados=new URLSearchParams({token});if(f.getAttribute('action')==='/hora')dados.set('epoch',Math.floor(Date.now()/1000));
  try{const r=await fetch(f.getAttribute('action'),{method:'POST',body:dados,signal:AbortSignal.timeout(5000)});texto('resultado',r.ok?'Comando aceito.':r.status===409?'Comando bloqueado pelo estado do sistema.':'Comando recusado.');}
  catch(e){texto('resultado','Sem confirmação do comando.');}
  finally{ocupado=false;await atualizar();await carregarHistorico();}
}));
function interpretar(texto){
  const linhas=texto.trim().split('\n').filter(Boolean),bases={};
  linhas.forEach(l=>{const b=l.match(/BASE_HORA=(\d+).*boot=(\w+)/);if(b)bases[b[2]]=Number(b[1]);});
  return linhas.map(l=>{const partes=l.split(' | '),b=l.match(/boot=(\w+) ms=(\d+)/);let data=partes[0];
    if(data==='SEM_HORA'&&b&&bases[b[1]])data=new Date(bases[b[1]]+Number(b[2])).toLocaleString('pt-BR',{timeZone:'America/Sao_Paulo'})+' *';
    return [data,partes[1]||'INFOS',partes.slice(2).filter(p=>!p.startsWith('boot=')).join(' | ')||l];});
}
async function carregarHistorico(){
  if(carregandoLog)return;carregandoLog=true;$('atualizar-log').disabled=true;
  try{const r=await fetch('/registros',{cache:'no-store',signal:AbortSignal.timeout(8000)});if(!r.ok)throw Error();registros=interpretar(await r.text());
    $('eventos').replaceChildren();registros.slice(-80).reverse().forEach(row=>{const tr=document.createElement('tr');row.forEach((v,i)=>{const td=document.createElement('td');td.textContent=v;if(i===1)td.className='nivel '+(v==='ERRO'?'erro':v==='AVISOS'?'aviso':'ok');tr.append(td);});$('eventos').append(tr);});
    texto('log-status',registros.length?'Exibindo '+Math.min(80,registros.length)+' de '+registros.length+' registros.':'Nenhum evento registrado.');$('csv').disabled=!registros.length;
  }catch(e){texto('log-status','Histórico indisponível · tente atualizar novamente.');}
  finally{carregandoLog=false;$('atualizar-log').disabled=false;}
}
$('atualizar-log').addEventListener('click',carregarHistorico);
$('csv').addEventListener('click',()=>{const cell=v=>'"'+String(v).replace(/^[=+@-]/,"'$&").replace(/"/g,'""')+'"';const csv=[['Data e hora','Nível','Ocorrência'],...registros].map(r=>r.map(cell).join(';')).join('\r\n');const url=URL.createObjectURL(new Blob(['\ufeff'+csv],{type:'text/csv;charset=utf-8'}));const a=document.createElement('a');a.href=url;a.download='SCBEV_eventos.csv';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);});
window.addEventListener('resize',desenhar);desenhar();ciclo();carregarHistorico();setInterval(carregarHistorico,15000);
</script></body></html>
)HTML");
}

void comando() {
  if (servidor.arg("token") != token) { servidor.send(403, "text/plain", "Atualize a pagina."); return; }
  String rota = servidor.uri();
  bool ok = false, wifi = WiFi.status() == WL_CONNECTED;
  portENTER_CRITICAL(&trava);
  if (rota == "/fechar") { estado.fechar(); ok = true; }
  if (rota == "/abrir") ok = wifi && estado.abrir(millis());
  if (rota == "/rearmar") ok = wifi && estado.rearmar(millis());
  digitalWrite(RELE, estado.aberta ? HIGH : LOW);
  portEXIT_CRITICAL(&trava);
  registrar(ok ? "INFOS" : "AVISOS", (rota + (ok ? ": comando aceito" : ": comando bloqueado")).c_str());
  servidor.send(ok ? 200 : 409, "text/html; charset=utf-8", String(ok ? "Comando aceito." : "Bloqueado: confira sensor, alarme, rede e memoria.") + " <a href='/'>Voltar</a>");
}

void registros() {
  if (!memoriaOk) { servidor.send(503, "text/plain", "Memoria indisponivel."); return; }
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.setContentLength(CONTENT_LENGTH_UNKNOWN);
  servidor.send(200, "text/plain; charset=utf-8", "");
  for (const char *nome : {"/simples_antigo.txt", "/simples.txt"}) {
    File f = LittleFS.open(nome, FILE_READ);
    char trecho[512];
    while (f && f.available() && servidor.client().connected()) {
      size_t n = f.readBytes(trecho, sizeof(trecho));
      servidor.sendContent(trecho, n);
      yield();
    }
    f.close();
  }
  servidor.sendContent("");
}

void historico() {
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send(200, "text/html; charset=utf-8", cabecalho("Histórico de eventos") + R"HTML(
<section class="largo"><p><a href="/">Voltar ao painel</a> · <a href="/registros">Registros originais</a></p>
<small>Níveis: INFOS, AVISOS e ERRO. * Horário reconstruído após sincronização do mesmo boot.</small><pre id="log">Carregando...</pre></section>
<script>
fetch('/registros',{cache:'no-store'}).then(r=>{if(!r.ok)throw Error('Memoria indisponivel');return r.text()}).then(texto=>{
  const linhas=texto.trim().split('\n'), bases={};
  for(const l of linhas){const b=l.match(/BASE_HORA=(\d+).*boot=(\w+)/);if(b)bases[b[2]]=Number(b[1]);}
  document.getElementById('log').textContent=linhas.map(l=>{
    const b=l.match(/boot=(\w+) ms=(\d+)/);
    if(l.startsWith('SEM_HORA')&&b&&bases[b[1]])l=l.replace('SEM_HORA',new Date(bases[b[1]]+Number(b[2])).toLocaleString('pt-BR',{timeZone:'America/Sao_Paulo'})+' *');
    return l;
  }).join('\n');
}).catch(e=>document.getElementById('log').textContent=e.message);
</script></main></body></html>)HTML");
}

void acertarHora() {
  if (servidor.arg("token") != token) { servidor.send(403, "text/plain", "Atualize a pagina."); return; }
  String valor = servidor.arg("epoch");
  int64_t segundos = strtoll(valor.c_str(), nullptr, 10);
  if (segundos < 1704067200LL || segundos > 4102444800LL) {
    registrar("ERRO", "Ajuste de hora recusado: data invalida");
    servidor.send(400, "text/plain", "Hora invalida."); return;
  }
  struct timeval tv{time_t(segundos), 0};
  settimeofday(&tv, nullptr);
  registrar("INFOS", "Hora ajustada pelo navegador");
  servidor.sendHeader("Location", "/");
  servidor.send(303);
}

void setup() {
  digitalWrite(RELE, LOW); pinMode(RELE, OUTPUT); digitalWrite(RELE, LOW);
  Serial.begin(115200);
  pinMode(SENSOR, INPUT); analogReadResolution(12); analogSetPinAttenuation(SENSOR, ADC_11db);
  token = String(esp_random(), HEX) + String(esp_random(), HEX);
  boot = String(esp_random(), HEX);
  fila = xQueueCreate(16, sizeof(Registro));
  Preferences config;
  bool nvs = config.begin("scbev", false), pronta = nvs && config.getBool("fs_pronto", false);
  memoriaOk = LittleFS.begin(false);
  if (!memoriaOk && nvs && !pronta) memoriaOk = LittleFS.format() && LittleFS.begin(false);
  if (memoriaOk && nvs && !pronta) config.putBool("fs_pronto", true);
  config.end();
  if (!memoriaOk || !fila) falhaMemoria();
  if (xTaskCreate(lerSensor, "sensor", 4096, nullptr, 2, nullptr) != pdPASS) falhaMemoria();
  servidor.on("/", HTTP_GET, pagina);
  servidor.on("/estado", HTTP_GET, dados);
  servidor.on("/abrir", HTTP_POST, comando); servidor.on("/fechar", HTTP_POST, comando);
  servidor.on("/rearmar", HTTP_POST, comando); servidor.on("/hora", HTTP_POST, acertarHora);
  servidor.on("/historico", HTTP_GET, historico);
  servidor.on("/registros", HTTP_GET, registros);
  servidor.onNotFound([] { registrar("ERRO", "Requisicao HTTP para rota inexistente"); servidor.send(404, "text/plain", "Pagina inexistente."); });
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true); WiFi.begin(NOME_WIFI, SENHA_WIFI);
  configTzTime("BRT3", "pool.ntp.org", "time.google.com");
  registrar("INFOS", "SCBEV iniciado; valvula com comando de fechamento");
}

void loop() {
  bool wifi = WiFi.status() == WL_CONNECTED;
  if (wifi && !servidorAtivo) {
    servidor.begin(); servidorAtivo = true;
    String endereco = "Servidor: http://" + WiFi.localIP().toString() + " TCP 80; MAC " + WiFi.macAddress();
    Serial.println(endereco); registrar("INFOS", endereco.c_str());
  }
  if (!wifi && servidorAtivo) { servidor.stop(); servidorAtivo = false; }
  if (!wifi && uint32_t(millis() - tentativa) >= 15000) {
    tentativa = millis(); WiFi.reconnect(); registrar("AVISOS", "Tentativa de reconexao Wi-Fi");
  }
  if (!horaRegistrada && time(nullptr) >= 1704067200) {
    horaRegistrada = true;
    String base = "BASE_HORA=" + String((long long)(int64_t(time(nullptr)) * 1000 - esp_timer_get_time() / 1000));
    registrar("INFOS", base.c_str());
  }
  salvarRegistros();
  if (servidorAtivo) servidor.handleClient();
  delay(2);
}
