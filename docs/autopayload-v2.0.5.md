# ELF Launcher v2.0.5 — AutoPayload e BootSplash

Entrega local de 30/10/2026. Não foi feita ligação, instalação, execução ou
reinicialização de uma consola. Nenhum payload, perfil ou configuração do PS5
foi alterado. Os testes utilizaram ficheiros temporários e serviços simulados.

## Alterações

- AutoPayload utiliza uma coluna em todas as larguras. A biblioteca geral
  conserva o seu layout. Os selecionados aparecem na ordem real, seguidos
  da secção Disponíveis. Itens selecionados ausentes do disco continuam visíveis.
- Prioridades numéricas, botões Subir/Descer, limites desativados e drag-and-drop
  no PC. As gravações reutilizam a fila de revisões e o foco acompanha o payload.
  A numeração é atualizada após confirmação da gravação, sem recarregar a página.
- Intervalos inteiros de 0 a 300 segundos, por payload e perfil. O último não
  apresenta campo nem gera espera, mas conserva o valor anteriormente definido.
- O worker copia conjuntamente ordem e intervalos sob os mutexes existentes.
  Alterações posteriores só afetam a próxima execução. A espera utiliza o
  relógio monotónico, começa depois do resultado do envio e pode ser cancelada.
  Payloads inexistentes são ignorados sem espera, pois não houve envio.
- Mantém-se a política de continuar após falhas de transporte, na mesma ordem.
  Um único worker e o lock existente serializam os envios. O atraso inicial
  permanece independente, em `settings.json`.
- BootSplash permanece aberto no fim, erro ou cancelamento. Só Fechar o encerra.
  Cancelar impede os próximos envios e não fecha o ecrã. Ver detalhes expande
  os diagnósticos sem fechar o ecrã. Fechar não termina o servidor nem os payloads.
- Rótulos registados no I18N existente, dez idiomas, atualização com a preferência
  persistente e RTL em árabe. Nomes próprios não são traduzidos. Diagnósticos
  técnicos recebidos do carregador são preservados no idioma original, dentro
  dos detalhes; estados e mensagens da interface são traduzidos.
- O journal aceita a nova fase `interval`, preservando a proteção contra
  repetição depois de uma admissão de envio e substituição do processo.

## Persistência e compatibilidade

`/data/elf-launcher/auto.list` continua a conter exclusivamente nomes, um por
linha, na ordem de envio. Ler a configuração não altera o ficheiro.

`/data/elf-launcher/profiles.json` preserva `active`, `order` e `list`, acrescentando
opcionalmente `intervals`, indexado por perfil e nome de payload:

```json
{
  "active": "Principal",
  "order": ["Principal"],
  "list": {
    "Principal": ["kstuff-lite.elf", "shadowmountplus.elf", "ProsperoMgr.elf"]
  },
  "intervals": {
    "Principal": {"kstuff-lite.elf": 5, "shadowmountplus.elf": 3}
  }
}
```

Valores ausentes equivalem a zero, sem migração destrutiva. Renomear ou eliminar
um perfil trata também os seus intervalos. O backup existente inclui o objeto
completo de perfis, incluindo este campo, e a restauração usa a gravação conjunta.
O backend rejeita valores negativos, frações, strings e valores acima de 300.
Intervalos persistidos inválidos bloqueiam novos envios automáticos.

Mantêm-se `If-Match`, `X-Auto-Revision`, `X-Auto-List`, gravações atómicas de cada
ficheiro e rollback se a substituição de `auto.list` falhar. A transação continua
a usar a arquitetura anterior: dois ficheiros substituídos individualmente,
sem garantir uma transação única perante perda de alimentação entre renames.
O localStorage só guarda a supressão de reapresentação do ecrã por sessão;
não é a fonte da ordem, dos intervalos ou do idioma.

## Ficheiros

Código de produção alterado:

- `index.html` e `host/hbinstall/webapp/index.html`: layout, controlos, foco,
  perfis, BootSplash, traduções e CSS.
- `host/hbinstall/main.c`: validação dos intervalos e snapshot coerente da fila.
- `host/hbinstall/auto_boot.inc`: espera entre envios e cancelamento da nova fase.
- `host/hbinstall/auto_journal.inc`: reconhecimento da fase no journal.

