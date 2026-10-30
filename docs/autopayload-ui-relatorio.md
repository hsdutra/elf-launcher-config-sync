# AutoPayload — revisão de design e usabilidade

## Resultado

A página utiliza os componentes, cores, bordas, tipografia e slider existentes
na Home. A lista mantém uma coluna, com largura máxima de 72 rem para evitar
cartões demasiado largos em monitores grandes.

A ordem visual passou a ser: perfis, atraso inicial, ordem de execução e
disponíveis. Cada cartão selecionado separa prioridade/seleção/nome/versão/setas
da área do intervalo. O último apresenta Fim da sequência. Foram removidas as
etiquetas redundantes de seleção automática; a seleção continua explícita no
botão de marcação, com semântica de checkbox para acessibilidade.

Os intervalos utilizam o mesmo trilho, thumb, preenchimento, foco e indicador
de segundos da Home. Mantêm os limites anteriores de 0–300 segundos, em passos
de 1 segundo. O atraso inicial mantém 0–10 segundos, em passos de 0,5 segundo.

## Sincronização e gravação

- `startDelayHtml()` e `bindStartDelay()` são partilhados pela Home e AutoPayload.
  Ambas leem `DEVICE_SETTINGS.autoDelay` e gravam `/settings`, com o mesmo
  `If-Match`. Não foi criado outro estado persistente nem utilizado localStorage.
- A consulta periódica existente sincroniza outros navegadores. Uma atualização
  apenas dos valores ajusta os sliders sem reconstruir a lista AutoPayload.
- `input` atualiza apenas o controlo e o indicador local. `change` agrupa eventos
  de teclado/comando durante 220 ms. Sair do controlo confirma imediatamente a
  alteração pendente, antes de uma troca de perfil. Arrastar com o rato não
  grava cada posição intermédia.
- A gravação dos intervalos utiliza a fila de perfis e as revisões existentes,
  com atualização dos valores confirmados sem substituir os cartões.
  Erros são apresentados pelo mecanismo existente, restaurando o valor do PS5
  e o foco. O controlo distingue alterações pendentes de pedidos já enviados,
  para não repetir uma gravação falhada quando perde o foco.
- A troca de perfil durante uma alteração utiliza o perfil capturado pelo
  controlo. O formato de `profiles.json`, `settings.json` e `auto.list` permanece
  igual ao da implementação anterior.
- A navegação vertical segue seleção → intervalo → próximo cartão. Esquerda e
  direita no D-pad ajustam o slider focado. Os botões de ordenação continuam
  acessíveis e o foco acompanha o payload depois da gravação.
- Os controlos de formulário mantêm os gestos nativos de rato dentro dos cartões
  arrastáveis; arrastar o slider não inicia uma reordenação.

Não foram alterados o worker, o backend, o BootSplash, a proteção contra execução
duplicada nem a integração WK Autoloader nesta revisão visual.

## Ficheiros alterados nesta revisão

- `index.html` e `host/hbinstall/webapp/index.html`: componente partilhado,
  composição dos cartões, sliders, gravação sem reconstrução, navegação e textos
  nos dez idiomas.
- `tests/autopayload-ui.test.cjs`: testes de navegador ampliados.
- `tests/config-persistence.test.cjs`: fixture da seleção atualizada para o
  botão de checkbox explícito.
- `tests/artifacts/autopayload-demo.html`: regenerado com o CSS atual.
- `tests/artifacts/autopayload-list.png`, `autopayload-list-mobile.png` e
  `autopayload-splash.png`: capturas da validação local.
- Este relatório.
- Novo artefacto: `host/hbinstall/elf-launcher-v2.0.5-autopayload-ui.elf`.

Os ficheiros C e outros ficheiros já modificados pela tarefa anterior foram
preservados. O ELF anterior `elf-launcher-v2.0.5.elf` também foi preservado.

## Testes locais

38 testes JavaScript aprovados, incluindo testes em Chrome headless com pedidos
intercetados por fixtures. A integração de navegador verificou:

- Uma coluna nas larguras 1920, 1280, 768 e 390, com 2/3/4/12 selecionados e
  nomes longos; ausência de overflow horizontal e de campos numéricos.
- Componente de Start delay idêntico ao da Home, sincronização nos dois sentidos
  em duas sessões e persistência depois de recarregar o navegador.
- Ajuste do atraso inicial em passos de 0,5 segundo pelo teclado.
- Arrastamento real do slider com o rato: uma gravação após soltar, nenhuma
  durante o arrastamento.
- Alterações dos intervalos por teclado e D-pad simulado, agregação de eventos,
  conservação do nó focado e recuperação após falha simulada de gravação.
- Ordenação por setas e drag-and-drop, intervalos associados ao nome, mudança
  de perfil e passagem da configuração pelo caminho de backup/restauração.
- Textos dos dez idiomas, RTL em árabe e regressão do BootSplash.

Os dois harnesses C da implementação anterior também passaram: configuração,
revisões, rollback, ordem, intervalos, cancelamento, journal e prevenção de replay.
Os envios ELF e a consola foram simulados. As esperas do worker usam relógio
real nos testes locais.

As capturas desktop e mobile foram inspecionadas visualmente:
[desktop](../tests/artifacts/autopayload-list.png),
[mobile](../tests/artifacts/autopayload-list-mobile.png).

Comandos, a partir da raiz:

```powershell
node tools/auto-splash-demo.cjs
node --test tests/config-persistence.test.cjs tests/boot-splash.test.cjs tests/offline-demo.test.cjs tests/autopayload-ui.test.cjs
node tests/check-boot-build.cjs host/hbinstall/elf-launcher-v2.0.5-autopayload-ui.elf
git diff --check
```

O teste de navegador requer `playwright` resolvível pelo Node e Chrome instalado.
Foi usado o módulo já disponível no runtime local, via `NODE_PATH`.

## Compilação e limites da validação

Compilação realizada no WSL com o SDK local, `-Wall -Werror -O2`:

```sh
cd host/hbinstall
make PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk ELF=elf-launcher-v2.0.5-autopayload-ui.elf
```

As fontes temporárias elfldr foram repostas a partir de `ps5-payload-dev/elfldr`,
no mesmo commit da compilação anterior:
`2a8da002e5292481ea964daa7342bbf4855f4c33`.
O verificador confirmou o cabeçalho ELF e o HTML incorporado byte a byte.

Não houve ligação, instalação, execução nem reinicialização do PS5. Nenhum
payload, perfil ou configuração persistida do utilizador foi alterado pelos
testes. O DualSense foi simulado pela função de navegação de produção; a
navegação, os sliders e os glifos ainda exigem validação no WebKit e DualSense
reais, assim como o carregamento desta compilação na consola.
