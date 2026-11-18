use chiavdf::{create_discriminant, prove};
use clap::{Parser, Subcommand};
use std::hint::black_box;
use std::num::NonZeroU64;
use std::time::Instant;

const DISCRIMINANT_SIZE_BITS: usize = 1024;

/// Experiment hook (gmp-windows-perf): with CHIAVDF_BENCH_GMP_ALLOC=mimalloc,
/// route all of GMP's allocations through mimalloc instead of the C runtime's
/// malloc, to measure how much the platform allocator costs chiavdf.
mod gmp_alloc {
    use mimalloc::MiMalloc;
    use std::alloc::{GlobalAlloc, Layout};

    const ALIGN: usize = 16;

    unsafe fn layout(size: usize) -> Layout {
        Layout::from_size_align_unchecked(size.max(1), ALIGN)
    }
    unsafe extern "C" fn alloc(size: usize) -> *mut u8 {
        MiMalloc.alloc(layout(size))
    }
    unsafe extern "C" fn realloc(ptr: *mut u8, old_size: usize, new_size: usize) -> *mut u8 {
        MiMalloc.realloc(ptr, layout(old_size), new_size.max(1))
    }
    unsafe extern "C" fn free(ptr: *mut u8, size: usize) {
        MiMalloc.dealloc(ptr, layout(size))
    }

    extern "C" {
        // mp_set_memory_functions is a macro for this symbol in gmp.h.
        fn __gmp_set_memory_functions(
            alloc: Option<unsafe extern "C" fn(usize) -> *mut u8>,
            realloc: Option<unsafe extern "C" fn(*mut u8, usize, usize) -> *mut u8>,
            free: Option<unsafe extern "C" fn(*mut u8, usize)>,
        );
    }

    /// Must run before GMP allocates anything.
    pub fn install_from_env() -> &'static str {
        if std::env::var("CHIAVDF_BENCH_GMP_ALLOC").as_deref() == Ok("mimalloc") {
            unsafe { __gmp_set_memory_functions(Some(alloc), Some(realloc), Some(free)) };
            "mimalloc"
        } else {
            "system"
        }
    }
}

/// With BENCH_LABEL set, also print a machine-readable line for the CI summary.
fn report(metric: &str, value: f64) {
    if let Ok(label) = std::env::var("BENCH_LABEL") {
        println!("RESULT|{label}|{metric}|{value:.2}");
    }
}

#[derive(Parser)]
#[command(name = "vdf_bench", version, about = "Benchmark chiavdf Rust bindings")]
struct Args {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    /// Benchmark proof generation for N VDF iterations.
    Prove { iterations: NonZeroU64 },
    /// Benchmark N discriminant creations.
    Discr { iterations: NonZeroU64 },
}

fn elapsed_ms(start: Instant) -> i64 {
    start.elapsed().as_millis().min(i64::MAX as u128) as i64
}

fn main() -> Result<(), String> {
    let allocator = gmp_alloc::install_from_env();
    println!("GMP allocator: {allocator}");
    match Args::parse().command {
        Command::Prove { iterations } => {
            let iterations = iterations.get();
            // Match the fixed challenge and initial form used by the Python tests.
            let challenge = [0, 0, 1, 2, 3, 3, 4, 4];
            let mut x = [0_u8; 100];
            x[0] = 8;

            let start = Instant::now();
            let proof = prove(&challenge, &x, DISCRIMINANT_SIZE_BITS, iterations)
                .ok_or_else(|| "prove failed".to_string())?;
            let duration = elapsed_ms(start);
            black_box(proof);

            let duration = if duration == 0 {
                println!("WARNING: too few iterations, results will be inaccurate!");
                1
            } else {
                duration
            };
            let duration = duration as u64;
            let speed_tenths_k_ips = iterations
                .checked_mul(10)
                .ok_or_else(|| "iteration count overflows speed calculation".to_string())?
                / duration;
            report("vdf prove (K ips)", iterations as f64 / duration as f64);
            println!(
                "Time: {duration} ms; speed: {}.{}K ips",
                speed_tenths_k_ips / 10,
                speed_tenths_k_ips % 10
            );
        }
        Command::Discr { iterations } => {
            let iterations = iterations.get();
            let mut seed: Vec<u8> = (0..32).collect();
            let mut discriminant = [0_u8; DISCRIMINANT_SIZE_BITS / 8];

            let start = Instant::now();
            for i in 0..iterations {
                let index = (i % seed.len() as u64) as usize;
                seed[index] = seed[index].wrapping_add(1);
                if !create_discriminant(&seed, &mut discriminant) {
                    return Err("create_discriminant failed".to_string());
                }
                black_box(&discriminant);
            }
            let duration = elapsed_ms(start);
            let duration = if duration == 0 {
                println!("WARNING: too few iterations, results will be inaccurate!");
                1
            } else {
                duration
            };
            report("vdf discr (ms)", duration as f64 / iterations as f64);
            println!(
                "Time: {duration} ms; speed: {}.{} ms/discr",
                duration as u64 / iterations,
                (duration as u64 * 10 / iterations) % 10
            );
        }
    }
    Ok(())
}
