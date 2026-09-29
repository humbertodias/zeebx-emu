#!/usr/bin/env bash
# Monta zeebx_libretro.klf para o Dreamcast.
#
# O KallistiOS carrega a biblioteca em tempo de execução (`library_open` / `elf_load`).
# O arquivo é um ELF relocável, o mesmo que `loadable/Makefile.prefab` produz: `-r`,
# sem startup e sem `libkallisti`. Os símbolos que o núcleo ainda não tem ficam em
# aberto e o carregador preenche com a tabela de exports do programa que abriu a
# biblioteca. `elf_load` recusa o arquivo se faltarem `lib_get_name`, `lib_get_version`,
# `lib_open` e `lib_close`; os `retro_*` ficam na tabela de símbolos do mesmo ELF.
#
# O host é SH-4, e o rustc de fábrica não emite esse código. A cadeia é a do
# dreamcast.rs (KallistiOS + rustc_codegen_gcc): o alvo se apresenta como MIPS para
# o rustc, o GCC gera SH-4, e o cabeçalho de cada objeto ainda diz MIPS até este
# script reescrever e_machine. Sem isso o `sh-elf-ld` recusa o objeto.
#
# A cadeia já no ambiente (source de $KOS_RUST_BASE/misc/environ.sh):
#   ./frontends/dreamcast/compilar.sh
#
# Dentro dessa cadeia, com o ambiente já exportado:
#   ./frontends/dreamcast/compilar.sh --local [diretorio-de-saida]
#
# Uma imagem que já traz a cadeia:
#   ZEEBX_DREAMCAST_IMAGE=nome ./frontends/dreamcast/compilar.sh
set -euo pipefail

AQUI="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RAIZ="$(cd "$AQUI/../.." && pwd)"
KOS_RUST_BASE="${KOS_RUST_BASE:-/opt/toolchains/dc/rust}"

# O rustc_codegen_gcc não faz LTO, e o perfil release do workspace pede thin LTO.
# Debug info não entra no ELF que o console carrega: 16 MB de RAM principal.
export CARGO_PROFILE_RELEASE_LTO=off
export CARGO_PROFILE_RELEASE_CODEGEN_UNITS=16

preparar_ambiente() {
	if [[ ! -f "$KOS_RUST_BASE/misc/environ.sh" ]]; then
		echo "não achei o Rust para Dreamcast em $KOS_RUST_BASE." >&2
		echo "A cadeia é a do https://dreamcast.rs/setup.html — defina KOS_RUST_BASE se ela não está em /opt/toolchains/dc/rust." >&2
		exit 1
	fi
	# shellcheck disable=SC1091
	source "$KOS_RUST_BASE/misc/environ.sh"
	if ! command -v kos-cargo >/dev/null 2>&1; then
		echo "o environ.sh não deixou o kos-cargo no PATH." >&2
		exit 1
	fi
	if ! command -v kos-cc >/dev/null 2>&1 || ! command -v sh-elf-ar >/dev/null 2>&1; then
		echo "faltou kos-cc ou sh-elf-ar. O environ.sh do KallistiOS não foi aplicado por completo." >&2
		exit 1
	fi
}

# O getrandom 0.4 recusa um target_os que não conhece. O sevenz só pede bytes
# aleatórios ao gravar um .7z; o emulador lê. O backend "unsupported" compila e
# devolve erro se alguém tentar comprimir.
export KOS_RCG_RUSTFLAGS="${KOS_RCG_RUSTFLAGS:-} --cfg getrandom_backend=\"unsupported\" -C debuginfo=0"

compilar_local() {
	local saida="${1:-$AQUI/saida}"
	preparar_ambiente
	mkdir -p "$saida"

	# O cc do sqlite precisa do mesmo ABI do Rust (-m4-single, não -m4-single-only:
	# f64 do Rust tem 64 bits). kos-cc já carrega os -m do KallistiOS.
	export CC_sh_elf="kos-cc"
	export AR_sh_elf="sh-elf-ar"
	export RANLIB_sh_elf="sh-elf-ranlib"
	# Sem mmap, sem dlopen, sem WAL: o newlib do KallistiOS não tem os três.
	export LIBSQLITE3_FLAGS="-DSQLITE_OMIT_WAL -DSQLITE_MAX_MMAP_SIZE=0 -DSQLITE_OMIT_LOAD_EXTENSION"

	local config_libc=()
	if [[ -d "$KOS_RUST_BASE/libc" ]]; then
		# A libc do crates.io não conhece o KallistiOS. O fork vem com a cadeia.
		config_libc=(--config "patch.crates-io.libc.path=\"$KOS_RUST_BASE/libc\"")
	else
		echo "aviso: $KOS_RUST_BASE/libc não existe; a libc do crates.io pode não casar com o newlib." >&2
	fi

	cd "$RAIZ"
	# O patch da libc muda a resolução. O lock do repositório não pode ficar com o path da máquina.
	local trava
	trava=$(mktemp)
	cp "$RAIZ/Cargo.lock" "$trava"
	local status=0
	# O .a é só o maço de objetos. O arquivo que o console abre é o .klf, ligado abaixo.
	kos-cargo rustc --release -p zeebx-libretro --crate-type staticlib "${config_libc[@]}" || status=$?
	cp "$trava" "$RAIZ/Cargo.lock"
	rm -f "$trava"
	[[ $status -eq 0 ]]

	local origem="$RAIZ/target/sh-elf/release/libzeebx_libretro.a"
	if [[ ! -f "$origem" ]]; then
		echo "o kos-cargo não deixou $origem." >&2
		exit 1
	fi
	local objetos
	objetos=$(reescrever_cabecalhos "$origem")
	ligar_klf "$objetos" "$saida/zeebx_libretro.klf"
	rm -rf "$(dirname "$objetos")"
	conferir_arquivo "$saida/zeebx_libretro.klf"
	echo "built: $saida/zeebx_libretro.klf"
}

