# Lista de compatibilidade (Mandacaru_switch)

Registro dos jogos testados. Atualize a cada rodada de testes. "Não testado" significa que ninguém abriu o jogo com a versão indicada.

| Jogo | Title ID | Aparelho / GPU | Resultado | Observações |
|---|---|---|---|---|
| Pokémon: Let's Go, Eevee! | 0100187003A36000 | Adreno 650 (Turnip) | Jogável | Salvar congelou só com log em DEBUG; um teste de 45 min terminou em crash sem mensagem |
| Pokémon: Let's Go, Eevee! | 0100187003A36000 | Moto G54 (PowerVR BXM-8-256) | Fecha antes de iniciar | Log para durante a carga do cache de pipelines (checklist H18) |
| Kingdom Come: Deliverance | 0100650018FE0000 | S20 FE | Chega ao jogo, 3 FPS | Texturas e menus errados (reinterpretação de formato ASTC) |
| Kingdom Come: Deliverance | 0100650018FE0000 | Moto G54 (PowerVR) | Fecha antes de iniciar | Mesmo ponto do Pokémon (H18) |
| Dark Souls: Remastered | 01004AB00A260000 | Moto G54 (PowerVR) | Fecha logo após iniciar | Patch de iluminação existente é da v1.0.3; o jogo testado é 1.0.0 |
| Well Dweller | 0100217023F6C000 | Moto G54 (PowerVR) | Fecha logo após iniciar | Sem erro no log (H18) |
| Pokémon Legends: Arceus | 01001F5010DFA000 | S20 FE | Roda a ~27 FPS, depois SIGBUS/congela | Checklist H15/F12 |
| Dark Souls: Remastered | 01004AB00A260000 | S20 FE (Adreno 650, driver do sistema) | Entra no jogo (~20 FPS) | Texturas piscando/borradas, personagem andando sempre para a direita, outros controles não reconhecidos (versão 355252ba) |
| Pokémon: Let's Go, Eevee! | 0100187003A36000 | S20 FE (Adreno 650) | Roda ~45 s e cai | `Vulkan device lost` em um submit grande (38 render passes); causa ainda desconhecida (versão 355252ba) |
| Kingdom Come: Deliverance | 0100650018FE0000 | S20 FE (Adreno 650) | Tela preta com música, 1 a 2 FPS | Falha ao criar pipeline com geometry shader passthrough (Adreno); estágio passou a ser omitido (checklist H5) |
| Farming Simulator 26 | — | S20 FE (Adreno 650) | Tela preta | Falta o serviço `ssl:s` (registrado depois); log termina após a inicialização do Input System |
| Well Dweller | 0100217023F6C000 | S20 FE (Adreno 650) | Tela preta | `rtld.nso` entra em laço infinito após consultar a memória do `main.nso`; investigação em andamento |
| Hogwarts Legacy | — | — | Não testado após as correções | hwopus 0x8 e instrução SAM corrigidos |

## Como registrar um teste
- Versão do app (commit), aparelho, GPU e driver (aparece no início do `emulation.log`).
- Nível de log usado (Info para jogar, Debug só para reproduzir bugs curtos).
- O que aconteceu e em quanto tempo.

## Formatos aceitos na biblioteca
NRO, NSO, NCA, NSP, XCI, e NSZ/XCZ comprimidos **por blocos** (`nsz -B`). NSZ/XCZ "sólido" (padrão do `nsz`) não é lido: o app mostra a mensagem de arquivo sólido não suportado. Para esses arquivos, descomprima com `nsz -D` para NSP/XCI ou recomprima com `nsz -C -B`.

## Limitações conhecidas
- **Renderização condicional (Maxwell3D `renderEnable`):** quando o jogo usa o modo condicional ou "se igual/diferente" com um resultado de query que ainda está na GPU, o emulador não espera o resultado: desenha sempre (como se a query tivesse passado). Só quando o valor já está na memória do jogo ele é lido de verdade. Emular de forma exata exigiria indirect draws ou uma sincronização com a CPU a cada draw condicional, o que custaria muito desempenho; o efeito prático é desenhar objetos que o jogo queria esconder (oclusão). Checklist E5.
- **Topologia `LineLoop`:** é desenhada como `LineStrip`, sem o segmento que fecha o laço (o Vulkan não tem line loop). `QuadStrip` é desenhada como `TriangleStrip` e `Polygon` como `TriangleFan`. Checklist E3.
- **Formatos de vertex de 3 componentes (8 e 16 bits):** em GPUs que não os leem de vertex buffers, são trocados pelo formato de 4 componentes equivalente (o componente extra é ignorado pelo shader). Checklist E3.
