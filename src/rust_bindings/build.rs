use std::env;
use std::path::PathBuf;

fn main() {
    println!("cargo:rerun-if-changed=wrapper.cpp");
    println!("cargo:rustc-link-lib=gmp");

    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());

    let mut src_dir = manifest_dir.join("cpp");
    if !src_dir.exists() {
        src_dir = manifest_dir
            .parent()
            .expect("can't access ../c_bindings")
            .join("c_bindings");
    }

    cc::Build::new()
        .cpp(true)
        .std("c++14")
        .files([src_dir.join("c_wrapper.cpp")])
        .warnings(false)
        .include(src_dir.as_path())
        .compile("chiavdf");

    let bindings = bindgen::Builder::default()
        .header(manifest_dir.join("wrapper.hpp").to_str().unwrap())
        .clang_arg("-x")
        .clang_arg("c++")
        .clang_arg(format!("-I{}", src_dir.to_str().unwrap()))
        .clang_arg("-std=c++14")
        .allowlist_function("verify_n_wesolowski_wrapper")
        .allowlist_function("create_discriminant_wrapper")
        .allowlist_function("free")
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        .generate()
        .expect("Unable to generate bindings");

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindings!");
}
