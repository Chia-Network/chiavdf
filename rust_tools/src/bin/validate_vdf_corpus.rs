use chia_protocol::{ClassgroupElement, VDFInfo, VDFProof};
use chia_streamable_macro::Streamable;
use chia_traits::Streamable as StreamableTrait;
use chiavdf::{create_discriminant, verify_n_wesolowski};
use memmap2::{Mmap, MmapOptions};
use std::env;
use std::fs::File;
use std::io::Cursor;
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::thread;

const DISCRIMINANT_SIZE_BITS: usize = 1024;
const FORM_SIZE: usize = ClassgroupElement::SIZE;
const MAX_WITNESS_SIZE: usize = FORM_SIZE + 255 * (8 + 33 + FORM_SIZE);

#[derive(Streamable)]
struct CorpusEntry {
    height: u32,
    input: ClassgroupElement,
    info: VDFInfo,
    proof: VDFProof,
}

struct Args {
    start_height: u32,
    corpora: Vec<PathBuf>,
}

impl Args {
    fn parse() -> Result<Self, String> {
        let mut args = env::args().skip(1);
        let mut start_height = 0;
        let mut corpora = Vec::new();
        let mut files_only = false;
        while let Some(arg) = args.next() {
            if !files_only && arg == "--help" {
                println!(
                    "Usage: validate_vdf_corpus [--start-height HEIGHT] CORPUS.bin [CORPUS.bin ...]\n\
                     Verify binary Streamable corpora from chia-tools/export-vdf-proofs."
                );
                std::process::exit(0);
            }
            if !files_only && arg == "--" {
                files_only = true;
            } else if !files_only && arg == "--start-height" {
                let value = args
                    .next()
                    .ok_or("--start-height requires one HEIGHT value")?;
                start_height = value
                    .parse::<u32>()
                    .map_err(|_| format!("invalid start height: {value}"))?;
            } else if !files_only && arg.starts_with('-') {
                return Err(format!("unknown option: {arg}"));
            } else {
                corpora.push(PathBuf::from(arg));
            }
        }
        if corpora.is_empty() {
            return Err("at least one corpus file is required".to_string());
        }
        Ok(Self {
            start_height,
            corpora,
        })
    }
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
        if witness.len() > MAX_WITNESS_SIZE {
            return Err("witness exceeds maximum encoded size".to_string());
        }
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
    stop: bool,
    totals: Totals,
}

fn read_record(cursor: &mut Cursor<&[u8]>) -> Result<ProofRecord, String> {
    let offset = cursor.position() as usize;
    let remaining = cursor.get_ref().len().saturating_sub(offset);
    // The Streamable parser allocates the variable-length witness. Check its
    // declared length first so malformed input cannot trigger an oversized allocation.
    if remaining >= 249 {
        let witness_size = u32::from_be_bytes(
            cursor.get_ref()[offset + 245..offset + 249]
                .try_into()
                .unwrap(),
        ) as usize;
        if witness_size > MAX_WITNESS_SIZE {
            let height =
                u32::from_be_bytes(cursor.get_ref()[offset..offset + 4].try_into().unwrap());
            return Err(format!(
                "height={height}: witness exceeds maximum encoded size"
            ));
        }
    }
    let height = (remaining >= 4)
        .then(|| u32::from_be_bytes(cursor.get_ref()[offset..offset + 4].try_into().unwrap()));
    let entry = CorpusEntry::parse::<false>(cursor).map_err(|error| match height {
        Some(height) => format!("height={height}: {error}"),
        None => error.to_string(),
    })?;
    ProofRecord::try_from(entry).map_err(|error| match height {
        Some(height) => format!("height={height}: {error}"),
        None => error,
    })
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

        let mut state = shared.lock().unwrap();
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
            let mut state = shared.lock().unwrap();
            if state.stop || state.cursor.position() as usize >= mmap.len() {
                return;
            }
            let first_number = state.record_number + 1;
            let mut first = match read_record(&mut state.cursor) {
                Ok(record) => record,
                Err(error) => {
                    state.stop = true;
                    state.totals.failures += 1;
                    eprintln!("{}: record {}: {error}", path.display(), first_number);
                    return;
                }
            };
            first.number = first_number;
            let challenge = first.challenge;
            let mut records = vec![first];
            state.record_number += 1;
            loop {
                if state.cursor.position() as usize >= mmap.len() {
                    break;
                }
                let mut candidate_cursor = state.cursor.clone();
                match read_record(&mut candidate_cursor) {
                    Ok(mut candidate) if candidate.challenge == challenge => {
                        state.cursor = candidate_cursor;
                        state.record_number += 1;
                        candidate.number = state.record_number;
                        records.push(candidate);
                    }
                    Ok(_) => break,
                    Err(error) => {
                        state.stop = true;
                        state.totals.failures += 1;
                        eprintln!(
                            "{}: record {}: {error}",
                            path.display(),
                            state.record_number + 1
                        );
                        break;
                    }
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

fn map_file(path: &Path) -> Result<Mmap, String> {
    let file = File::open(path).map_err(|_| "cannot open corpus".to_string())?;
    let metadata = file
        .metadata()
        .map_err(|_| "cannot determine corpus size".to_string())?;
    if !metadata.is_file() || metadata.len() == 0 {
        return Err(if metadata.len() == 0 {
            "empty corpus"
        } else {
            "cannot determine corpus size"
        }
        .to_string());
    }
    // SAFETY: The mapping is read-only and the file is kept immutable while validation runs.
    unsafe {
        MmapOptions::new()
            .map(&file)
            .map_err(|_| "cannot map corpus".to_string())
    }
}

fn verify_file(path: &Path, start_height: u32, workers: usize) -> Totals {
    println!("{}: start height {start_height}", path.display());
    let mmap = match map_file(path) {
        Ok(mmap) => mmap,
        Err(error) => {
            eprintln!("{}: {error}", path.display());
            return Totals {
                failures: 1,
                ..Totals::default()
            };
        }
    };
    let shared = Mutex::new(Shared {
        cursor: Cursor::new(&mmap[..]),
        record_number: 0,
        stop: false,
        totals: Totals::default(),
    });
    thread::scope(|scope| {
        let mut threads = Vec::with_capacity(workers);
        for _ in 0..workers {
            threads.push(scope.spawn(|| worker(path, &mmap, start_height, &shared)));
        }
        for thread in threads {
            let _ = thread.join();
        }
    });
    let mut totals = std::mem::take(&mut shared.lock().unwrap().totals);
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
    let args = match Args::parse() {
        Ok(args) => args,
        Err(error) => {
            eprintln!("{error}");
            std::process::exit(2);
        }
    };
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
