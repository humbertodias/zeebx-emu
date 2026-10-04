#!/usr/bin/env bash
# Monta libzeebx_libretro_wii.a para o RetroArch do Wii.
#
# O console é PowerPC 750 big-endian (EABI). O Dynarmic não emite esse código, então
# o núcleo aqui é o interpretador. O RetroArch do Wii não carrega .so: ele liga um
# .a estático. Na hora de montar o DOL, copie este arquivo para libretro_wii.a na
# raiz do RetroArch e rode `make -f Makefile.griffin platform=wii`.
#
# No Mac, a cadeia está na imagem do CI de Wii do RetroArch (o script instala o rustc lá dentro):
#   ./frontends/wii/compilar.sh
#
# Dentro dessa cadeia:
#   ./frontends/wii/compilar.sh --local [diretorio-de-saida]
#
# O job do CI chama o script sem --local. Declarar a imagem como `container:` do
# Actions faz o checkout rodar lá dentro, e o glibc dela é anterior ao 2.25.
set -euo pipefail

AQUI="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RAIZ="$(cd "$AQUI/../.." && pwd)"
IMAGEM="${ZEEBX_WII_IMAGE:-reallibretroretroarch/libretro-build-devkitpro:latest}"
PPC_BIN="/opt/devkitpro/devkitPPC/bin"

compilar_local() {
	local saida="${1:-$AQUI/saida}"
	export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
	export DEVKITPPC="${DEVKITPPC:-/opt/devkitpro/devkitPPC}"
	export PATH="${PPC_BIN}:${PATH}"

	if [[ ! -x "${PPC_BIN}/powerpc-eabi-gcc" ]]; then
		echo "não achei o powerpc-eabi-gcc. Rode dentro da imagem devkitPPC, ou sem --local para o script subir o Docker." >&2
		exit 1
	fi
	if [[ ! -d "$(rustc --print sysroot)/lib/rustlib/src/rust/library/std" ]]; then
		echo "precisa do componente rust-src: a std é recompilada para o EABI do Wii." >&2
		exit 1
	fi

	export CC_powerpc_unknown_eabi="${PPC_BIN}/powerpc-eabi-gcc"
	export CXX_powerpc_unknown_eabi="${PPC_BIN}/powerpc-eabi-g++"
	export AR_powerpc_unknown_eabi="${PPC_BIN}/powerpc-eabi-gcc-ar"
	export CARGO_TARGET_POWERPC_UNKNOWN_EABI_LINKER="${PPC_BIN}/powerpc-eabi-gcc"
	# panic=abort fica no alvo, não no host: o proc-macro continua com unwind.
	# zeebx_wii encolhe o heap e o cache de som. O Broadway não tem AltiVec.
	# O alvo declara atômico de 64 bits para o `AtomicU64` que o egui usa. O LLVM
	# não emite lqarx: vira chamada de `__atomic_*_8`. O libgcc daqui não tem
	# essas funções; o `compat.c` implementa com a interrupção desligada.
	export CARGO_TARGET_POWERPC_UNKNOWN_EABI_RUSTFLAGS="--cfg zeebx_wii -C panic=abort -C target-cpu=750"
	# -I aponta para o ioctl.h que o newlib não tem e o sqlite inclui mesmo assim.
	export CFLAGS_powerpc_unknown_eabi="-mcpu=750 -meabi -mhard-float -mno-altivec -mrvl -I$AQUI/compat"
	# O newlib dessa imagem tem pthread.h sem PTHREAD_MUTEX_INITIALIZER, e o
	# sqlite em THREADSAFE=1 não compila. O banco é chamado no fio da emulação.
	# WAL e mmap ficam desligados: o newlib não tem mmap de verdade.
	export LIBSQLITE3_FLAGS="-USQLITE_THREADSAFE -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_WAL -DSQLITE_MAX_MMAP_SIZE=0 -DSQLITE_OMIT_LOAD_EXTENSION"
	export RUSTC_BOOTSTRAP=1
	export RUST_TARGET_PATH="$AQUI"
	mkdir -p "$saida"
	# A sondagem do cargo chama o rustc sem `-Z json-target-spec`. Sem este
	# prefixo o alvo customizado é recusado antes de qualquer compilação.
	# Fica em /tmp, não no volume: alguns mounts recusam executar o que foi escrito neles.
	local rustc_wii="/tmp/zeebx-rustc-wii"
	cat >"$rustc_wii" <<'EOF'
#!/bin/sh
exec rustc -Z unstable-options "$@"
EOF
	chmod +x "$rustc_wii"
	export RUSTC="$rustc_wii"

	cd "$RAIZ"
	cargo rustc -Z build-std=std,panic_abort -Z json-target-spec \
		--release --locked \
		-p zeebx-libretro \
		--target powerpc-unknown-eabi \
		--crate-type staticlib
	bash "$AQUI/bundle-native-libs.sh"

	local ar_bin="${PPC_BIN}/powerpc-eabi-gcc-ar"
	local compat="${saida}/compat.o"
	# -fno-builtin: o gcc conhece __atomic_compare_exchange_8 com outra assinatura
	# (tem o parâmetro weak). O LLVM chama a versão sem ele. Sem isto o gcc trata
	# a nossa definição como se fosse o builtin e o tipo não fecha.
	"${PPC_BIN}/powerpc-eabi-gcc" -mcpu=750 -meabi -mhard-float -mno-altivec -mrvl \
		-fno-builtin -c "$AQUI/compat.c" -o "$compat"
	"${ar_bin}" r "$RAIZ/target/powerpc-unknown-eabi/release/libzeebx_libretro.a" "$compat"
	"${ar_bin}" s "$RAIZ/target/powerpc-unknown-eabi/release/libzeebx_libretro.a" >/dev/null
	rm -f "$compat"

	cp -f "$RAIZ/target/powerpc-unknown-eabi/release/libzeebx_libretro.a" \
		"$saida/libzeebx_libretro_wii.a"

	# Prova de que o RetroArch consegue ligar o arquivo: um main que chama a ABI,
	# com --whole-archive, contra o newlib e o libogc que o -mrvl puxa.
	local prova
	prova="$(mktemp -d)"
	cat >"$prova/main.c" <<'EOF'
unsigned retro_api_version(void);
int main(void) { return retro_api_version() == 1 ? 0 : 1; }
EOF
	# -logc traz o __app_start e o sbrk do console. Sem ele o -mrvl liga só o
	# newlib, e o _sbrk_r pede um __end__ que o script do Wii não define.
	"${PPC_BIN}/powerpc-eabi-gcc" -mcpu=750 -meabi -mhard-float -mno-altivec -mrvl -O2 \
		-L"${DEVKITPRO}/libogc/lib/wii" \
		-o "$prova/prova.elf" "$prova/main.c" \
		-Wl,--whole-archive "$saida/libzeebx_libretro_wii.a" -Wl,--no-whole-archive \
		-logc -lm
	"${PPC_BIN}/powerpc-eabi-objdump" -f "$prova/prova.elf" | grep -q elf32-powerpc
	rm -rf "$prova"

	echo "built: $saida/libzeebx_libretro_wii.a"
	echo "no RetroArch: copie para libretro_wii.a e rode make -f Makefile.griffin platform=wii"
}

