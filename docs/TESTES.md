# Testes da versão final

Os ensaios abaixo foram refeitos em 09/11/2026 com o firmware de 273 linhas e
`Seguranca.h` de 94 linhas. As evidências da interface anterior não foram usadas
para aprovar essa versão. As capturas estão no relatório acadêmico do grupo.

Cliente: Windows 11, 192.168.0.94. ESP32: 192.168.0.254,
MAC 34:AB:95:EB:9A:60. Ferramentas: Arduino IDE, navegador, CMD,
Test-NetConnection e curl.

| Ensaio | Resultado | Evidência |
| --- | --- | --- |
| Compilação e execução | Código gravado e interface final em operação | Capturas e confirmação da equipe |
| Sensor seco | ADC 4095 | Captura 22:02:45 |
| Abertura manual | Abertura solicitada e passagem de água | Captura 22:03:25 e observação da equipe |
| Fechamento manual | Fechamento solicitado e interrupção do fluxo | Captura 22:04:04 e observação da equipe |
| Vazamento | ADC 2318, alarme retido e fechamento automático | Captura 22:05:40; corte de água observado |
| Secagem com alarme retido | ADC 4095, abertura bloqueada | Captura 22:06:19 |
| Rearme | Alarme livre e fechamento mantido | Captura 22:06:44 |
| Novo ciclo manual | Abertura 22:07:36 e fechamento 22:07:39 | Histórico e observação da equipe |
| Persistência após reinício | Eventos anteriores preservados e novo boot às 22:09:09 | Captura do histórico |
| Ping | 4/4 respostas, perda 0%; mínimo 36 ms, máximo 79 ms, média 55 ms | Captura do CMD |
| TCP 80 | TcpTestSucceeded: True | Captura do CMD |
| GET da página | HTTP/1.1 200 OK | Captura do CMD |
| Consultas consecutivas | 20 de 20 consultas sequenciais com HTTP 200 | Captura do CMD |
| Rota inexistente | HTTP 404 e nível ERRO às 22:12:08 | Capturas do CMD e histórico |
| Queda deliberada do Wi-Fi | Não executado na versão final | Limitação de acesso às configurações do roteador |

O histórico registrou secagem às 22:06:10, nova detecção às 22:06:14 e secagem às
22:06:16. As transições não determinam a causa da variação da leitura. O alarme
permaneceu retido até o comando de rearme às 22:06:40.

O retorno do Wi-Fi após reiniciar foi observado, mas não prova recuperação de
uma queda de rede durante a operação. O ensaio deliberado foi omitido porque
interromper a alimentação do roteador havia provocado demora de recuperação,
e o usuário não tinha acesso ao aplicativo/configuração para desligar só o Wi-Fi.

A interrupção da água foi uma observação qualitativa da equipe. As telas mostram
comandos, não posição ou vazão. Não foram medidos pressão, volume perdido ou
tempo físico de corte. Consultas sequenciais não caracterizam concorrência,
carga máxima ou disponibilidade contínua.

## Repetir os testes de rede no Windows CMD

```bat
ping -n 4 192.168.0.254
powershell -NoProfile -Command "Test-NetConnection 192.168.0.254 -Port 80"
curl.exe -sS --max-time 10 -D - -o NUL http://192.168.0.254/
for /L %i in (1,1,20) do @curl.exe -sS --max-time 5 -o NUL -w "Teste %i: HTTP %{http_code}\n" http://192.168.0.254/
curl.exe -sS --max-time 10 -D - -o NUL http://192.168.0.254/teste-inexistente
```

Use o IP indicado pela placa na sessão. O `for` acima é para o CMD interativo;
em arquivo `.bat`, escape os sinais de porcentagem conforme a sintaxe do CMD.

## Testes locais de software

A partir da raiz, com g++ e Node.js instalados:

```sh
g++ -std=c++17 -Wall -Wextra -Werror testes/teste_seguranca.cpp -o /tmp/scbev_teste_seguranca
/tmp/scbev_teste_seguranca
node testes/teste_historico.cjs
```

Essas verificações passaram para partida fechada, confirmação temporal, alarme
retido, rearme sem abertura, histerese, perda/retorno de rede simulados, falha de
memória, amostra vencida e virada do contador. O JavaScript real do histórico foi
verificado para reconstrução de hora do mesmo boot, preservação de hora desconhecida,
texto literal e falha HTTP simulada. Não substituem os ensaios físicos.

A atualização parcial da página também foi conferida localmente com estados
simulados: valores e cores, preservação dos controles, falha e retorno da comunicação,
botões e troca do token após reinício. O usuário confirmou funcionamento na placa.

A compilação de referência da versão simples anterior usou Arduino-ESP32 3.3.12,
1.011.205 bytes de programa e 49.012 bytes de variáveis globais. Não houve nova
medição desses tamanhos para o sketch final; não são métricas de RAM máxima.
