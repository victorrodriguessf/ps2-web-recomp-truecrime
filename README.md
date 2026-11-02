# True Crime: New York City no navegador

Recompilação estática de **True Crime: New York City** (PlayStation 2) para rodar no Chrome, feita a partir do projeto [ps2-web-recomp](https://github.com/snuri00/ps2-web-recomp), de [snuri00](https://github.com/snuri00).

## Por que este projeto existe

True Crime: New York City foi um dos jogos da minha infância. Quando descobri que dava para traduzir o código de um jogo de PS2 para WebAssembly, em vez de emulá-lo instrução por instrução, resolvi entender como funcionava tudo isso. 

Este é um projeto de estudo, sem fins comerciais. O objetivo foi aprender sobre recompilação estática, sobre a arquitetura do PS2 e sobre o que acontece quando um jogo usa o hardware de um jeito que o runtime ainda não conhecia. O trabalho foi feito com a ajuda do [Claude Code](https://claude.com/claude-code), que aparece como coautor nos commits.

**Nenhum dado do jogo está neste repositório.** Só há código-fonte. A imagem do disco, o código traduzido e os builds ficam na máquina de quem roda o projeto, e a imagem precisa ser de um disco próprio.

## Situação atual

O jogo roda dos menus até a jogabilidade: tela de título com a cidade em 3D, criação de save no memory card, fase inicial com personagem, HUD e tutorial.

| O que foi verificado | Como |
|---|---|
| Menus, controle e save | Sessão automática com capturas de tela e partida real no Chrome |
| Tela de configuração de joystick | 27 verificações automáticas no Chrome com um controle simulado |
| Taxa de quadros | Medida no Chrome sem janela, em um Mac com chip Apple M5, na tela de título |

**O que ainda não foi verificado:**

- O jogo inteiro. Os testes automáticos cobrem os menus e a fase inicial, e a cidade aberta pode revelar novos problemas.
- O input lag de ponta a ponta. Foram medidas as etapas internas, não o tempo entre apertar o botão e a imagem mudar na tela.
- Gran Turismo 4 e God of War II com a correção do VIF. Os dois são os jogos do projeto original e não foram testados de novo aqui.

O log ainda mostra `[iop] out of memory allocating 0x600000`. Nenhum efeito foi observado, mas a causa não foi investigada.

## Como a técnica funciona

O processador principal do PS2, o Emotion Engine, não é interpretado. O `eerecomp` lê o executável do jogo, acompanha o fluxo de controle e gera C++ para cada função. Esse C++ é compilado para WebAssembly, então o código original do jogo passa a rodar traduzido, antes mesmo de a página abrir.

O resto do console é reimplementado pelo runtime: o kernel do EE em alto nível, o processador de E/S (IOP) interpretando os módulos IRX do próprio disco, as unidades vetoriais, o som e a decodificação de vídeo. A parte gráfica roda em compute shaders de WebGPU, e o disco é lido sob demanda por requisições HTTP Range. Não é preciso BIOS.

Como a análise estática não enxerga tudo, o `port.sh` também executa o jogo sem janela e usa essas execuções para descobrir funções alcançadas só por ponteiros e gravar os microprogramas das unidades vetoriais. Os detalhes da técnica estão no [README original](README.upstream.md).

## Como o port foi feito

Só dois jogos tinham passado por esse caminho antes, então o trabalho foi rodar, ler os logs, achar o próximo bloqueio e corrigir. Foram quatro etapas.

### 1. O executável de boot era só um carregador

A primeira execução parou logo depois da abertura:

```
[ee] halted: program asked to run another executable
```

O `SYSTEM.CNF` aponta para `SLUS_211.06` (1,4 MB), mas ele apenas mostra a abertura e passa o controle para o `TC2.ELF` (4,2 MB), onde está o jogo. Os dois carregam no mesmo endereço, `0x00100000`, então não dá para ter os dois na mesma tradução.

**Solução:** traduzir o `TC2.ELF` no lugar do carregador, usando o `games/<nome>/core.elf` que o `port.sh` já aceita. Foi uma mudança sem alterar código, e o preço é pular a abertura original.

### 2. Um módulo do IOP com o próprio laço de RPC

Com o executável certo, apareceu no log:

```
[iop] missing sifcmd #20 called from 0x00061F10
```

Pela desmontagem, o `NSTMMAN.IRX` atende chamadas com um laço próprio em vez de usar `sceSifRpcLoop`: pega um pedido, executa e dorme quando a fila está vazia. Nenhuma das duas funções existia, então a thread dormia para sempre e a chamada ficava sem resposta.

**Solução:** implementar `sceSifGetNextRequest` e `sceSifExecRequest` em `runtime/src/iop/sif.cpp`, e acordar a thread quando chega um pedido. Isso corrigiu o RPC, mas não o problema seguinte, que era independente.

### 3. A corrupção de imagem: dados dentro das tags DMA

Este foi o bug principal. Depois da tela do memory card, a imagem congelava corrompida, com milhares de mensagens como esta:

```
[vif1] unknown command 0xBEDEFBA9
```

Esses "comandos" eram números de ponto flutuante, ou seja, dados de vértices sendo executados como instruções. O bit mais alto de um float negativo coincide com o bit de interrupção do VIF, então cada um deles também podia paralisar a unidade.

Um rastreio do fluxo mostrou que o jogo divide um comando `UNPACK` entre várias partes da cadeia DMA:

```
UNPACK V4-8 com 72 elementos, precisa de 72 palavras:
  REF com 17 qwords             68 palavras
  parte alta da tag CNT seguinte 2 palavras
  dados da CNT                   2 palavras
```

O runtime entregava toda tag ao VIF como duas palavras zeradas seguidas da parte alta. Fora de um comando, isso é inofensivo, pois as duas viram NOPs. No meio de um `UNPACK`, elas viravam dados: o comando terminava duas palavras antes da hora, e o resto dos vértices era lido como comandos.

**Solução:** quando a tag chega no meio de um comando, só a parte alta segue para o VIF (`vif::transfer_tag` em `runtime/src/vif.cpp`). O resultado em 3600 frames:

| | Antes | Depois |
|---|---|---|
| Comandos VIF inválidos | 23.124 | 0 |
| Primitivas desenhadas | 337 mil | 79,6 milhões |
| Imagem | congelada e corrompida | cidade em 3D, em movimento |

### 4. Passar da tela de título

As entradas automáticas padrão do `port.sh` apertam start no frame 300, quando o jogo ainda está na tela do memory card, e por isso a exploração nunca saía do "Press START". Bastou uma sequência própria de botões, começando no frame 700, para ela chegar à jogabilidade e gravar os programas VU do jogo.

## Ajustes para jogar

**Input lag.** A página lia o controle uma vez por frame exibido. Agora lê a cada 4 ms (cerca de 290 leituras por segundo na medição), e o teclado chega ao jogo no próprio evento. O menu também ganhou a opção **Low latency**, ligada por padrão, que deixa o emulador no máximo 1 frame à frente da GPU, em vez de 2, e reduz a fila de áudio. Se aparecer engasgo ou chiado, basta desligá-la.

**Configuração de joystick.** Em *Menu → Configure gamepad* dá para remapear botões, gatilhos, direcional e analógicos, inclusive em controles que o navegador não reconhece e que informam gatilhos ou direcional como eixos. Também há zona morta, inversão do eixo vertical e vibração. Cada tipo de controle tem a própria configuração salva, e nada chega ao jogo enquanto a tela está aberta.

**Resolução.** O menu oferece três opções, e o padrão continua em 640×512 porque a taxa de quadros cai bastante nas maiores, o que aumentaria justamente o input lag:

| Resolução | Taxa de quadros | Fila de áudio |
|---|---|---|
| 640×512 | 60 fps estáveis | 53 a 65 ms com Low latency, 78 a 90 ms sem |
| 1280×1024 | 35 a 42 fps | 0 ms, sem folga |
| 1920×1536 | cerca de 22 fps | 0 ms, sem folga |

Esses números são de uma medição sem janela e podem ser diferentes em uma janela normal. Para conferir na sua máquina, ligue *Show stats* no menu.

## Como rodar

Testado em um Mac com Apple silicon. O bash do sistema é antigo demais para os scripts, então o do Homebrew precisa vir primeiro no `PATH`. A primeira execução também baixa o Emscripten e o wgpu-native, então demora mais.

```bash
brew install bash cmake ninja sevenzip
export PATH=/opt/homebrew/bin:$PATH

git clone https://github.com/victorrodriguessf/ps2-web-recomp-truecrime.git
cd ps2-web-recomp-truecrime
git switch truecrime-nyc

ISO="$HOME/Desktop/True Crime - New York City (USA).iso"

# O jogo real é o TC2.ELF, não o executável de boot
python3 -I scripts/disc.py extract "$ISO" games/truecrime-nyc/disc TC2.ELF
cp games/truecrime-nyc/disc/TC2.ELF games/truecrime-nyc/core.elf

# Sequência de botões que passa da tela de título e dos menus
A=(); for ((f=700; f<14000; f+=200)); do A+=(--press "${f}:start" --press "$((f+100)):cross"); done
scripts/port.sh truecrime-nyc "$ISO" --frames 14400 --rounds 4 "${A[@]}"

python3 scripts/serve.py build/web-truecrime-nyc games/truecrime-nyc/disc.iso
```

Depois é só abrir http://localhost:8620/ no Chrome. O servidor só aceita conexões da própria máquina.

| Ação | Teclado |
|---|---|
| Start / Select | Enter / Backspace |
| Cross / Circle | Z / X |
| Square / Triangle | A / S |
| L1 / R1 | Q / W |
| L2 / R2 | 1 / 2 |
| Direcional | setas |
| Analógico esquerdo | I, J, K, L |
| Tela cheia | F |

O teclado não tem teclas para o analógico direito, que o jogo usa para mirar, então um controle é a melhor forma de jogar.

## Aviso legal

Este é um projeto pessoal de estudo, sem fins comerciais e sem vínculo com a Activision, a Sony ou qualquer empresa do jogo. True Crime e PlayStation são marcas de seus respectivos donos.

O repositório contém apenas código-fonte. O C++ gerado e o WebAssembly são traduções do executável do jogo e pertencem aos donos do jogo, por isso `games/`, `generated/` e `build/` ficam fora do controle de versão. Não publique esses arquivos e use apenas imagens de discos que você possui.

## Créditos

- **[ps2-web-recomp](https://github.com/snuri00/ps2-web-recomp), de snuri00:** o recompilador, o runtime e toda a base técnica. Sem esse projeto, nada disto existiria. O README original está em [README.upstream.md](README.upstream.md).
- **Referências e bibliotecas do projeto original:** PCSX2, Play!, ps2sdk, wgpu-native, Dawn e Emscripten, conforme os créditos do README original.
- **[Claude Code](https://claude.com/claude-code):** ajuda na investigação dos bugs e na implementação.

Código sob a licença MIT, veja [LICENSE](LICENSE).
