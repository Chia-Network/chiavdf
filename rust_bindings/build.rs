use std::env;
use std::path::PathBuf;

use cmake::Config;

fn main() {
    println!("cargo:rerun-if-changed=wrapper.h");
    println!("cargo:rerun-if-changed=../src/c_bindings/c_wrapper.h");
    println!("cargo:rerun-if-changed=../src/c_bindings/c_wrapper.cpp");

    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());

    let is_fuzzing = std::env::var("CARGO_CFG_FUZZING").is_ok();
    let is_debug_build = std::env::var_os("OPT_LEVEL").unwrap_or("".into()) == "0";

    let mut src_dir = manifest_dir.join("cpp");
    if !src_dir.exists() {
        src_dir = manifest_dir
            .parent()
            .expect("can't access ../")
            .join("src")
            .to_path_buf();
    }

    let dst = Config::new(src_dir.as_path())
        .build_target("chiavdfc_static")
        .define("BUILD_CHIAVDFC", "ON")
        .env("BUILD_VDF_CLIENT", "N")
        .define("BUILD_PYTHON", "OFF")
        .define("HARDENING", if is_fuzzing || is_debug_build { "ON" } else { "OFF" })
        .very_verbose(true)
        .build();

    println!("cargo:rustc-link-lib=static=chiavdfc");

    println!(
        "cargo:rustc-link-search=native={}",
        dst.join("build")
            .join("lib")
            .join("static")
            .to_str()
            .unwrap()
    );

    if cfg!(target_os = "windows") {
        println!("cargo:rustc-link-lib=static=mpir");
        println!(
            "cargo:rustc-link-search=native={}",
            src_dir
                .parent()
                .unwrap()
                .join("mpir_gc_x64")
                .to_str()
                .unwrap()
        );
    } else if cfg!(target_os = "macos") {
        println!("cargo:rustc-link-lib=static=gmp");
        println!("cargo:rustc-link-search=native=/opt/homebrew/lib");
    } else {
        println!("cargo:rustc-link-lib=gmp");
    }

    let bindings = bindgen::Builder::default()
        .header(manifest_dir.join("wrapper.h").to_str().unwrap())
        .clang_arg("-x")
        .clang_arg("c++")
        .clang_arg(format!(
            "-I{}",
            src_dir.join("c_bindings").to_str().unwrap()
        ))
        .clang_arg("-std=c++14")
        .allowlist_function("verify_n_wesolowski_wrapper")
        .allowlist_function("create_discriminant_wrapper")
        .allowlist_function("prove_wrapper")
        .allowlist_function("free")
        .allowlist_function("delete_byte_array")
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        .generate()
        .expect("Unable to generate bindings");

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindings!");
}
