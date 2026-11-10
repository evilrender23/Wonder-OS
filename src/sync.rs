//! Lock helpers that never panic.
//!
//! A `std` mutex is poisoned when a thread panics while holding it. The
//! compositor's own state is single-threaded, so poisoning can only come from
//! a helper thread (tile worker, font loader) or a caught panic; either way
//! the guarded data is still structurally valid, and a session that keeps
//! running beats one that aborts on the next lock. These helpers take the
//! guard regardless and log once per call site class.

use std::sync::{Condvar, Mutex, MutexGuard, PoisonError};

pub trait MutexExt<T: ?Sized> {
    /// `lock()`, recovering the guard from a poisoned mutex.
    fn lock_or_recover(&self) -> MutexGuard<'_, T>;
}

impl<T: ?Sized> MutexExt<T> for Mutex<T> {
    fn lock_or_recover(&self) -> MutexGuard<'_, T> {
        self.lock().unwrap_or_else(|poisoned| {
            tracing::warn!("recovering a poisoned mutex");
            PoisonError::into_inner(poisoned)
        })
    }
}

pub trait CondvarExt {
    /// `wait()`, recovering the guard from a poisoned mutex.
    fn wait_or_recover<'a, T>(&self, guard: MutexGuard<'a, T>) -> MutexGuard<'a, T>;
}

impl CondvarExt for Condvar {
    fn wait_or_recover<'a, T>(&self, guard: MutexGuard<'a, T>) -> MutexGuard<'a, T> {
        self.wait(guard).unwrap_or_else(|poisoned| {
            tracing::warn!("recovering a poisoned mutex after a condvar wait");
            PoisonError::into_inner(poisoned)
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Arc;

    #[test]
    fn poisoned_mutex_still_yields_its_data() {
        let m = Arc::new(Mutex::new(7));
        let m2 = Arc::clone(&m);
        let _ = std::thread::spawn(move || {
            let _guard = m2.lock().unwrap();
            panic!("poison it");
        })
        .join();
        assert!(m.is_poisoned());
        assert_eq!(*m.lock_or_recover(), 7);
    }
}