if [[ "${1:-}" == "--local" ]]; then
	shift
	compilar_local "$@"
	exit 0
fi

SAIDA="${1:-$AQUI/saida}"
mkdir -p "$SAIDA"

# Esses dois diretórios, se o CI os definir, sobrevivem ao --rm. Sem eles o
# rustup fica em /root e some com o contêiner.
# A imagem entra com um usuário sem permissão de escrita em /var/lib/apt. O CI do
# RetroArch sobe a mesma imagem com --user root pelo mesmo motivo.
docker_args=(--rm --platform linux/amd64 --user root -v "$RAIZ:/src" -w /src)
if [[ -n "${ZEEBX_WII_CARGO_HOME:-}" ]]; then
	mkdir -p "$ZEEBX_WII_CARGO_HOME"
	docker_args+=(-v "$ZEEBX_WII_CARGO_HOME:/usr/local/cargo" -e CARGO_HOME=/usr/local/cargo)
fi
if [[ -n "${ZEEBX_WII_RUSTUP_HOME:-}" ]]; then
	mkdir -p "$ZEEBX_WII_RUSTUP_HOME"
	docker_args+=(-v "$ZEEBX_WII_RUSTUP_HOME:/usr/local/rustup" -e RUSTUP_HOME=/usr/local/rustup)
fi

docker run "${docker_args[@]}" \
	"$IMAGEM" \
	bash -lc "set -euo pipefail
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=/opt/devkitpro/devkitPPC
export PATH=\"\${CARGO_HOME:-\$HOME/.cargo}/bin:/opt/devkitpro/devkitPPC/bin:\$PATH\"
if ! command -v rustc >/dev/null 2>&1 || ! [[ -d \"\$(rustc --print sysroot)/lib/rustlib/src/rust/library/std\" ]]; then
  if ! command -v curl >/dev/null 2>&1 || ! command -v gcc >/dev/null 2>&1 || ! command -v pkg-config >/dev/null 2>&1; then
    # O Stretch saiu do deb.debian.org. O arquivo ainda serve os pacotes, mas o
    # Release e a chave de assinatura expiraram, e o apt recusa o índice sem isto.
    sed -i '/stretch/d' /etc/apt/sources.list /etc/apt/sources.list.d/*.list 2>/dev/null || true
    printf '%s\n' \
      'deb [trusted=yes] http://archive.debian.org/debian stretch main' \
      'deb [trusted=yes] http://archive.debian.org/debian-security stretch/updates main' \
      > /etc/apt/sources.list
    printf '%s\n' 'Acquire::Check-Valid-Until \"false\";' > /etc/apt/apt.conf.d/99archive
    apt-get update
    apt-get install -y --no-install-recommends ca-certificates curl build-essential pkg-config
  fi
  curl --proto '=https' --tlsv1.2 -fsSL https://sh.rustup.rs | sh -s -- -y --default-toolchain stable --profile minimal --component rust-src
fi
. \"\${CARGO_HOME:-\$HOME/.cargo}/env\"
cd /src
./frontends/wii/compilar.sh --local /src/frontends/wii/saida
"

if [[ "$SAIDA" != "$AQUI/saida" ]]; then
	cp -f "$AQUI/saida/libzeebx_libretro_wii.a" "$SAIDA/"
	echo "built: $SAIDA/libzeebx_libretro_wii.a"
fi
