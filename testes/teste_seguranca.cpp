#include <cassert>
#include <iostream>
#include <limits>
#include "../firmware/SCBEV_Simples/Seguranca.h"

static void secar(Seguranca &s, uint32_t inicio, bool rede = true, bool memoria = true) {
  s.atualizar(4095, inicio, rede, memoria);
  s.atualizar(4095, inicio + 2000, rede, memoria);
}

int main() {
  Seguranca s;
  assert(!s.aberta && !s.abrir(0));
  s.atualizar(4095, 0, true, true);
  assert(!s.abrir(0)); // Nao basta uma leitura seca ao ligar.
  s.atualizar(4095, 1999, true, true);
  assert(!s.abrir(1999));
  s.atualizar(4095, 2000, true, true);
  assert(s.abrir(2000));

  s.atualizar(3800, 2050, true, true);
  assert(!s.podeAbrir(2050));
  s.atualizar(3800, 2150, true, true);
  assert(s.aberta); // Confirmacao ainda nao terminou.
  auto evento = s.atualizar(3800, 2200, true, true);
  assert((evento & Seguranca::AGUA) && (evento & Seguranca::FECHOU));
  assert(s.molhado && s.alarme && !s.aberta);
  assert(!s.rearmar(2200) && !s.abrir(2200));

  secar(s, 2300);
  assert(!s.molhado && s.secoConfirmado && s.alarme && !s.aberta);
  assert(!s.abrir(4300)); // Secar sozinho nao retira o bloqueio.
  assert(s.rearmar(4300));
  assert(!s.aberta); // Rearmar nao energiza a valvula.
  assert(s.abrir(4300));
  evento = s.atualizar(4095, 4350, false, true);
  assert((evento & Seguranca::REDE_CAIU) && !s.aberta);
  s.atualizar(4095, 4400, true, true);
  assert(!s.aberta); // Reconectar tambem nao reabre.
  assert(s.abrir(4400));
  evento = s.atualizar(4095, 4450, true, false);
  assert((evento & Seguranca::FALHA_MEMORIA) && !s.aberta);
  assert(!s.abrir(4450));

  Seguranca velha;
  secar(velha, 0);
  assert(!velha.abrir(2251)); // Recusa leitura com mais de 250ms.
  velha.atualizar(4095, 2300, true, true);
  assert(velha.abrir(2300));

  Seguranca histerese;
  secar(histerese, 0);
  histerese.atualizar(4020, 2050, true, true);
  assert(!histerese.secoConfirmado && !histerese.abrir(2050));
  histerese.atualizar(4095, 2100, true, true);
  histerese.atualizar(4020, 4000, true, true);
  histerese.atualizar(4095, 4050, true, true);
  assert(!histerese.abrir(4050)); // Estabilidade precisa ser continua.

  Seguranca ruido;
  secar(ruido, 0);
  ruido.atualizar(3800, 2100, true, true);
  ruido.atualizar(4095, 2150, true, true);
  assert(!ruido.molhado && !ruido.alarme); // Pico curto nao vira alarme.

  Seguranca virada;
  uint32_t inicio = std::numeric_limits<uint32_t>::max() - 1000;
  secar(virada, inicio);
  assert(virada.abrir(inicio + 2000)); // Funciona no rollover do millis().
  std::cout << "OK: partida fechada, temporizacao, vazamento, rearme, histerese, "
               "reconexao, falha da memoria, amostra vencida e rollover.\n";
}
