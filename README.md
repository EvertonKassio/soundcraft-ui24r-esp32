# Controle Ui24R — ESP32-4848S040C_I

Projeto independente em PlatformIO/Arduino, com LVGL 8.3, painel RGB
ST7701 de 480 × 480 e toque GT911. Do projeto anterior foram copiados
somente o driver/configuração de display e toque, configuração LVGL e
parâmetros de compilação da placa. O código de interface e comunicação
foi escrito novamente. As pastas de origem permanecem intactas.

## Telas

- **Grupos:** quatro botões grandes para os grupos de mute 1 (vocal),
  2 (instrumentos), 3 (bateria) e 4 (microfones sem fio). As associações
  de canais vêm da mesa: configure esses grupos na interface original.
  Verde indica grupo desativado; vermelho, grupo ativado. Um grupo aberto
  não remove o mute individual de um canal. Este firmware controla grupos
  de mute, não faders de subgrupos ou VCA.
- **Canais:** 24 entradas, em uma grade fixa de quatro colunas por seis
  linhas. Cada botão abre/fecha a entrada, respeitando o estado de mute
  individual e grupos. Abrir uma entrada bloqueada pelo grupo usa a
  exceção `forceunmute`, como a interface da mesa. Cinza indica estado
  indisponível; verde aberto; vermelho fechado. Nomes longos são truncados.
  O número do canal aparece na primeira linha e o nome recebido em
  `SETS^i.N.name^...` na segunda. Antes da conexão/sincronização aparece
  `...`; um nome recebido vazio aparece como `Sem nome`. Renomear um
  canal na mesa atualiza a tela quando a mensagem correspondente chega.
- **Solo:** mesma grade para selecionar uma ou várias entradas, seguida
  de um botão de iniciar/encerrar. A seleção fica bloqueada enquanto ativo.
- **Rede:** acessível pelo símbolo de Wi-Fi no canto superior direito.
  O ícone fica verde com Wi-Fi conectado e vermelho sem conexão. Toque
  em **Rede** para buscar e selecionar as redes disponíveis. A busca é
  assíncrona; redes repetidas aparecem uma vez, ordenadas por sinal, em
  páginas fixas de seis redes com botões anterior/próxima e nova busca.
  As redes aparecem em linhas planas com divisórias, nome na primeira
  linha e proteção/sinal na segunda. A seleção atual tem destaque discreto.
  Setas de paginação têm 42 × 42 pixels e indicador centralizado.
  A busca aguarda 700 ms para o rádio estabilizar e usa até 360 ms por
  canal. Se não encontra redes ou falha, repete automaticamente até três
  varreduras, indicando o progresso. Resultados anteriores permanecem
  visíveis durante uma nova busca; a lista é atualizada ao concluir.
  Iniciar uma busca pausa a comunicação com a mesa, interrompe tentativas
  de conexão e desconecta também o Wi-Fi atual, sem apagar credenciais.
  Não há reconexão durante ou após a busca até tocar em Salvar e conectar
  ou Tentar conexão novamente. Os
  registros `[wifi-scan]` no monitor serial mostram início, resultado
  (`-1`: em andamento; `-2`: falha; `0`: sem redes), duração e estado Wi-Fi.
  O campo Rede tem o mesmo visual e alinhamento de Senha e IP/host;
  tocá-lo abre a seleção de redes. O ícone superior mede 42 × 42 pixels,
  com margens superior e direita de 15 pixels.
  Senha e IP/nome da mesa abrem um editor dedicado sobre a tela, com
  teclado visível e botões Aplicar/Cancelar. Aplicar atualiza o campo;
  o editor de IP usa apenas dígitos, ponto e apagar. O teclado está
  alinhado explicitamente ao topo do editor, inteiramente acima dos botões.
  Salvar e conectar grava as configurações. Dados são gravados em NVS e usados nas
  próximas inicializações. Senha vazia sem edição mantém a senha anterior;
  para removê-la, digite e apague algum caractere. IP sem protocolo ou porta.
  Selecionar outra rede limpa a senha em edição e não reutiliza a senha da
  rede anterior. A seleção lista redes com SSID anunciado; redes ocultas
  não aparecem. A busca interrompe a conexão atual para liberar o rádio.

Não há rolagem nas telas operacionais. A edição de texto pode deslocar
horizontalmente o conteúdo do próprio campo. A navegação fica sempre
disponível no rodapé; sair da tela de solo não encerra o solo.

## Comportamento do solo

Conforme solicitado, a mixagem dos fones é preservada: os outros canais
continuam nos auxs 6/7, e nenhum nível de envio ou volume desses masters
é alterado. O firmware:

1. Salva em NVS os valores recebidos da mesa antes de enviar alterações.
2. Muta os masters auxiliares 1–5 e 8–10 globalmente.
3. Coloca temporariamente os envios dos canais selecionados aos auxs 6/7
   em pré-fader, para permitir escuta com o main zerado.
4. Muta os quatro envios de efeitos dos canais selecionados para evitar
   que esses instrumentos continuem chegando ao main pelos retornos FX.
5. Zera `i.N.mix` dos selecionados. Não usa o PFL/solo nativo da mesa.
6. Ao encerrar, restaura tudo em ordem inversa, abrindo os masters
   auxiliares somente após restaurar os canais.

Não abre entradas que já estavam mutadas, nem os auxs 6/7 se já estavam
mutados. Um envio originalmente pós-fader passa a pré-fader durante o
solo; embora seu valor permaneça igual, o nível percebido pode mudar.
Caudas de efeitos já existentes podem continuar audíveis por algum tempo.
Roteamentos físicos alternativos, matrix, gravação e subgrupos não são
reconfigurados pelo firmware.

