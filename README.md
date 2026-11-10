# SCBEV

Sistema Ciberfísico de Bloqueio Emergencial de Vazamentos com ESP32.
Projeto do Grupo 10 da disciplina Conectividade em Sistemas Ciberfísicos da PUCPR.
Professor: Fábio Bettio.

O protótipo detecta água em uma placa resistiva e comanda um relé que alimenta
uma solenoide normalmente fechada de 12 V. O ESP32 oferece consulta e comandos
por HTTP na rede local. A bomba tem alimentação separada e não é desligada pelo
firmware.

## Integrantes

- Alysson Henrique Silva Ribas
- Caetano Chueiri Seixas
- Gustavo Vinicius Ribeiro
- Igor Luiz Grando Motta
- Marco Antonio de Oliveira Mattos

## Código e interface

- `firmware/SCBEV_Simples/SCBEV_Simples.ino`: 273 linhas; aquisição, rede,
  interface, relé e persistência.
- `firmware/SCBEV_Simples/Seguranca.h`: 94 linhas; condições de bloqueio,
  abertura e rearme.
- `testes/`: verificações locais da lógica e do JavaScript do histórico.
- [Testes e resultados](docs/TESTES.md): ensaios da versão final e limitações.

O HTML, o CSS e o JavaScript estão no próprio `.ino`. A interface apresenta
leitura, alarme, comando da válvula, IP, MAC, horário e acesso ao histórico.
O navegador consulta a página em segundo plano e copia somente campos,
condições dos botões e tokens, sem recarregar a tela. Não há gráfico nem CSV.
Em falha de comunicação, mostra que os dados são anteriores e desabilita os
botões de abertura e rearme até receber nova resposta.

## Montagem

A bomba retira água do reservatório e a conduz pela mangueira de 3/8 até a
solenoide de 1/2, utilizando adaptadores. Outra mangueira leva a água ao ponto
que simula o vazamento. Há um registro manual de 3/8 na saída e não há tubulação
de retorno. Um cotovelo de 3/8 está disponível para ajustar a direção da mangueira.

| Origem | Destino |
| --- | --- |
| Sensor VCC / GND | 3V3 / GND do ESP32 |
| Sensor AO / DO | GPIO 34 / sem ligação |
| Relé IN | GPIO 19; HIGH liga, LOW desliga |
| Relé 5V / GND | Fonte de 5 V / GND comum ao ESP32 |
| Fonte de 12 V positivo | Contato C do relé |
| Contato NA do relé | Terminal positivo definido da bobina |
| Contato NF do relé | Sem ligação |
| Outro terminal da bobina | Negativo da fonte de 12 V |
| Diodo em paralelo com a bobina | Faixa no lado ligado ao NA; outra ponta no negativo de 12 V |
| Bomba | Fonte de 5 V, independentemente do relé da válvula |

O ESP32 foi conectado por USB durante os ensaios. Não se aplica 12 V aos seus
pinos. O comportamento de acionamento é o do módulo de relé utilizado pelo grupo.
A carga não é alimentada pelo GPIO.

A equipe modificou a borracha e a mola da solenoide para o ensaio de baixa
pressão. Os resultados se referem a esse exemplar e à montagem de bancada,
sem estabelecer uma nova especificação da válvula original.

## Configuração e uso

1. Abra `firmware/SCBEV_Simples/SCBEV_Simples.ino` no Arduino IDE, mantendo
   `Seguranca.h` na mesma pasta.
2. Selecione ESP32 Dev Module e a porta da placa. A compilação de referência
   utilizou Arduino-ESP32 3.3.12 e partição padrão de 4 MB com filesystem.
   As bibliotecas utilizadas pertencem ao pacote ESP32.
3. Na cópia local, substitua `SEU_WIFI` e `SUA_SENHA` pelos dados da rede.
   Mantenha exemplos na cópia publicada, sem senha pessoal.
4. Compile, envie e abra o monitor serial em 115200 baud.
5. Em um dispositivo com acesso à mesma rede, abra o endereço `http://`
   mostrado no monitor serial. O servidor usa TCP, porta 80.
6. Confira sensor, alarme, comando, IP, MAC e histórico. A hora pode ser
   ajustada pelo navegador se necessário.

## Operação

O sistema inicia com fechamento solicitado. Abrir exige rede conectada,
histórico disponível, amostra recente, condição seca confirmada e ausência de
alarme. Detectar água retém o alarme mesmo após secar. O usuário precisa rearmar
e solicitar uma nova abertura. Rearmar e reconectar não abrem automaticamente.

| Parâmetro | Configuração |
| --- | --- |
| Tarefa de leitura | A cada 50 ms |
| Água | ADC abaixo de 4000 por pelo menos 150 ms |
| Seco | ADC acima de 4050 por pelo menos 2000 ms |
| Idade máxima da amostra para abrir/rearmar | 250 ms |
| Tentativa explícita de reconexão | A cada 15 s sem conexão |
| Consulta dos dados no navegador | 2 s após a consulta anterior; timeout de 4 s |
| Histórico | Dois arquivos de até 32768 bytes, com rotação |

Esses intervalos são configurações lógicas, não tempos hidráulicos medidos.
O ADC fornece uma leitura bruta, não porcentagem de umidade.

## Comunicação e registros

| Método | Rota | Função |
| --- | --- | --- |
| GET | `/` | Página principal |
| POST | `/abrir` | Solicitar abertura |
| POST | `/fechar` | Solicitar fechamento |
| POST | `/rearmar` | Liberar alarme quando permitido, mantendo fechamento |
| POST | `/hora` | Ajustar horário pelo navegador |
| GET | `/historico` | Página do histórico |
| GET | `/registros` | Registros em texto |

Os comandos POST exigem o token do formulário. Não há autenticação por usuário
nem HTTPS; o token não substitui esses mecanismos. Uso previsto: rede local.

O LittleFS guarda níveis `INFOS`, `AVISOS` e `ERRO`, horário, identificador do
boot e tempo desde a inicialização. A hora vem de NTP ou do navegador. Antes da
sincronização, usa `SEM_HORA`. O histórico pode reconstruir horários do mesmo
boot com uma base válida e os marca com asterisco. Eventos de um boot que nunca
obteve hora válida permanecem sem data conhecida.

## Validação e limites

A equipe compilou e executou a versão final em 09/11/2026. Foram verificados
comandos manuais, bloqueio de água por detecção, retenção e rearme, registros
após reinício, ping, TCP 80, HTTP 200, vinte consultas sequenciais e erro 404.
A queda deliberada do Wi-Fi não foi testada nesta versão. O retorno da conexão
após reiniciar não substitui esse teste. Os resultados estão em `docs/TESTES.md`;
as capturas foram reunidas no relatório acadêmico entregue separadamente.

A tela mostra o comando elétrico, sem medir posição ou vazão. Algumas falhas de
ligação podem produzir leitura semelhante a seco; o sistema não diagnostica
todas as falhas. Não houve medição de pressão, vazão ou tempo físico de corte,
validação prolongada ou validação para instalação residencial.
