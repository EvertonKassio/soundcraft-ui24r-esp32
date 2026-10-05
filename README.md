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
- **Canais:** 24 entradas, em uma grade fixa de cinco colunas por cinco
  linhas (24 canais e um espaco para configuracao). Cada botão abre/fecha a entrada, respeitando o estado de mute
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
  com margens superior e direita de 12 pixels.
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

Na aba Solo, um toque no canal ativa o teste; outro toque restaura apenas
esse canal. N�o h� sele��o seguida de ativa��o de v�rios canais. Outros
canais podem ser abertos/fechados na aba Canais durante o teste.

Antes de enviar comandos, o estado original � salvo em NVS. O firmware
zera o main, muta os envios auxiliares fora da sele��o de escuta e os
quatro envios FX, e s� depois abre o mute da entrada e ativa forceunmute
para superar grupos de mute. Os auxiliares de escuta s�o considerados
pr�-fader e seu PRE/POST, n�vel de envio e mute n�o s�o alterados.
Os masters auxiliares e os demais canais permanecem inalterados.

O segundo toque restaura em ordem inversa: forceunmute e mute anteriores,
FX, envios auxiliares e main. Cada canal usa o mesmo transporte em lote
dos controles normais, com atualiza��o local imediata. A confirma��o da
restaura��o � acompanhada em segundo plano, sem bloquear outros canais;
o registro persistente s� � removido ap�s observar os valores restaurados.
Um novo teste durante essa espera reutiliza o estado original salvo.
Ap�s desconex�o ou reinicializa��o, os registros restantes s�o restaurados
quando a mesa volta a fornecer seus par�metros.

O bot�o Auxiliares do solo abre uma sele��o de AUX 1�10, salva em NVS.
O padr�o � 4, 6, 7 e 8. N�o � permitido mudar a sele��o enquanto houver
solo ou recupera��o pendente, nem salvar uma sele��o vazia.

Os 24 canais usam uma grade 5 x 5 com botoes de 88 x 74 px.
Na aba Canais, borda significa fechado e preenchimento significa aberto.
Na aba Solo, o preenchimento indica teste ativo. As cores sao escolhidas
somente em Solo > Config. > Cores: toque no canal e escolha na paleta.
A mesma cor aparece em Canais e Solo e fica salva apos reiniciar.

O fader zerado e os mutes de envio n�o reconfiguram rotas alternativas
por matrix, grava��o ou patching; verificar essas rotas na mesa antes
de usar o teste durante o culto. Os envios de fones j� mutados ou zerados
continuam assim. Caudas de efeitos anteriores podem persistir.

## Conexão e cancelamento

A comunicação HTTP/WebSocket roda em uma tarefa separada. Desenho,
toque, edição e estado da mesa permanecem na tarefa da interface;
as duas tarefas trocam mensagens por filas. Esperar pela rede não
interrompe o loop de desenho. As operações TCP têm timeout de
400 ms; uma tentativa completa tem até 8 segundos para Wi-Fi, seguidos
de até 20 segundos para conexão e sincronização inicial com a mesa.

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

O firmware conecta diretamente por WebSocket em `ws://<IP da mesa>:80/`,
sem exigir uma sessão HTTP em `/socket.io/1/`. Usa pacotes
`3:::SETD^chave^valor`, resposta `2::` aos heartbeats e `3:::ALIVE`
a cada segundo. O firmware solicita `INIT` ao abrir o WebSocket e aceita
parâmetros com ou sem o prefixo `3:::`. Os índices são
zero-based: entrada 1 = `i.0`, aux 6 = `a.5`, aux 7 = `a.6`.

Os controles de canais e grupos exibem o estado solicitado imediatamente
após enfileirar o envio, sem bloquear os demais controles. Mensagens
`SETD`/`SETS` recebidas atualizam esse estado durante a operação, inclusive
alterações feitas por outros dispositivos. Não há leitura completa a cada
toque nem reversão por falta de eco do comando. `INIT` é solicitado somente
ao estabelecer uma conexão; após desconexão, o estado é lido novamente.

O solo e sua restauração enviam comandos um por vez e aguardam confirmação
`SETD`. Quatro segundos sem confirmação provocam reconexão para preservar
a recuperação dos valores do solo.
Mensagens de medidores não ocupam a fila de parâmetros. Heartbeats recebidos também contam como
atividade para detectar conexão inativa. O solo não inicia se faltam valores
necessários para salvar e restaurar. O trecho de `network.txt` fornecido
é parcial e não contém os masters auxiliares; esses dados precisam chegar
na sincronização real. Firmware da mesa com comportamento diferente pode
exigir adaptação do handshake ou das confirmações. O monitor serial
(115200 baud) registra abertura e desconexão com o prefixo `[mesa-net]`.
Também registra a primeira mensagem recebida, a conclusão da sincronização
e os parâmetros disponíveis se o prazo esgotar. Durante o snapshot inicial,
a tarefa de rede aguarda espaço na fila de entrada sem descartar parâmetros.
Durante essa espera continua enviando `ALIVE` e comandos pendentes. O parser
percorre os pacotes em uma única passagem e ignora parâmetros não usados.
O toque atualiza imediatamente os controles; textos e cores inalterados
não provocam novas atualizações de estilo.

