//! Contador de 64 bits que também existe num alvo sem atômico dessa largura.
//!
//! O JSON do Dreamcast declara `max-atomic-width` 32: o SH-4 não tem instrução atômica de
//! 64 bits, e o `core` compilado para esse alvo não tem `AtomicU64`. Onde o alvo tem, este
//! módulo é o tipo da biblioteca padrão — a mesma instrução, a mesma ordem.

#[cfg(target_has_atomic = "64")]
pub use std::sync::atomic::AtomicU64;

#[cfg(not(target_has_atomic = "64"))]
mod largo {
    use std::sync::atomic::Ordering;
    use std::sync::Mutex;

    /// O SH-4 do Dreamcast é um núcleo só. Um cadeado no lugar da instrução que não existe
    /// não muda o que os contadores medem; só deixa o crate compilar.
    pub struct AtomicU64(Mutex<u64>);

    impl AtomicU64 {
        pub const fn new(valor: u64) -> Self {
            Self(Mutex::new(valor))
        }

        pub fn load(&self, _ordem: Ordering) -> u64 {
            *self.trava()
        }

        pub fn store(&self, valor: u64, _ordem: Ordering) {
            *self.trava() = valor;
        }

        pub fn fetch_add(&self, valor: u64, _ordem: Ordering) -> u64 {
            let mut guarda = self.trava();
            let antes = *guarda;
            *guarda = antes.wrapping_add(valor);
            antes
        }

        pub fn swap(&self, valor: u64, _ordem: Ordering) -> u64 {
            let mut guarda = self.trava();
            let antes = *guarda;
            *guarda = valor;
            antes
        }

        fn trava(&self) -> std::sync::MutexGuard<'_, u64> {
            self.0.lock().unwrap_or_else(|envenenado| envenenado.into_inner())
        }
    }
}

#[cfg(not(target_has_atomic = "64"))]
pub use largo::AtomicU64;
