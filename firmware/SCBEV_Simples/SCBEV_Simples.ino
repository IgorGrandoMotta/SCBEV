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
const int SENSOR = 34, RELE = 19; // Rele RoboCore: HIGH liga, LOW desliga.
WebServer servidor(80);          // HTTP sobre TCP, porta 80.
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

// A tarefa do sensor so coloca registros na fila; o loop escreve na flash.
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

// Executa a cada 50 ms, mesmo enquanto o navegador acessa o historico.
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
  String p = "<!doctype html><html lang='pt-BR'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>SCBEV</title>";
  p += R"HTML(<style>
*{box-sizing:border-box}body{margin:0;background:#10171d;color:#e8eef2;font:16px/1.6 system-ui,sans-serif}
header{background:#102e3d;border-bottom:1px solid #344956;padding:24px}header div,main{max-width:980px;margin:auto}
h1{margin:0;font-size:26px;letter-spacing:3px}h2{margin:0 0 16px;font-size:14px;color:#b7cbd5;letter-spacing:1px}p{margin:10px 0}
main{padding:24px;display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:20px}section{padding:24px;background:#19232c;border:1px solid #344956;border-radius:12px}
strong{display:block;font-size:23px;line-height:1.4;margin-bottom:12px}.ok{color:#80dfbf}.aviso{color:#f1c16a}.erro{color:#ff9292}
small{display:block;color:#b7cbd5;font-size:13px}a{color:#9addf4}form{display:inline-block;margin:4px 4px 4px 0}
button{font:inherit;font-size:14px;cursor:pointer;color:#f0f6fa;background:#25495b;border:1px solid #68899a;border-radius:7px;padding:10px 14px}
button:hover:not(:disabled){background:#32627a}button:disabled{opacity:.45;cursor:default}a:focus-visible,button:focus-visible{outline:3px solid #f1c16a;outline-offset:3px}
pre{white-space:pre-wrap;overflow-wrap:anywhere;font:13px/1.7 monospace}.largo{grid-column:1/-1}.dato{overflow-wrap:anywhere}
@media(max-width:640px){main{grid-template-columns:1fr;padding:16px;gap:16px}section{padding:20px}strong{font-size:21px}}
</style></head><body><header><div><h1>SCBEV</h1><small>Sistema Ciberfísico de Bloqueio Emergencial de Vazamentos</small></div></header><main>)HTML";
  return p + "<section class='largo'><h2>" + titulo + "</h2><small>ESP32 · Rede local · HTTP / TCP 80</small></section>";
}

String formulario(const char *rota, const char *texto, bool habilitado = true) {
  return "<form method='post' action='" + String(rota) + "'><input type='hidden' name='token' value='" + token + "'><button" + (habilitado ? String("") : String(" disabled")) + ">" + texto + "</button></form>";
}

void pagina() {
  Seguranca s = consultar();
  Seguranca teste = s; // Confere o rearme em uma copia; nao altera o estado real.
  String p = cabecalho("Monitoramento e controle");
  p += "<section><h2>MONITORAMENTO</h2><strong id='estado' data-live class='" + String(s.molhado ? "erro" : s.alarme || !s.secoConfirmado ? "aviso" : "ok") + "'>";
  p += String(s.molhado ? "Vazamento detectado" : s.alarme ? "Alarme aguardando rearme" : s.secoConfirmado ? "Sensor seco" : "Confirmando leitura") + "</strong>";
  p += "<p>Leitura do sensor: <b id='leitura' data-live>" + String(s.leitura) + " / 4095</b></p><small>Água: abaixo de 4000. Seco: acima de 4050 por 2 s.</small>";
  p += "<p>Alarme: <span id='alarme' data-live>" + String(s.alarme ? "retido; seque o sensor e rearme" : "livre") + "</span></p></section>";
  p += "<section><h2>COMANDO DA VÁLVULA</h2><strong id='valvula' data-live>" + String(s.aberta ? "ABERTURA SOLICITADA" : "FECHAMENTO SOLICITADO") + "</strong>";
  p += formulario("/abrir", "Abrir válvula", s.podeAbrir(millis())) + formulario("/fechar", "Fechar válvula") + formulario("/rearmar", "Rearmar alarme", s.alarme && teste.rearmar(millis()));
  p += "<p><small>Após secar por 2 s, rearme e abra manualmente. A posição física não é medida. A bomba tem alimentação separada.</small></p></section>";
  p += "<section class='dato'><h2>REDE E DISPOSITIVO</h2><p>Wi-Fi: <span id='wifi' data-live>" + String(s.conectado ? "conectado" : "desconectado") + "</span></p>";
  p += "<p>IP: " + WiFi.localIP().toString() + "<br>MAC: " + WiFi.macAddress() + "</p><small id='atualizacao' role='status'>Atualização dos dados a cada 2 s.</small></section>";
  p += "<section><h2>HISTÓRICO E HORÁRIO</h2><p>Hora: <span id='hora' data-live>" + dataHora(time(nullptr)) + "</span></p><p>Memória: <span id='memoria' data-live>" + (s.memoriaOk ? "disponível" : "FALHA; abertura bloqueada") + "</span></p>";
  p += "<form method='post' action='/hora' onsubmit=\"this.epoch.value=Math.floor(Date.now()/1000)\"><input type='hidden' name='epoch'><input type='hidden' name='token' value='" + token + "'><button>Acertar hora pelo celular/PC</button></form>";
  p += R"HTML(<p><a href='/historico'>Consultar histórico de eventos</a></p></section></main>
<script>
// Busca os dados sem recarregar a pagina nem substituir os controles.
async function atualizar() {
  try {
    const r=await fetch('/',{cache:'no-store',signal:AbortSignal.timeout(4000)});
    if(!r.ok) throw Error('Sem resposta');
    const nova=new DOMParser().parseFromString(await r.text(),'text/html');
    const campos=[...document.querySelectorAll('[data-live]')];
    if(campos.some(e=>!nova.getElementById(e.id))) throw Error('Resposta invalida');
    campos.forEach(e=>{const n=nova.getElementById(e.id);e.textContent=n.textContent;e.className=n.className;});
    document.querySelectorAll('form').forEach(f=>{f.querySelector('button').disabled=nova.querySelector('form[action="'+f.getAttribute('action')+'"] button').disabled;});
    document.querySelectorAll('input[name="token"]').forEach(e=>e.value=nova.querySelector('input[name="token"]').value);
    document.getElementById('atualizacao').textContent='Dados atualizados a cada 2 s, sem recarregar a página.';
  } catch(e) {
    document.getElementById('atualizacao').textContent='Sem resposta do ESP32. Dados anteriores; tentando novamente.';
    document.querySelectorAll('form[action="/abrir"] button,form[action="/rearmar"] button').forEach(b=>b.disabled=true);
  }
  setTimeout(atualizar,2000);
}
setTimeout(atualizar,2000);
</script></body></html>)HTML";
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.sendHeader("X-Frame-Options", "DENY");
  servidor.send(200, "text/html; charset=utf-8", p);
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
  // Nao formata uma memoria que ja foi inicializada pela versao anterior.
  Preferences config;
  bool nvs = config.begin("scbev", false), pronta = nvs && config.getBool("fs_pronto", false);
  memoriaOk = LittleFS.begin(false);
  if (!memoriaOk && nvs && !pronta) memoriaOk = LittleFS.format() && LittleFS.begin(false);
  if (memoriaOk && nvs && !pronta) config.putBool("fs_pronto", true);
  config.end();
  if (!memoriaOk || !fila) falhaMemoria();
  if (xTaskCreate(lerSensor, "sensor", 4096, nullptr, 2, nullptr) != pdPASS) falhaMemoria();
  servidor.on("/", HTTP_GET, pagina);
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
    registrar("INFOS", base.c_str()); // Permite datar os eventos iniciais SEM_HORA do mesmo boot.
  }
  salvarRegistros();
  if (servidorAtivo) servidor.handleClient();
  delay(2);
}