Referência de conexão direta:
[Websockets and Soundcraft](https://blechtrottel.net/en/jswebsockets.html).

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
solo com um canal e depois com vários. Confirme áudio nos auxs 4/6/7/8,
silêncio nos outros auxs e restauração exata. Repita interrompendo Wi-Fi
e reiniciando o ESP32 durante a sessão e durante a restauração.

A compilação não comprova funcionamento elétrico, roteamento de áudio
ou comunicação com uma mesa física.

## Resultado da compilação

Compilado com sucesso em 05/10/2026 com PlatformIO, plataforma
Espressif32 6.9.0 e as dependências fixadas no `platformio.ini`.
RAM estática interna: 48.500 bytes (14,8%). Flash da aplicação:
1.079.689 bytes (34,3% da partição). Objetos LVGL e buffers de desenho
usam PSRAM. Não foram executados testes na placa ou na mesa.

`bin/firmware.bin` contém apenas a aplicação compilada. Para a primeira
gravação, use `pio run -t upload`, que grava também bootloader e tabela de
partições nos endereços corretos. Não grave esse binário sozinho no
endereço zero.

Referência da API WebSocket utilizada:
[arduinoWebSockets 2.4.1](https://github.com/Links2004/arduinoWebSockets/blob/2.4.1/src/WebSocketsClient.h).
As chaves e o comportamento de Socket.IO foram extraídos do código
`cod_fonte_mesa/index.html` fornecido.

## Avisos de inicialização

O driver de toque TAMC_GT911 1.0.2 está em `lib/TAMC_GT911`, com licença
original e uma adaptação que evita acessar INT/RESET quando não existem
na placa (pinos -1). O journal de recuperação de solo só é lido se a chave
já existir na NVS; sua ausência é normal antes do primeiro solo.

`Core dump data check failed` significa que o diagnóstico armazenado na
partição de core dump não passou na verificação de integridade. Pode ser
um resíduo de gravações anteriores; o aviso isolado não comprova uma nova
falha nem defeito da flash. Essa correção não apaga diagnósticos ou a NVS.

## Repouso do display

A retroiluminação apaga após 10 minutos consecutivos sem comunicação
pronta com a mesa e sem toque, ou após 1 hora sem toque com a mesa
conectada. Após perder a conexão, é necessário completar os 10 minutos
sem conexão mesmo que o último toque seja antigo.

O ESP32 e o registro de recuperacao de solo permanecem ativos.
Qualquer toque acende a tela; esse primeiro gesto é consumido até soltar
o dedo, evitando acionar controles ao acordar. Não é deep sleep do ESP32.

Durante o repouso, as atualizações visuais são pausadas e o laço principal
consulta o toque aproximadamente a cada 50 ms. Sem conexão pronta com a
mesa, o Wi-Fi é desligado sem apagar credenciais ou o journal de solo.
Ao tocar, o rádio é religado e uma tentativa de conexão é iniciada.
Se a mesa continuar ausente, a conexão respeita os limites de tempo normais.
Enquanto conectado, o transporte permanece ativo; quando desabilitado,
sua tarefa aguarda comandos em vez de consultar continuamente a fila.

Referência medida pelo usuário antes desta otimização: 1,15 W com a tela
acesa e 0,4 W com iluminação apagada. O novo consumo precisa ser medido
na placa. Não foi alterado o clock da CPU nem suspenso o controlador RGB.

A ativação e a restauração do solo usam o mesmo transporte em lote dos
controles de canais, em blocos de até 16 alterações. Não aguardam o eco
individual antes de enviar o próximo comando. O estado recebido continua
sendo reconciliado pela comunicação. Na restauração, o journal só é
apagado após observar todos os valores restaurados; a verificação final
não bloqueia o envio. Se essa verificação não terminar em 4 segundos,
a conexão é retomada para recuperar o estado e repetir a restauração.

## Configurações unificadas

O botão de conexão no topo abre as abas Wi-Fi, Aux solo, Cor canal e
Cor grupo. As cores são escolhidas na paleta de oito opções e salvas em
NVS. A mesma cor de canal é usada em Canais e Solo. Na tela Grupos,
borda indica grupo mutado; preenchimento indica grupo aberto.
Os quatro grupos medem 222 x 174 px; reconexão fica abaixo da grade.
Nas telas Canais e Solo, reconexão ocupa a última posição livre da grade.
Nas configurações, o botão de reconexão fica na área inferior, separado
dos campos e da seleção de cores. A busca de redes conserva seus controles
próprios para não iniciar uma conexão enquanto a varredura estiver ativa.

Nas telas de cores, o rodape tem Voltar e Salvar, sem reconexao. A cor escolhida fica marcada na paleta e so e gravada ao tocar Salvar. Voltar sem salvar descarta a escolha.

Em Aux solo e nas telas de cores, Salvar fica a esquerda em verde e Voltar a direita em cinza. Essas telas nao possuem botao de reconexao.

Na aba Wi-Fi, Salvar e conectar fica no final da pagina. Tentar novamente fica no topo, em um botao compacto, e muda para Cancelar tentativa durante a conexao.

Barra principal, abas de configuracao e Tentar novamente usam 108 x 42 px, raio de 6 px e espacamento horizontal de 8 px. Tentar novamente usa duas linhas para manter o texto legivel.
