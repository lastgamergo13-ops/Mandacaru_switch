# Origem dos arquivos desta pasta

Os arquivos em `common/` (`bit_field.h`, `stream.*`, `scratch_buffer.h`, `alignment.h`) e em `video_core/host1x/` (`codec_types.h`, `nvdec_common.h`, `vic_types.h`, `codecs/*`) vêm do emulador **Eden** (https://git.eden-emu.dev/eden-emu/eden), licenciados GPL-3.0-or-later (os avisos de copyright ficam no topo de cada arquivo). O Mandacaru_switch também é GPL v3.

Mudanças feitas aqui: só os `#include` (apontam para esta pasta) e a camada de compatibilidade:
- `common/common_types.h`, `common_funcs.h`, `assert.h`, `logging.h`, `settings.h`, `swap.h`: versões mínimas feitas para este projeto.
- `video_core/host1x/host1x.*` e `ffmpeg.*`: interface própria que os codecs usam (memória do jogo pelo SMMU, fila de quadros e o decodificador do FFmpeg).
