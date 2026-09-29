fn main() {
    // **O ícone do `.exe` não mora mais aqui.** Um recurso do Windows só chega ao executável
    // se for compilado no pacote que o produz, e desde a 0.3.0 quem produz o `zeebx` é o
    // `frontends/egui-standalone`.
    println!("cargo:rerun-if-changed=build.rs");
    // O `frontends/switch/compilar.sh` passa `--cfg zeebx_switch`. Sem este aviso o rustc
    // trata o nome como desconhecido.
    println!("cargo::rustc-check-cfg=cfg(zeebx_switch)");
    // O alvo do Dreamcast é um JSON com `os = "kallistios"`. Sem isto o rustc avisa que o
    // valor não está na lista que ele conhece, em todo arquivo que escolhe o interpretador.
    println!("cargo::rustc-check-cfg=cfg(target_os, values(\"kallistios\"))");
}
