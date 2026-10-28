# ELF Launcher v2.0.5 — Correção da persistência

Data: 28 de outubro de 2026. Alvo: PS5 13.60. Nenhuma consola foi contactada,
instalada ou modificada durante este trabalho.

## A. Correções implementadas

### Seleção de AutoPayloads

Defeitos reproduzidos no código local:

- `setAuto()` alterava `AUTO_NAMES` e o perfil antes de obter a confirmação HTTP.
- `autoRowToggle()` pintava imediatamente a linha como selecionada.
- `profSave()` descartava o callback passado por `setAuto()`, impedindo a
  recuperação visual e deixando operações em lote sem confirmação de conclusão.
- A consulta periódica não se excluía de gravações de perfis e podia aplicar
  uma resposta antiga depois de uma alteração mais recente.
- As rotinas de upload e a API de escrita direta da lista atualizavam apenas
  `auto.list`, permitindo divergência do perfil ativo em disco.

As alterações explícitas são agora enfileiradas sobre o último estado
confirmado. A seleção, o perfil ativo e os contadores só mudam depois do ACK.
Os callbacks são propagados; erros e conflitos são apresentados e o estado
confirmado é recuperado. Um pedido periódico iniciado antes de uma alteração
não pode desfazê-la quando termina.

Todas as escritas da lista, incluindo uploads e `POST /auto-list`, usam o mesmo
caminho para atualizar o perfil ativo. A ordem de bloqueio é sempre perfis,
depois lista. Os perfis inativos são preservados. Há rollback do perfil se a
gravação da lista falhar; entradas inválidas/excesso de capacidade já não podem
ser silenciosamente omitidos num pedido declarado bem-sucedido.

`POST /profiles` também verifica se o perfil ativo coincide com a lista ordenada
enviada em `X-Auto-List`. As duas revisões continuam obrigatórias. `GET /profiles`
inclui `autoRevision`, permitindo repetir uma leitura se duas respostas
pertencerem a versões diferentes da configuração.

### Atraso inicial

O valor de `3000ms` não era uma consequência da lista vazia. O backend lia
`autoDelay` de `settings.json`, usando 3 segundos quando o ficheiro estava
ausente, ilegível ou o valor era inválido. Também podia haver 3 segundos
efetivamente guardados. Sem ler a configuração da consola afetada, não é
possível determinar qual desses casos originou o registo fornecido.

Foram encontrados caminhos que permitiam à interface divergir dessa leitura:
o slider apresentava o valor pedido antes do ACK; a primeira gravação a partir
do PC era rejeitada se `settings.json` ainda não existisse; e abrir a página no
PS5 podia iniciar uma migração automática de valores antigos do navegador,
depois de o worker já ter lido o atraso anterior.

Agora:

- Abrir a interface é uma operação de leitura, incluindo a primeira abertura.
  Não se migram automaticamente valores de `localStorage`.
- Uma alteração explícita e autorizada a partir do PC ou PS5 pode criar
  `settings.json`, condicionada à revisão correspondente ao ficheiro ausente.
- O slider volta ao valor confirmado até receber o ACK. Erros permanecem visíveis.
- O mesmo parser converte 3/5/10 segundos em 3000/5000/10000 ms no backend e na API.
- `GET /settings` inclui `autoDelayMs` e `delaySource` (`settings.json` ou `default`).
- O registo indica a origem do atraso. Apenas o ficheiro ausente usa o padrão
  de 3000 ms. Ficheiros ilegíveis ou valores inválidos produzem erro e impedem
  envios automáticos, em vez de executar silenciosamente com outro atraso.
- As escritas atómicas fazem `fflush` e `fsync` antes da publicação do ficheiro.

O atraso não depende do número de AutoPayloads. Uma lista vazia não apaga nem
altera `settings.json`. O ecrã de inicialização continua a consultar o atraso
restante calculado pelo worker no backend.

### Ficheiros alterados nesta correção

| Ficheiro | Alteração |
|---|---|
| `index.html`, `host/hbinstall/webapp/index.html` | Estado confirmado, filas de alterações, callbacks, perfis, slider e proteção contra respostas antigas |
| `host/hbinstall/main.c` | Sincronização de todas as escritas, revisão conjunta, validação dos perfis, parser do atraso, API e mensagens de erro |
| `host/hbinstall/auto_boot.inc` | Erro explícito quando o atraso persistido não pode ser lido; nenhum envio nessa condição |
| `host/hbinstall/update_impl.inc` | Flush do conteúdo antes de publicar uma gravação atómica |
| `tools/auto-splash-demo.cjs` | Demonstração de 10 segundos; cancelamento coerente durante o envio simulado |
| `tests/config-persistence.test.cjs` | Testes dos eventos e da persistência frontend |
| `tests/build-config-harness.cjs`, `tests/config-backend.*.c` | Testes dos handlers C extraídos diretamente do código de produção |
| `tests/offline-demo.test.cjs`, `tests/auto-boot.test.c` | Demonstração offline e regressões do arranque |

