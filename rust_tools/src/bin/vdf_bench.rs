use chiavdf::{create_discriminant, prove};
use clap::{Parser, Subcommand};
use std::hint::black_box;
use std::num::NonZeroU64;
use std::time::Instant;

const DISCRIMINANT_SIZE_BITS: usize = 1024;

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
            println!(
                "Time: {duration} ms; speed: {}.{} ms/discr",
                duration as u64 / iterations,
                (duration as u64 * 10 / iterations) % 10
            );
        }
    }
    Ok(())
}
