# Teste dos portes no POCO — 08/11/2026

APK de debug instalado por atualização, preservando os dados. Aparelho USB `98d66058`, POCO F7 Ultra (`24122RKC7G`), Adreno 830 com Turnip, tela de 120 Hz e MemTotal de 15.113 MiB. Jogo TOTK 1.4.3, código nativo, resolução 1x e FSR1. Cada execução usa configurações privadas separadas e uma cópia do save do jogador, na Grande Ilha do Céu. O INI original foi comparado com o backup e permaneceu idêntico.

## Resultados

Faixas observadas parado no mesmo ponto, após o carregamento. São verificações curtas de funcionamento, sem clima/horário fixados; não constituem benchmark de ganho percentual.

| Execução | Resultado |
| --- | --- |
| Normal, memória placed + BDA, limite 60 | Cerca de 45–46 FPS no cenário. |
| Arquivo OPAQUE_FD + BDA, limite 60 | Cerca de 45–46 FPS. Heap de 2144 MiB exportado e mapeado no endereço do jogo; luz, sombras e inventário visíveis. |
| Arquivo sem placed maps e sem BDA | Cerca de 38–40 FPS, com janelas de SSBO. Cenário com luz e sombras, sem encerramento observado. Simula esse caminho de acesso, não uma GPU Mali real. |
| DMA-BUF explicitamente escolhido | Cerca de 45–46 FPS. Log confirma DMA-BUF no heap, com acesso por ponteiro. |
| Pouca RAM forçada + limite 20 | 20 FPS após carregar. Logs confirmam buffers menores, texturas a partir do segundo mip e descarte de pipelines antigos. Orçamento de pipelines reduzido a 200 somente para exercitar o descarte: houve remoções reais, sem encerramento observado. |
| Limite intermediário | Detectou 120 Hz e escolheu 40 FPS; estabilizou em 39,9–40,0 após carregar. |
| Segundo plano e retorno | Processo preservado; inventário voltou a renderizar. AAudio pausou e retornou ao estado de reprodução sem erro. Não foi provocada desconexão do dispositivo de áudio. |

A sonda de memória confirmou exportação, mapeamento e visibilidade nos dois sentidos CPU/GPU para OPAQUE_FD e DMA-BUF nos tipos de memória expostos pelo Turnip. Isso valida este driver; não garante coerência em outros drivers.

O modo normal já usa memória compartilhada por placed maps neste Adreno. Não houve ganho de FPS claro ao trocar esse caminho pelo arquivo. A utilidade do arquivo aqui foi validar a alternativa para drivers sem placed maps/importação.

## Regra de pouca RAM

A detecção automática usa MemTotal conhecido até 4 GiB. Aparelhos de 6 GB ou mais mantêm os buffers, texturas e cache normais; somente os de pouca RAM recebem o orçamento de cache proporcional. Overrides por variável de ambiente continuam disponíveis para diagnóstico.

`tools/test_low_ram.py` extrai as funções reais de detecção e orçamento de cache. Oito casos passaram no WSL: 4 GiB incluído, imediatamente acima excluído, 6/8 GB com cache normal de 768 MiB, memória desconhecida e overrides. O APK final foi recompilado e reinstalado após restringir também esse cache.

As quatro regressões ARM64 executadas no próprio POCO passaram: afinidade de CPU, vida útil NVN, writeback e filtragem de cópias vazias.

## Limites e evidências

Ainda precisam de aparelho correspondente: estabilidade em 4 GB reais, GPUs Mali/Xclipse, 45 FPS em 90 Hz, fallback em 60 Hz e rotação física do giroscópio. A simulação de pouca RAM no POCO verifica os caminhos de código, não a pressão de memória de um aparelho de 4 GB. Não houve teste prolongado de aquecimento ou de todas as regiões do jogo.

Logs, capturas, INIs e relatórios de memória estão em `build/botw-port-20261108/`, por execução (`baseline`, `file-bda`, `file-windows`, `file-dma`, `low20`, `mid45`). A primeira tentativa de INI em pasta criada pelo shell foi recusada pelo Android e usou padrões de 30 FPS; foi descartada da comparação e guardada como `baseline-30-default`. As execuções válidas registram o carregamento do INI privado da aplicação.