O mecanismo de identidade em tmpfs e a proteção contra duplicações foram
preservados; `auto_journal.inc` não precisou de alterações. Não foi criada uma
segunda rotina de execução de ELF. O layout do ecrã de arranque foi preservado.

## B. Testes locais

Os testes JavaScript usam funções e eventos reais extraídos do frontend, com
transporte HTTP simulado. O teste C da configuração extrai os handlers atuais,
o parser, a validação, SHA-256 e as escritas de produção, utilizando ficheiros
reais em `/tmp` no WSL. O transporte HTTP e as dependências específicas do PS5
são substituídos por adaptadores de teste; isto não constitui teste de rede
ou autorização num PS5 real.

| Teste | Resultado | Evidência | Observações |
|---|---|---|---|
| Marcar NanoDNS pela linha | Aprovado | POST `/profiles`, duas revisões, `X-ELFL: 1`, lista com quatro nomes | Linha e perfil mantêm três até ao ACK |
| Desmarcar NanoDNS | Aprovado | Nova lista e perfil contêm os três nomes anteriores | Frontend e C |
| Selecionar quatro payloads | Aprovado | Handlers C gravam quatro; GET inclui `nanodns.elf` | Ficheiros locais reais |
| Alterar perfil ativo | Aprovado | Perfil DNS e lista ativa coincidem após ACK | Ordem preservada ao reabrir |
| Upload/escrita direta da lista | Aprovado | Caminho de escrita atualiza também o perfil ativo | Perfis inativos preservados |
| Recarregar interface | Aprovado | Nova instância lê os valores confirmados | Nenhum POST automático |
| Atrasos 3/5/10 s | Aprovado | `settings_auto_delay_ms()` e API devolvem 3000/5000/10000 | C e frontend |
| Lista vazia/ausente | Aprovado | Atraso continua a ser 10000 ms | Nenhum reset de settings |
| Falhas de gravação | Aprovado | HTTP 500 simulado/injetado; lista anterior e rollback verificados | Erro visível no frontend |
| Dois navegadores | Aprovado | Segunda alteração com revisão antiga recebe conflito | Não sobrescreve silenciosamente |
| Alterações rápidas | Aprovado | Mutações compostas sobre ACKs sucessivos | Sem perda da segunda seleção |
| Resposta periódica antiga | Aprovado | Resposta iniciada antes da gravação é descartada | Preserva os quatro nomes confirmados |
| GET `/auto-list` | Aprovado | Dez consultas mantêm ficheiro/revisão | SHA-256 real no teste C |
| Alteração efetiva da lista | Aprovado | Nova revisão após remoção | Perfis/lista coerentes |
| Inicialização a partir do PC | Aprovado | POST explícito cria settings ausentes | GET mantém ficheiro ausente |
| Dados antigos do navegador | Aprovado | Abrir com `localStorage` antigo não grava nem substitui PS5 | Inclui primeiro acesso no PS5 simulado |
| Atraso inválido/ilegível | Aprovado | Parser rejeita valor; worker apresenta erro e envia zero | Sem fallback silencioso |
| Marcador legado `103` | Aprovado | Harness do arranque permite a sessão legítima | Reinício simulado pela perda de tmpfs |
| Reabertura/duplicações | Aprovado | Mesma sessão bloqueia nova execução; 32 claims concorrentes | Backend simulado; registo real |
| Espera real de 10 s | Aprovado | Primeiro envio simulado ocorre após pelo menos 10000 ms | Não executa ELF reais |
| Demonstração offline | Aprovado | 1/3/12 itens, contagem, progresso, erro, ignorado e dois cancelamentos | Execução JavaScript numa DOM simulada |
| Inspeção visual em navegador | Não executado | A autorização da origem local foi recusada anteriormente nesta sessão | Testes da DOM não validam o layout visual |
| JavaScript/C e recursos | Aprovado | 37 testes JS; dois harnesses C; build `-Wall -Werror`; HTML embebido comparado byte a byte | Sem dependência de HTML antigo |

Comandos reproduzíveis, a partir da raiz:

```console
node tools/auto-splash-demo.cjs
node --test tests/config-persistence.test.cjs tests/boot-splash.test.cjs tests/offline-demo.test.cjs
node tests/build-config-harness.cjs
```

No WSL, a partir da mesma raiz:

```console
cc -Wall -Wextra -Werror -Wno-unused-parameter -pthread tests/artifacts/config-backend.generated.c -o /tmp/elf-config-test
/tmp/elf-config-test
cc -Wall -Wextra -Werror -Wno-unused-parameter -pthread tests/auto-boot.test.c -o /tmp/elf-auto-boot-test
/tmp/elf-auto-boot-test
```

## C. Testes no PS5 — Pendentes de validação

Antes de começar, preservar uma cópia do ELF funcional e o respetivo nome de
versão. A instalação da nova compilação deve ser manual. Não apagar/reconstruir
`auto.list`, nem reenviar payloads já carregados apenas para testar o ecrã.

