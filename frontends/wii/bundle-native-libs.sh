#!/usr/bin/env bash
# O `cc` marca o sqlite como `static=` e o rustc já o põe no .a. Bibliotecas
# ligadas sem esse prefixo ficam de fora. Este passo copia os objetos delas
# para o arquivo do core, no mesmo critério do Switch.
set -euo pipefail

ar_bin=ar
if [[ -x /opt/devkitpro/devkitPPC/bin/powerpc-eabi-gcc-ar ]]; then
	ar_bin=/opt/devkitpro/devkitPPC/bin/powerpc-eabi-gcc-ar
fi

natives=()
while IFS= read -r -d '' lib; do
	natives+=("${lib}")
done < <(find target/powerpc-unknown-eabi/release -type f -path '*/out/lib/*.a' -print0 | sort -z)

if [[ ${#natives[@]} -eq 0 ]]; then
	exit 0
fi

arts=(target/powerpc-unknown-eabi/release/libzeebx_libretro.a)

if [[ ! -f "${arts[0]}" ]]; then
	echo "bundle-native-libs: não achei o arquivo do core" >&2
	exit 1
fi

for art in "${arts[@]}"; do
	tmp="$(mktemp -d)"
	i=0
	added=0
	for lib in "${natives[@]}"; do
		sub="${tmp}/l${i}"
		mkdir -p "${sub}"
		"${ar_bin}" x --output "${sub}" "${lib}"
		renamed=()
		for obj in "${sub}"/*; do
			[[ -f "${obj}" ]] || continue
			dest="${sub}/n${i}_$(basename "${obj}")"
			mv "${obj}" "${dest}"
			renamed+=("${dest}")
		done
		if [[ ${#renamed[@]} -gt 0 ]]; then
			"${ar_bin}" r "${art}" "${renamed[@]}"
			added=$((added + ${#renamed[@]}))
		fi
		i=$((i + 1))
	done
	"${ar_bin}" s "${art}" >/dev/null
	rm -rf "${tmp}"
	echo "bundled ${added} objects from ${#natives[@]} native libs into ${art}"
done
