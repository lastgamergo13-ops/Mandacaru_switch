# Testes

Testes que rodam no PC (e no GitHub Actions, workflow `Host Tests`), sem Android:

    ./tests/run_host_tests.sh

Precisa de `g++` 13+, `python3-cryptography` e `libzstd1` (ou `libzstd-dev`).

- **NCZ**: `tests/ncz/gen_ncz.py` gera um NCA sintético e o `.ncz` por blocos (zstd + AES-CTR); `test_ncz.cpp` lê o `NczBacking` com milhares de leituras aleatórias e compara com o NCA original. Também confere que um NCZ sólido é recusado.
- **Opus multi-stream**: decodifica 5.1 (4 streams, 2 acoplados) com o mesmo fluxo do serviço `hwopus` e compara com o decodificador de referência do libopus.

Os testes não usam jogos nem chaves. Os arquivos em `tests/shims` só existem para compilar fontes do emulador no x86.
