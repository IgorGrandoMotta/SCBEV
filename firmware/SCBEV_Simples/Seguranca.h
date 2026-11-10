#pragma once
#include <stdint.h>

// Logica independente de Wi-Fi e de HTML, testavel no computador.
// Todos os acessos no ESP32 ficam protegidos por uma secao critica curta.
class Seguranca {
 public:
  enum Evento : uint32_t {
    NENHUM = 0, AGUA = 1, SECO = 2, REDE_CAIU = 4,
    REDE_VOLTOU = 8, FECHOU = 16, FALHA_MEMORIA = 32
  };

  int leitura = 4095;
  bool molhado = false;
  bool secoConfirmado = false;
  bool alarme = false;
  bool aberta = false;  // COMANDO eletrico, nao posicao fisica medida.
  bool conectado = false;
  bool memoriaOk = false;
  bool temAmostra = false;
  uint32_t ultimaAmostra = 0;

  uint32_t atualizar(int valor, uint32_t agora, bool rede, bool memoria,
                     int limiteAgua = 4000, int limiteSeco = 4050,
                     uint32_t tempoAgua = 150, uint32_t tempoSeco = 2000) {
    uint32_t eventos = NENHUM;
    if (rede != conectado) eventos |= rede ? REDE_VOLTOU : REDE_CAIU;
    if (memoriaOk && !memoria) eventos |= FALHA_MEMORIA;
    conectado = rede;
    memoriaOk = memoria;
    leitura = valor;
    ultimaAmostra = agora;
    temAmostra = true;

    if (valor < limiteAgua) {
      secoConfirmado = false;
      contandoSeco = false;
      if (!contandoAgua) { contandoAgua = true; inicioAgua = agora; }
      if (!molhado && uint32_t(agora - inicioAgua) >= tempoAgua) {
        molhado = true;
        alarme = true;
        eventos |= AGUA;
      }
    } else {
      contandoAgua = false;
      if (valor > limiteSeco) {
        if (!contandoSeco) { contandoSeco = true; inicioSeco = agora; }
        if (uint32_t(agora - inicioSeco) >= tempoSeco) {
          secoConfirmado = true;
          if (molhado) { molhado = false; eventos |= SECO; }
        }
      } else {
        // Faixa de histerese: nao prova que a placa esta seca.
        contandoSeco = false;
        secoConfirmado = false;
      }
    }
    if (molhado || alarme || !conectado || !memoriaOk) {
      if (aberta) { aberta = false; eventos |= FECHOU; }
    }
    return eventos;
  }

  bool amostraRecente(uint32_t agora, uint32_t limiteIdade = 250) const {
    return temAmostra && uint32_t(agora - ultimaAmostra) <= limiteIdade;
  }

  bool podeAbrir(uint32_t agora, uint32_t limiteIdade = 250) const {
    return conectado && memoriaOk && secoConfirmado && !molhado && !alarme &&
           amostraRecente(agora, limiteIdade);
  }

  bool abrir(uint32_t agora, uint32_t limiteIdade = 250) {
    if (!podeAbrir(agora, limiteIdade)) return false;
    aberta = true;
    return true;
  }

  void fechar() { aberta = false; }

  bool rearmar(uint32_t agora, uint32_t limiteIdade = 250) {
    if (!secoConfirmado || molhado || !memoriaOk || !conectado ||
        !amostraRecente(agora, limiteIdade)) return false;
    alarme = false;
    // Rearmar NAO abre a valvula.
    return true;
  }

 private:
  bool contandoAgua = false;
  bool contandoSeco = false;
  uint32_t inicioAgua = 0;
  uint32_t inicioSeco = 0;
};
