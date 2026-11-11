use chiavdf::{create_discriminant, prove};
use std::hint::black_box;
use std::process;
use std::time::Instant;

const DISCRIMINANT_SIZE_BITS: usize = 1024;

fn usage(program: &str) {
    eprintln!("Usage: {program} {{prove|discr}} N");
}

fn elapsed_ms(start: Instant) -> i64 {
    start.elapsed().as_millis().min(i64::MAX as u128) as i64
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let program = args.first().map_or("vdf_bench", String::as_str);

    if args.len() == 2 && args[1] == "--version" {
        println!("{program} {}", env!("CARGO_PKG_VERSION"));
        return;
    }
    if args.len() < 3 {
        usage(program);
        process::exit(1);
    }

    let iterations = args[2].parse::<u64>().unwrap_or_else(|_| {
        eprintln!("Invalid iteration count: {}", args[2]);
        process::exit(1);
    });
    if iterations == 0 {
        eprintln!("Iteration count must be greater than zero");
        process::exit(1);
    }

    match args[1].as_str() {
        "prove" => {
            // Match the fixed challenge and initial form used by the Python tests.
            let challenge = [0, 0, 1, 2, 3, 3, 4, 4];
            let mut x = [0_u8; 100];
            x[0] = 8;

            let start = Instant::now();
            let proof =
                prove(&challenge, &x, DISCRIMINANT_SIZE_BITS, iterations).unwrap_or_else(|| {
                    eprintln!("prove failed");
                    process::exit(1);
                });
            let duration = elapsed_ms(start);
            black_box(proof);

            let duration = if duration == 0 {
                println!("WARNING: too few iterations, results will be inaccurate!");
                1
            } else {
                duration
            };
            let duration = duration as u64;
            println!(
                "Time: {duration} ms; speed: {}.{}K ips",
                iterations / duration,
                (iterations.saturating_mul(10) / duration) % 10
            );
        }
        "discr" => {
            let mut seed: Vec<u8> = (0..32).collect();
            let mut discriminant = [0_u8; DISCRIMINANT_SIZE_BITS / 8];

            let start = Instant::now();
            for i in 0..iterations {
                let index = (i % seed.len() as u64) as usize;
                seed[index] = seed[index].wrapping_add(1);
                if !create_discriminant(&seed, &mut discriminant) {
                    eprintln!("create_discriminant failed");
                    process::exit(1);
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
        _ => {
            eprintln!("Unknown command");
            usage(program);
            process::exit(1);
        }
    }
}
