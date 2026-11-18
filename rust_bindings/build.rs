use std::env;
use std::fs;
use std::path::PathBuf;

use cmake::Config;

fn main() {
    println!("cargo:rerun-if-env-changed=CHIAVDF_GMP_PREFIX");
    println!("cargo:rerun-if-env-changed=MINGW_LIBMINGWEX");
    println!("cargo:rerun-if-env-changed=MINGW_LIBGCC");
    println!("cargo:rerun-if-env-changed=CHIAVDF_CMAKE_DEFINES");
    println!("cargo:rerun-if-env-changed=CHIAVDF_EXTRA_LINK_LIB");
    println!("cargo:rerun-if-env-changed=CHIAVDF_USE_MPIR");
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

    let mut config = Config::new(src_dir.as_path());
    config
        .build_target("chiavdfc_static")
        .define("BUILD_CHIAVDFC", "ON")
        .env("BUILD_VDF_CLIENT", "N")
        .define("BUILD_PYTHON", "OFF")
        .define(
            "HARDENING",
            if is_fuzzing || is_debug_build {
                "ON"
            } else {
                "OFF"
            },
        );

    // Experiment hook: extra CMake cache entries, as "NAME=VALUE;NAME2=VALUE2".
    if let Ok(defines) = env::var("CHIAVDF_CMAKE_DEFINES") {
        for define in defines.split(';').filter(|d| !d.is_empty()) {
            let (name, value) = define
                .split_once('=')
                .expect("CHIAVDF_CMAKE_DEFINES entries must be NAME=VALUE");
            config.define(name, value);
        }
    }

    let target_os = std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let target_env = std::env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    // Experiment hook: CHIAVDF_USE_MPIR=1 builds against ../mpir_gc_x64 the way
    // main does, for comparing MPIR with MinGW GMP on Windows.
    let use_mpir = env::var("CHIAVDF_USE_MPIR").as_deref() == Ok("1");
    let gmp_prefix = if use_mpir {
        None
    } else {
        env::var_os("CHIAVDF_GMP_PREFIX")
            .map(PathBuf::from)
            .or_else(|| {
                let bundled_prefix = manifest_dir.join("gmp-win64");
                bundled_prefix.is_dir().then_some(bundled_prefix)
            })
    };

    if target_os == "windows" && target_env == "msvc" {
        if let Some(prefix) = &gmp_prefix {
            config.define(
                "CHIAVDF_GMP_PREFIX",
                prefix.to_str().expect("GMP prefix is not UTF-8"),
            );
        }
    }

    if target_os == "windows" && target_env == "msvc" {
        // Rust MSVC links against msvcrt in both debug/release test builds.
        // Keep CMake-produced static libs on the same CRT to avoid *_dbg unresolved symbols.
        config
            .define("CMAKE_POLICY_DEFAULT_CMP0091", "NEW")
            .define("CMAKE_MSVC_RUNTIME_LIBRARY", "MultiThreadedDLL");
    }

    let dst = config.build();

    println!("cargo:rustc-link-lib=static=chiavdfc");

    println!(
        "cargo:rustc-link-search=native={}",
        dst.join("build")
            .join("lib")
            .join("static")
            .to_str()
            .unwrap()
    );

    if target_os == "windows" && use_mpir {
        println!("cargo:rustc-link-lib=static=mpir");
        println!(
            "cargo:rustc-link-search=native={}",
            src_dir.parent().unwrap().join("mpir_gc_x64").display()
        );
    } else if target_os == "windows" {
        let prefix = gmp_prefix.unwrap_or_else(|| {
            panic!("CHIAVDF_GMP_PREFIX must point to a MinGW-built GMP installation on Windows")
        });
        println!("cargo:rustc-link-lib=static=gmp");
        println!(
            "cargo:rustc-link-search=native={}",
            prefix.join("lib").display()
        );
        for runtime_archive in ["MINGW_LIBMINGWEX", "MINGW_LIBGCC"] {
            let archive_name = if runtime_archive == "MINGW_LIBMINGWEX" {
                "libmingwex.a"
            } else {
                "libgcc.a"
            };
            let archive = env::var_os(runtime_archive)
                .map(PathBuf::from)
                .unwrap_or_else(|| prefix.join("lib").join(archive_name));
            assert!(
                archive.is_file(),
                "MinGW runtime archive not found: {}",
                archive.display()
            );
            println!(
                "cargo:rustc-link-search=native={}",
                archive.parent().unwrap().display()
            );
        }
        println!("cargo:rustc-link-lib=static:+verbatim=libmingwex.a");
        println!("cargo:rustc-link-lib=static:+verbatim=libgcc.a");
    } else if target_os == "macos" {
        println!("cargo:rustc-link-lib=static=gmp");
        let homebrew_path = if fs::metadata("/opt/homebrew").is_ok() {
            "/opt/homebrew/lib"
        } else {
            "/usr/local/lib"
        };
        println!("cargo:rustc-link-search=native={homebrew_path}");
    } else {
        println!("cargo:rustc-link-lib=gmp");
    }

    // Experiment hook: one extra static library to link, given as a full path
    // (e.g. clang_rt.builtins-x86_64.lib for clang-cl's __int128 helpers).
    if let Some(lib) = env::var_os("CHIAVDF_EXTRA_LINK_LIB").map(PathBuf::from) {
        assert!(lib.is_file(), "CHIAVDF_EXTRA_LINK_LIB not found: {}", lib.display());
        println!(
            "cargo:rustc-link-search=native={}",
            lib.parent().unwrap().display()
        );
        println!(
            "cargo:rustc-link-lib=static:+verbatim={}",
            lib.file_name().unwrap().to_str().unwrap()
        );
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