| Teste | Procedimento manual | Resultado esperado | Estado inicial |
|---|---|---|---|
| Abrir no PC | Visitar `http://<PS5>:1000`; consultar `/settings`, `/auto-list`, `/profiles` | Interface apresenta a configuração real | **Não testado no PS5** |
| Abrir no PS5 | Abrir o launcher na consola e comparar com o PC | Mesmos nomes, ordem, perfil e atraso | **Não testado no PS5** |
| Marcar NanoDNS | Marcar pela linha e aguardar confirmação; consultar `/auto-list` e `/profiles` | Quatro nomes, incluindo `nanodns.elf`; mesmo perfil ativo | **Não testado no PS5** |
| Desmarcar NanoDNS | Desmarcar e consultar novamente os endpoints | Remoção confirmada em ambos | **Não testado no PS5** |
| Configurar 10 s | Alterar slider; consultar `/settings` | `settings.autoDelay:10`, `autoDelayMs:10000`, `delaySource:"settings.json"` | **Não testado no PS5** |
| Dois navegadores | Editar a partir de PC e PS5 com a mesma revisão inicial | Conflito explícito ou estado atualizado; sem perda silenciosa | **Não testado no PS5** |
| Reinício completo | Após ACKs, reiniciar normalmente e voltar a consultar os endpoints | Quatro nomes/atraso preservados; nova identidade do arranque | **Não testado no PS5** |
| Jailbreak WK | Para testar três ELF, desmarcar NanoDNS explicitamente antes do reinício; executar WK uma vez | Ecrã aparece e mostra a lista persistida | **Não testado no PS5** |
| Contagem real | Observar `/auto-status` e o registo desde o início do worker | Nenhum envio antes dos 10 s configurados | **Não testado no PS5** |
| Três ELF | Acompanhar itens na ordem configurada | Estado individual corresponde às confirmações disponíveis | **Não testado no PS5** |
| `/auto-status` | Consultar durante espera, envio e conclusão | Sessão não vazia, contagem e fase coerentes | **Não testado no PS5** |
| `/boot-auto` | Comparar sessão/fase com `/auto-status` | `done:false` durante execução; terminal coerente | **Não testado no PS5** |
| Reabrir/atualizar | Reabrir e atualizar a página no PC e PS5 na mesma sessão | Nenhum envio adicional nem alteração à configuração | **Não testado no PS5** |
| Lista após arranque | Comparar `/auto-list` e revisão antes/depois da execução | Lista e revisão permanecem intactas | **Não testado no PS5** |
| Cancelar na espera | Num novo arranque de teste, cancelar antes de terminar a contagem | Nenhum pendente é enviado; configurações preservadas | **Não testado no PS5** |
| Cancelar durante envio | Num arranque próprio, cancelar com um item em envio | Envio admitido termina; os seguintes não iniciam | **Não testado no PS5** |
| Falha individual | Usar um cenário de falha controlado num arranque próprio e consultar eventos | Erro visível e restantes tratados quando seguro | **Não testado no PS5** |
| Lista vazia | Guardar uma lista vazia por ação explícita; verificar atraso; restaurar depois via UI | Nenhum envio; 10 segundos continuam persistidos | **Não testado no PS5** |

Registar JSON e eventos para qualquer diferença. Um envio concluído ao elfldr
não comprova que o payload está em execução: a UI mantém essa distinção.

## D. Compilação

Artefacto: `host/hbinstall/elf-launcher-persistence-fix.elf`.

Caminho completo:

```text
C:\_PESSOAL\HSDUTRA CORTEX\Pessoal\Games\Desbloquear PS5 13.60\elf-launcher-main\host\hbinstall\elf-launcher-persistence-fix.elf
```

Compilação com o SDK configurado, a partir de `host/hbinstall`:

```console
make -B PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk ELF=elf-launcher-persistence-fix.elf
```

`-B` força a recompilação. A validação do artefacto compara o HTML atual com
os bytes embebidos no ELF e verifica o cabeçalho ELF:

```console
node tests/check-boot-build.cjs host/hbinstall/elf-launcher-persistence-fix.elf
```

Resultado final: **aprovado**, compilação forçada com `-Wall -Werror`, sem avisos
do compilador. Passaram os 37 testes JavaScript e os dois harnesses C. A validação
do ELF confirmou o cabeçalho e o HTML atual embebido byte a byte.

SHA-256 do ELF entregue:

```text
48745609b3f60a7917ca5df9dcf0821b88ac6829c648a268dcfcb454662a69f2
```

Avisos de conversão LF/CRLF do Git são distintos de avisos do compilador.

Instalação mínima **manual**, após preservar o ELF anterior: colocar a nova
compilação com o nome `elf-launcher.elf` no caminho usado pela instalação,
`/data/elf-launcher/mirror/elf-launcher.elf`. Garantir também que o WK carrega
essa compilação se estiver configurado para outra cópia. Usar o próximo arranque
de teste para o validar, sem reenviar payloads que já estejam em execução.
Nenhuma destas operações foi executada automaticamente.