Validação e documentação:

- `tests/auto-boot.test.c`, `tests/config-backend.cases.c`,
  `tests/boot-splash.test.cjs` e novo `tests/autopayload-ui.test.cjs`.
- `tools/auto-splash-demo.cjs`, `tests/artifacts/autopayload-demo.html` e
  `tests/artifacts/config-backend.generated.c` atualizados.
- `tests/artifacts/autopayload-list.png` e `autopayload-splash.png`: capturas
  locais em Chrome, inspecionadas visualmente.
- `docs/autopayload-boot.md` e este relatório.
- Novo binário `host/hbinstall/elf-launcher-v2.0.5.elf`.

## Validação local

38 testes JavaScript aprovados: persistência/revisões/conflitos, abertura sem
gravação, seleção e perfis, estados e cancelamento do BootSplash, demonstração
offline e integração em Chrome headless. A integração verifica larguras
1920/1280/768/390, filas de 2/3/4/12 itens, nomes longos, setas, foco,
drag-and-drop, intervalos por perfil, leitura posterior, dados do backup e
gravação pelo caminho da restauração, dez idiomas, RTL e Fechar.

Dois harnesses C aprovados com código de produção:

- Configuração: ordem, valores por perfil, reordenação, mudanças de perfil,
  padrão zero, oito formatos inválidos, conflitos e rollback.
- Worker: filas 0/1/3/12/64, 32 claims concorrentes, journal real em ficheiros
  locais, falhas, cancelamento inicial e nos dois intervalos, edição durante a
  execução, ausência de espera final, reinício simulado e proteção contra replay.
  Esperas de 1 + 1 segundos e atraso inicial de 10 segundos foram medidos com
  relógio real; os envios e a deteção de tmpfs foram simulados.

Comandos principais, a partir da raiz:

```powershell
node tools/auto-splash-demo.cjs
node --test tests/config-persistence.test.cjs tests/boot-splash.test.cjs tests/offline-demo.test.cjs tests/autopayload-ui.test.cjs
node tests/build-config-harness.cjs
wsl bash -lc 'cc -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -pthread tests/artifacts/config-backend.generated.c -o /tmp/elf-config-test && /tmp/elf-config-test'
wsl bash -lc 'cc -Wall -Wextra -Werror -Wno-unused-parameter -pthread tests/auto-boot.test.c -o /tmp/elf-auto-test && /tmp/elf-auto-test'
node tests/check-boot-build.cjs host/hbinstall/elf-launcher-v2.0.5.elf
git diff --check
```

O teste de navegador requer o módulo `playwright` resolvível pelo Node e Chrome
instalado. Nesta máquina foi utilizado `NODE_PATH` com o diretório de módulos do
runtime local do Codex. Todos os pedidos do navegador são intercetados por
fixtures; o teste não envia ELF nem contacta uma consola.

## Compilação

Concluída no WSL, com SDK local e `-Wall -Werror -O2`:

```sh
cd host/hbinstall
make PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk ELF=elf-launcher-v2.0.5.elf
```

As fontes elfldr estavam ausentes de `/tmp/elfldr-src` e foram obtidas do
repositório `ps5-payload-dev/elfldr`, commit
`2a8da002e5292481ea964daa7342bbf4855f4c33`.

Artefacto: `host/hbinstall/elf-launcher-v2.0.5.elf`, 1 483 520 bytes.
SHA-256: `4d7cb2432f689c1b9590249b7783c28b6c65a15ccf3689c4c2efc356b8b8fb36`.
O verificador confirmou o cabeçalho ELF e o HTML incorporado byte a byte.
Os ELF anteriores foram preservados.

## Verificações que exigem consola real

Não foi comprovado funcionamento efetivo no firmware 13.60. Exigem teste no PS5:
navegação e entrada numérica com DualSense; WebKit e apresentação dos glifos nos
dez idiomas; tempos e resultados do elfldr com payloads reais; durabilidade
após reiniciar a consola; início via WK Autoloader; substituição do launcher
sem reiniciar; servidor HTTP, gestão de descarregados, carregamento manual e
backup/restauração de ficheiros reais. A confirmação do transporte não comprova
a inicialização interna de um payload; o backend continua a declarar
`execution_confirmed: false`.
