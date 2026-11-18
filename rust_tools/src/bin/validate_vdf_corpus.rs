use chia_protocol::{ClassgroupElement, VDFInfo, VDFProof};
use chia_streamable_macro::Streamable;
use chia_traits::Streamable as StreamableTrait;
use chiavdf::{create_discriminant, verify_n_wesolowski};
use clap::Parser;
use memmap2::{Mmap, MmapOptions};
use std::fs::File;
use std::io::Cursor;
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::thread;

const DISCRIMINANT_SIZE_BITS: usize = 1024;
const FORM_SIZE: usize = ClassgroupElement::SIZE;

#[derive(Streamable)]
struct CorpusEntry {
    height: u32,
    input: ClassgroupElement,
    info: VDFInfo,
    proof: VDFProof,
}

#[derive(Parser)]
#[command(about = "Verify binary Streamable corpora from chia-tools/export-vdf-proofs.")]
struct Args {
    /// Begin verifying at this block height (inclusive).
    #[arg(long, default_value_t = 0)]
    start_height: u32,
    /// One or more concatenated corpus files.
    #[arg(required = true)]
    corpora: Vec<PathBuf>,
}

struct ProofRecord {
    number: u64,
    height: u32,
    challenge: [u8; 32],
    input: [u8; FORM_SIZE],
    output: [u8; FORM_SIZE],
    witness: Vec<u8>,
    iterations: u64,
    depth: u8,
}

impl TryFrom<CorpusEntry> for ProofRecord {
    type Error = String;

    fn try_from(entry: CorpusEntry) -> Result<Self, Self::Error> {
        let challenge = entry
            .info
            .challenge
            .as_ref()
            .try_into()
            .map_err(|_| "invalid challenge size")?;
        let input = entry
            .input
            .data
            .as_ref()
            .try_into()
            .map_err(|_| "invalid input size")?;
        let output = entry
            .info
            .output
            .data
            .as_ref()
            .try_into()
            .map_err(|_| "invalid output size")?;
        let witness = entry.proof.witness.as_ref().to_vec();
        Ok(Self {
            number: 0,
            height: entry.height,
            challenge,
            input,
            output,
            witness,
            iterations: entry.info.number_of_iterations,
            depth: entry.proof.witness_type,
        })
    }
}

#[derive(Default)]
struct Totals {
    checked: u64,
    failures: u64,
    run_proofs: u64,
    runs: u64,
}

struct Shared<'a> {
    cursor: Cursor<&'a [u8]>,
    record_number: u64,
    totals: Totals,
}

fn read_record(cursor: &mut Cursor<&[u8]>) -> ProofRecord {
    let offset = cursor.position() as usize;
    let remaining = cursor.get_ref().len().saturating_sub(offset);
    let height = (remaining >= 4).then(|| {
        u32::from_be_bytes(
            cursor.get_ref()[offset..offset + 4]
                .try_into()
                .expect("height slice has four bytes"),
        )
    });
    let entry = CorpusEntry::parse::<false>(cursor).unwrap_or_else(|error| match height {
        Some(height) => panic!("corrupt corpus record at height={height}: {error}"),
        None => panic!("corrupt corpus record: {error}"),
    });
    ProofRecord::try_from(entry).expect("Streamable proof record has invalid field sizes")
}

fn verify_run(records: &[ProofRecord], path: &Path, start_height: u32, shared: &Mutex<Shared<'_>>) {
    let selected: Vec<_> = records
        .iter()
        .filter(|record| record.height >= start_height)
        .collect();
    if selected.is_empty() {
        return;
    }

    let mut discriminant = [0_u8; DISCRIMINANT_SIZE_BITS / 8];
    let discriminant_error = (!create_discriminant(&selected[0].challenge, &mut discriminant))
        .then_some("could not create discriminant".to_string());

    for record in selected {
        let mut error = discriminant_error.clone();
        if error.is_none() {
            let mut proof_blob = Vec::with_capacity(FORM_SIZE + record.witness.len());
            proof_blob.extend_from_slice(&record.output);
            proof_blob.extend_from_slice(&record.witness);
            if !verify_n_wesolowski(
                &discriminant,
                &record.input,
                &proof_blob,
                record.iterations,
                u64::from(record.depth),
            ) {
                error = Some("INVALID".to_string());
            }
        }

        let mut state = shared.lock().expect("corpus state lock poisoned");
        state.totals.checked += 1;
        if let Some(error) = error {
            state.totals.failures += 1;
            eprintln!(
                "{}: record {}: height={}: {error}",
                path.display(),
                record.number,
                record.height
            );
        }
        if state.totals.checked.is_multiple_of(10_000) {
            let average = state.totals.run_proofs as f64 / state.totals.runs as f64;
            println!(
                "{}: checked {} proofs, {} failures, average challenge run length {:.2} (height={})",
                path.display(), state.totals.checked, state.totals.failures, average, record.height
            );
        }
    }
}

