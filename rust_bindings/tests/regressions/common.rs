//! Shared fixtures for regression tests exercising the public Rust API.

use chiavdf::create_discriminant;
use std::path::Path;
use std::sync::OnceLock;

pub(super) const FORM_SIZE: usize = 100;
pub(super) const CHALLENGE: [u8; 8] = [0, 0, 1, 2, 3, 3, 4, 4];

pub(super) fn special_form(flag: u8) -> [u8; FORM_SIZE] {
    let mut form = [0; FORM_SIZE];
    form[0] = flag;
    form
}

pub(super) struct ProofCase {
    pub(super) discriminant: Vec<u8>,
    pub(super) x: Vec<u8>,
    pub(super) proof: Vec<u8>,
    pub(super) iterations: u64,
    pub(super) recursion: u64,
}

pub(super) fn corpus() -> &'static [ProofCase] {
    static CASES: OnceLock<Vec<ProofCase>> = OnceLock::new();
    CASES.get_or_init(|| {
        // Use the same corpus as the C++ stress test. Published crates bundle src as cpp.
        let manifest = Path::new(env!("CARGO_MANIFEST_DIR"));
        let bundled = manifest.join("cpp/vdf.txt");
        let path = if bundled.is_file() {
            bundled
        } else {
            manifest.join("../src/vdf.txt")
        };
        let text = std::fs::read_to_string(&path)
            .unwrap_or_else(|e| panic!("could not read {}: {e}", path.display()));
        let lines: Vec<_> = text.lines().collect();
        assert!(!lines.is_empty(), "proof corpus must not be empty");
        assert_eq!(lines.len() % 6, 0, "incomplete proof corpus record");
        lines
            .chunks_exact(6)
            .map(|record| {
                let challenge = hex::decode(record[0]).unwrap();
                let bits: usize = record[1].parse().unwrap();
                assert_eq!(bits % 8, 0);
                let mut discriminant = vec![0; bits / 8];
                assert!(create_discriminant(&challenge, &mut discriminant));
                ProofCase {
                    discriminant,
                    x: hex::decode(record[2]).unwrap(),
                    proof: hex::decode(record[3]).unwrap(),
                    iterations: record[4].parse().unwrap(),
                    recursion: record[5].parse().unwrap(),
                }
            })
            .collect()
    })
}
