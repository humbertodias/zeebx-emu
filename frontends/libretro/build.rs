fn main() {
    println!("cargo:rerun-if-changed=src/r36s_compat.c");
    println!("cargo::rustc-check-cfg=cfg(zeebx_wii)");
    let target_os = std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let target = std::env::var("TARGET").unwrap_or_default();
    // O alvo do Wii também diz `os = linux`, para a std ter arquivo, mas o gcc do
    // devkitPPC não é o glibc que este arquivo espera. O objeto vazio não serve lá.
    if target_os == "linux" && !target.starts_with("powerpc") {
        cc::Build::new()
            .file("src/r36s_compat.c")
            .compile("r36s_compat");
    }
}