fn worker(path: &Path, mmap: &Mmap, start_height: u32, shared: &Mutex<Shared<'_>>) {
    loop {
        let (records, selected_count) = {
            let mut state = shared.lock().expect("corpus state lock poisoned");
            if state.cursor.position() as usize >= mmap.len() {
                return;
            }
            let first_number = state.record_number + 1;
            let mut first = read_record(&mut state.cursor);
            first.number = first_number;
            let challenge = first.challenge;
            let mut records = vec![first];
            state.record_number += 1;
            loop {
                if state.cursor.position() as usize >= mmap.len() {
                    break;
                }
                let mut candidate_cursor = state.cursor.clone();
                let mut candidate = read_record(&mut candidate_cursor);
                if candidate.challenge == challenge {
                    state.cursor = candidate_cursor;
                    state.record_number += 1;
                    candidate.number = state.record_number;
                    records.push(candidate);
                } else {
                    break;
                }
            }
            let selected_count = records
                .iter()
                .filter(|record| record.height >= start_height)
                .count() as u64;
            if selected_count > 0 {
                state.totals.run_proofs += selected_count;
                state.totals.runs += 1;
            }
            (records, selected_count)
        };

        if selected_count > 0 {
            verify_run(&records, path, start_height, shared);
        }
    }
}

fn map_file(path: &Path) -> Mmap {
    let file = File::open(path)
        .unwrap_or_else(|error| panic!("{}: cannot open corpus: {error}", path.display()));
    // SAFETY: The mapping is read-only and the file is kept immutable while validation runs.
    unsafe {
        MmapOptions::new()
            .map(&file)
            .unwrap_or_else(|error| panic!("{}: cannot map corpus: {error}", path.display()))
    }
}

fn verify_file(path: &Path, start_height: u32, workers: usize) -> Totals {
    println!("{}: start height {start_height}", path.display());
    let mmap = map_file(path);
    let shared = Mutex::new(Shared {
        cursor: Cursor::new(&mmap[..]),
        record_number: 0,
        totals: Totals::default(),
    });
    thread::scope(|scope| {
        let mut threads = Vec::with_capacity(workers);
        for _ in 0..workers {
            threads.push(scope.spawn(|| worker(path, &mmap, start_height, &shared)));
        }
        for thread in threads {
            thread.join().expect("corpus validation worker panicked");
        }
    });
    let mut totals = std::mem::take(&mut shared.lock().expect("corpus state lock poisoned").totals);
    if totals.runs == 0 {
        totals.run_proofs = 0;
    }
    println!(
        "{}: checked {} proofs, {} failures",
        path.display(),
        totals.checked,
        totals.failures
    );
    totals
}

fn main() {
    let args = Args::parse();
    let workers = thread::available_parallelism().map_or(1, usize::from);
    println!("Using {workers} verifier threads");
    let mut total_proofs = 0_u64;
    let mut total_failures = 0_u64;
    let mut failed_files = 0_u64;
    for path in &args.corpora {
        let result = verify_file(path, args.start_height, workers);
        total_proofs += result.checked;
        total_failures += result.failures;
        failed_files += u64::from(result.failures != 0);
    }
    println!(
        "Total: {total_proofs} proofs, {total_failures} failures, {failed_files} failed files out of {}",
        args.corpora.len()
    );
    if total_failures != 0 {
        std::process::exit(1);
    }
}