# Junta os objetos já com e_machine SH num ELF relocável, como o Makefile.prefab.
ligar_klf() {
	local objetos="$1"
	local destino="$2"
	local script="${KOS_BASE:?}/loadable/shlelf_dc.xr"
	if [[ ! -f "$script" ]]; then
		echo "não achei $script. Sem ele o ELF relocável sai com endereço que o elf_load não aceita." >&2
		exit 1
	fi
	kos-cc ${KOS_CFLAGS:-} -fno-lto \
		-Wl,-d -Wl,-r -Wl,-S -Wl,-x \
		-nostartfiles -nodefaultlibs \
		-o "$destino" \
		-Wl,-T,"$script" \
		-Wl,--whole-archive "$objetos" -Wl,--no-whole-archive \
		-lgcc
}

# e_machine fica no offset 0x12. 0x08 é EM_MIPS; 0x2A é EM_SH. O wrapper do
# dreamcast.rs só troca esse byte, e o sh-elf-ld aceita o objeto assim.
# Devolve o caminho de um .a só com objetos SH. e_machine fica no offset 0x12:
# 0x08 é EM_MIPS, 0x2A é EM_SH. O wrapper do dreamcast.rs só troca esse byte.
reescrever_cabecalhos() {
	local arq="$1"
	local tmp destino
	tmp=$(mktemp -d)
	destino="$tmp/objetos.a"
	(
		cd "$tmp"
		sh-elf-ar x "$arq"
		python3 - << 'PY'
import pathlib
elfs = []
for caminho in pathlib.Path(".").iterdir():
    if not caminho.is_file():
        continue
    dados = caminho.read_bytes()
    if len(dados) < 0x14 or dados[:4] != b"\x7fELF":
        continue
    if dados[0x12] == 0x08:
        bruto = bytearray(dados)
        bruto[0x12] = 0x2A
        caminho.write_bytes(bruto)
    elfs.append(caminho.name)
pathlib.Path("elfs.txt").write_text("\n".join(elfs) + "\n")
PY
		# shellcheck disable=SC2046
		sh-elf-ar rcs "$destino" @elfs.txt
	)
	echo "$destino"
}

conferir_arquivo() {
	local arq="$1"
	local tipo
	tipo=$(sh-elf-objdump -f "$arq" | grep 'file format' || true)
	if ! grep -q 'elf32-shl' <<< "$tipo"; then
		echo "o .klf não é SuperH little-endian" >&2
		echo "$tipo" >&2
		exit 1
	fi
	if ! sh-elf-readelf -h "$arq" | grep -q 'Type:.*REL'; then
		echo "o .klf não é relocável. O elf_load avisa quando o arquivo foi ligado sem -r." >&2
		exit 1
	fi
	local faltando=""
	for simbolo in retro_api_version lib_get_name lib_get_version lib_open lib_close; do
		if ! sh-elf-nm -g "$arq" | grep -q " ${simbolo}\$"; then
			faltando="$faltando $simbolo"
		fi
	done
	if [[ -n "$faltando" ]]; then
		echo "o .klf não exporta:${faltando}" >&2
		exit 1
	fi
	echo "zeebx_libretro.klf é ELF SH-4 relocável e exporta a ABI"
}

if [[ "${1:-}" == "--local" ]]; then
	shift
	compilar_local "$@"
	exit 0
fi

if [[ -n "${ZEEBX_DREAMCAST_IMAGE:-}" ]]; then
	SAIDA="${1:-$AQUI/saida}"
	mkdir -p "$SAIDA"
	docker run --rm --platform linux/amd64 \
		-u "$(id -u):$(id -g)" \
		-e HOME=/tmp \
		-v "$RAIZ:/src" \
		-w /src \
		"$ZEEBX_DREAMCAST_IMAGE" \
		bash -lc "set -euo pipefail
cd /src
./frontends/dreamcast/compilar.sh --local /src/frontends/dreamcast/saida
"
	if [[ "$SAIDA" != "$AQUI/saida" ]]; then
		cp -f "$AQUI/saida/zeebx_libretro.klf" "$SAIDA/"
		echo "built: $SAIDA/zeebx_libretro.klf"
	fi
	exit 0
fi

compilar_local "$@"
