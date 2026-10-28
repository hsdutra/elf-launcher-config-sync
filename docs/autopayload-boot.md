# Ecrã de arranque do AutoPayload

O backend mantém a única fila automática. Após `listen(:1000)`, um worker lê
o `auto.list` do PS5 pela ordem persistida, respeita o atraso configurado e usa
o envio existente ao elfldr. O navegador consulta o progresso; não envia ELF.
O catálogo remoto deixou de bloquear o primeiro acesso ao progresso.

## Estado e cancelamento

- `GET /auto-status`: sessão, fase, atraso real restante, total, processados e
  itens (`pending`, `loading`, `success`, `error`, `skipped`, `cancelled`).
- `POST /auto-cancel?session=<sessão>`: exige a proteção existente de origem/
  `X-ELFL: 1` e a sessão atual. Uma sessão antiga recebe HTTP 409; GET recebe 405.
- `success` significa envio concluído ao elfldr. A resposta inclui
  `confirmation: "transport-only"` e `execution_confirmed: false`; a interface
  apresenta **Enviado**, porque não existe confirmação geral de execução.
- O mutex protege os snapshots e a admissão de cada envio. Cancelar impede
  novos envios; um envio já admitido termina normalmente. A espera pelo lock
  do remetente também é cancelável. O servidor e os payloads em execução continuam.
- As falhas e respostas disponíveis são registadas no Registro de eventos.

O estado de execução e o ID aleatório de 128 bits vivem em
`/system_tmp/elf-launcher/`. O código confirma com `statfs` que o caminho está
em `tmpfs` antes de o usar. A identidade sobrevive à reabertura e à substituição
do processo, mas desaparece num reinício completo; não depende do relógio.
Se a memória temporária não estiver disponível, o backend apresenta erro e não
usa um ficheiro persistente como alternativa.

`/data/elf-launcher/boot-auto-done` passa a ser apenas um diagnóstico versionado.
Valores antigos como `103`, dois números ou um conteúdo inválido nunca bloqueiam
um novo arranque. `CLOCK_MONOTONIC` continua a medir o atraso, mas não identifica
arranques diferentes. `kern.boottime` também foi removido desta decisão: esse
valor pode ser ajustado quando o relógio muda ([FreeBSD](https://reviews.freebsd.org/D30116)).

Uma reserva em memória não grava uma falsa conclusão. Se a criação da thread
falhar, a execução não é consumida. O worker grava o estado de espera e, antes
de cada envio, publica atomicamente a admissão no registo temporário. Uma
interrupção anterior à primeira admissão permite reiniciar a espera. Depois de
uma admissão, o resultado pode ser desconhecido: a recuperação apresenta
`phase: "error"`, `done: false` e bloqueia a repetição, preservando os resultados
conhecidos e marcando os restantes como incertos/ignorados.

`GET /boot-auto` e `GET /auto-status` derivam `done`, sessão e fase do mesmo
estado protegido. `done` só é verdadeiro para `completed` ou `cancelled`;
`consumed` indica que existe uma reserva ou que a repetição está bloqueada.
Consultar qualquer um dos endpoints não cria sessões, não envia ELF e não grava
configurações. O marcador de passagem de uma atualização também passou para
`/system_tmp`, evitando reutilizar um timestamp de outro arranque.
O endpoint legado `trigger-auto` mantém compatibilidade com WK, mas deixou de
limpar o marcador; `boot-auto?done=...` deixou de permitir alterações por GET.

O ID de sessão guardado no navegador serve apenas para suprimir a reapresentação
na atualização/reabertura. Configurações, perfis e execução continuam no PS5.
Um navegador diferente pode acompanhar uma execução ativa. Snapshots já
terminados não reapresentam o ecrã. Após observar a conclusão, o resumo fica
visível durante 2,5 segundos; também se pode abrir a interface imediatamente.
O ecrã usa as cores do tema, as dez traduções e a navegação existente do comando.

## Demonstração offline

```console
node tools/auto-splash-demo.cjs
```

Abrir `tests/artifacts/autopayload-demo.html` no PC. Inclui 1, 3 e 12 ELF,
falha, ignorado, cancelamento, idioma e tema. Não faz pedidos de rede nem envia
payloads. O gerador extrai a apresentação atual de `index.html`; a demonstração
e os controlos simulados não são incluídos no ELF. Não existe parâmetro de URL
nem endpoint de produção que ative este modo.

## Verificações

```console
node --test tests/config-persistence.test.cjs tests/boot-splash.test.cjs
cc -Wall -Wextra -Werror -Wno-unused-parameter -pthread tests/auto-boot.test.c -o /tmp/elf-auto-boot-test
/tmp/elf-auto-boot-test
```

Os 26 testes JavaScript verificam persistência, cálculo do progresso, traduções,
reabertura, observadores PC/PS5, falhas de ligação e cancelamento. O harness C
inclui o worker de produção com dependências simuladas e verifica filas de
0/1/3/12/64, falhas, ficheiros ausentes, cancelamento na espera/no envio/no lock,
sessões antigas, falhas de escrita e 32 pedidos concorrentes de arranque.
Os testes usam o código real de leitura/escrita do registo em ficheiros locais,
incluindo o marcador exato `103`, reinício da consola simulado pela perda de
tmpfs, reabertura do processo, falha de criação da thread, interrupção antes/depois
da admissão e consistência dos dois endpoints. Um teste espera efetivamente
10 segundos antes dos três envios simulados. A deteção da montagem, os envios
e a configuração da consola usam dependências de teste.

Compilação com o SDK local, a partir de `host/hbinstall`:

```console
make PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk ELF=elf-launcher-bootfix.elf
```

O artefacto corrigido é `host/hbinstall/elf-launcher-bootfix.elf`. Não é instalado
automaticamente. A compilação usa `-Wall -Werror`.

Validação local: 26 testes JavaScript, harness C e compilação com o SDK do PS5.
A interface HTML não foi alterada nesta correção e mantém-se idêntica à cópia
embebida. Não foi contactada nem alterada nenhuma consola.

Ainda é necessário confirmar num PS5 real o acesso a `/system_tmp` em `tmpfs`,
a conservação do ID ao reabrir/retomar repouso, a sua renovação num reinício
completo, a abertura pelo WK, o WebKit/DualSense e os três envios reais. Os testes
locais simulam esses reinícios; não demonstram o comportamento físico da consola.
