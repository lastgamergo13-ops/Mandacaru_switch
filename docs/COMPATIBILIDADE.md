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
| Hogwarts Legacy | — | — | Não testado após as correções | hwopus 0x8 e instrução SAM corrigidos |

## Como registrar um teste
- Versão do app (commit), aparelho, GPU e driver (aparece no início do `emulation.log`).
- Nível de log usado (Info para jogar, Debug só para reproduzir bugs curtos).
- O que aconteceu e em quanto tempo.
