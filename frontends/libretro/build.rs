fn main() {
    println!("cargo:rerun-if-changed=src/r36s_compat.c");
    println!("cargo::rustc-check-cfg=cfg(target_os, values(\"kallistios\"))");
    let target_os = std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    if target_os == "linux" {
        cc::Build::new()
            .file("src/r36s_compat.c")
            .compile("r36s_compat");
    }
}