O diário só é apagado após confirmação dos valores restaurados. Depois
de queda de Wi-Fi ou reinicialização, a aplicação tenta reconectar e,
quando consegue, restaura automaticamente a sessão anterior. Ao esgotar
o tempo de conexão, use Tentar conexão novamente. Enquanto o ESP32 estiver desligado
ou sem rede, não pode restaurar a mesa. Não altere externamente os
parâmetros envolvidos durante o solo: a restauração reaplica o snapshot
anterior. Configurar outra mesa/rede fica bloqueado durante a sessão.

## Conexão e cancelamento

A comunicação HTTP/WebSocket roda em uma tarefa separada. Desenho,
toque, edição e estado da mesa permanecem na tarefa da interface;
as duas tarefas trocam mensagens por filas. Esperar pela rede não
interrompe o loop de desenho. As operações TCP e HTTP têm timeout de
400 ms; uma tentativa completa tem até 8 segundos para Wi-Fi, seguidos
de até 5 segundos para conexão e sincronização inicial com a mesa.

Durante as tentativas, as telas operacionais ficam esmaecidas com uma
contagem regressiva e **Cancelar / corrigir**, que abre as configurações.
Nas configurações a edição permanece disponível, junto do botão
**Cancelar tentativa** ou **Tentar conexão novamente**. Salvar e conectar
usa os dados em edição; Tentar conexão novamente usa os dados já salvos.
Ao esgotar o tempo, as tentativas são pausadas até uma nova solicitação.
Cancelar a tentativa com a mesa conserva o Wi-Fi conectado. Mensagens
de tentativas anteriores são descartadas para não aplicar estado antigo.

As configurações mostram **IP do display** e **Gateway**, atualizados
após conexão com Wi-Fi, mesmo sem comunicação com a mesa. Sem Wi-Fi,
os dois campos mostram `--`.

O diário de restauração do solo continua salvo ao cancelar ou esgotar
o tempo. A próxima conexão bem-sucedida restaura a sessão pendente;
cancelar a rede não desfaz alterações da mesa enquanto não há comunicação.

## Comunicação

O `index.html` fornecido usa Socket.IO 0.9: handshake HTTP
`/socket.io/1/`, transporte `/socket.io/1/websocket/<sessão>`, pacotes
`3:::SETD^chave^valor`, resposta `2::` aos heartbeats e `3:::ALIVE`
a cada segundo. O firmware solicita `INIT` ao conectar. Os índices são
zero-based: entrada 1 = `i.0`, aux 6 = `a.5`, aux 7 = `a.6`.

Os botões usam estado recebido, não estado otimista. Comandos são
enviados um por vez e aguardam confirmação `SETD`; falta de confirmação
em quatro segundos provoca reconexão. O solo não inicia se faltam valores
necessários para salvar e restaurar. O trecho de `network.txt` fornecido
é parcial e não contém os masters auxiliares; esses dados precisam chegar
na sincronização real. Firmware da mesa com comportamento diferente pode
exigir adaptação do handshake ou das confirmações.

## Compilar e gravar

Abra esta pasta como projeto PlatformIO e execute:

```powershell
pio run
pio run -t upload
pio device monitor -b 115200
```

Configuração padrão: rede `Soundcraft Ui24`, senha vazia, mesa
`10.10.1.1`. Configure pela tela se sua rede for diferente. Credenciais
ficam na NVS padrão, sem criptografia configurada neste projeto.

O perfil usa ESP32-S3, flash de 16 MB e PSRAM OPI de 8 MB, de acordo
com a configuração do projeto fornecido. Pinagem, temporização RGB,
sequência ST7701 e orientação estão em `src/board_config.h` e
`src/display.cpp`. Confirme a revisão física da placa C_I antes de gravar.

## Validação na bancada

Primeiro use uma cena de teste e saídas sem amplificação. Confira toque
nos quatro cantos, SSID salvo após reiniciar, estados atualizados após
mudar mutes no navegador e as associações dos quatro grupos. Anote níveis
main, estados dos masters auxiliares, pré/pós e mutes FX antes de testar
solo com um canal e depois com vários. Confirme áudio nos auxs 6/7,
silêncio nos outros auxs e restauração exata. Repita interrompendo Wi-Fi
e reiniciando o ESP32 durante a sessão e durante a restauração.

A compilação não comprova funcionamento elétrico, roteamento de áudio
ou comunicação com uma mesa física.

## Resultado da compilação

Compilado com sucesso em 03/10/2026 com PlatformIO, plataforma
Espressif32 6.9.0 e as dependências fixadas no `platformio.ini`.
RAM estática interna: 48.572 bytes (14,8%). Flash da aplicação:
1.102.377 bytes (35,0% da partição). Objetos LVGL e buffers de desenho
usam PSRAM. Não foram executados testes na placa ou na mesa.

`bin/firmware.bin` contém apenas a aplicação compilada. Para a primeira
gravação, use `pio run -t upload`, que grava também bootloader e tabela de
partições nos endereços corretos. Não grave esse binário sozinho no
endereço zero.

Referência da API WebSocket utilizada:
[arduinoWebSockets 2.4.1](https://github.com/Links2004/arduinoWebSockets/blob/2.4.1/src/WebSocketsClient.h).
As chaves e o comportamento de Socket.IO foram extraídos do código
`cod_fonte_mesa/index.html` fornecido.
