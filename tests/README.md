# Testes

Testes que rodam no PC (e no GitHub Actions, workflow `Host Tests`), sem Android:

    bash ./tests/run_host_tests.sh

Precisa de `g++` 13+, `python3-cryptography` e `libzstd1` (ou `libzstd-dev`).

- **NCZ**: `tests/ncz/gen_ncz.py` gera um NCA sintético e o `.ncz` por blocos (zstd + AES-CTR); `test_ncz.cpp` lê o `NczBacking` com milhares de leituras aleatórias e compara com o NCA original. Também confere que um NCZ sólido é recusado.
- **BKTR**: `tests/bktr/gen_bktr.py` gera uma seção de atualização sintética (tabela de relocação em vários conjuntos, subseções com gerações diferentes, AES-CTR) e `test_bktr.cpp` compara a leitura do `BktrBacking` com o resultado esperado.
- **Opus multi-stream**: decodifica 5.1 (4 streams, 2 acoplados) com o mesmo fluxo do serviço `hwopus` e compara com o decodificador de referência do libopus.

Os testes não usam jogos nem chaves. Os arquivos em `tests/shims` só existem para compilar fontes do emulador no x86.
- **BCn**: `tests/bcn_test.cpp` decodifica BC1 a BC7 com várias texturas (tamanhos com bordas parciais) usando o decodificador paralelo e o serial (`bc_decoder.cpp`, incluído direto no teste) e exige resultado idêntico byte a byte. Roda com 4 threads forçadas (`-DBCN_TEST_THREADS=4`), mesmo em máquina de um núcleo.
