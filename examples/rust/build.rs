// SPDX-License-Identifier: GPL-2.0+
/*
 * Build script for U-Boot Rust demo
 *
 * This configures linking with the U-Boot library, similar to how
 * the C examples are built in examples/ulib
 *
 * Copyright 2025 Canonical Ltd.
 * Written by Simon Glass <simon.glass@canonical.com>
 */

use std::env;
use std::path::PathBuf;
use std::process::Command;

fn main() {
    // Get the U-Boot build directory from environment or use default
    let uboot_build = env::var("UBOOT_BUILD")
        .unwrap_or_else(|_| "/tmp/b/sandbox".to_string());


    let uboot_build_path = PathBuf::from(&uboot_build);

    println!("cargo:rerun-if-env-changed=UBOOT_BUILD");
    println!("cargo:rerun-if-changed=build.rs");

    // Add library search path
    println!("cargo:rustc-link-search=native={}", uboot_build_path.display());

    // Check if dynamic linking is requested
    if env::var("UBOOT_DYNAMIC").is_ok() {
        // Dynamic linking with libu-boot.so
        println!("cargo:rustc-link-lib=dylib=u-boot");
        println!("cargo:rustc-link-arg=-Wl,-rpath,{}", uboot_build_path.display());
    } else {
        // Static linking with libu-boot.a using whole-archive to ensure
        // all symbols are included (required for U-Boot linker lists)
        // Use the same linker script as the C examples for proper linker list ordering
        let static_lib_path = uboot_build_path.join("libu-boot.a");
        // Use absolute path to the linker script in the source tree
        let current_dir = env::current_dir().expect("Failed to get current directory");
        let linker_script_path = current_dir.join("../ulib/static.lds");

        println!("cargo:rustc-link-arg=-Wl,-T,{}", linker_script_path.display());
        println!("cargo:rustc-link-arg=-Wl,--whole-archive");
        println!("cargo:rustc-link-arg={}", static_lib_path.display());
        println!("cargo:rustc-link-arg=-Wl,--no-whole-archive");
        println!("cargo:rustc-link-arg=-Wl,-z,noexecstack");

        // Add required system libraries AFTER --no-whole-archive like the C version
        println!("cargo:rustc-link-arg=-lpthread");
        println!("cargo:rustc-link-arg=-ldl");

        // Get SDL libraries using sdl2-config like the C version does
        if let Ok(output) = Command::new("sdl2-config").arg("--libs").output() {
            if output.status.success() {
                let libs_str = String::from_utf8_lossy(&output.stdout);
                for lib_flag in libs_str.split_whitespace() {
                    if lib_flag.starts_with("-l") {
                        println!("cargo:rustc-link-arg={}", lib_flag);
                    }
                }
            }
        } else {
            // Fallback to just SDL2 if sdl2-config is not available
            println!("cargo:rustc-link-arg=-lSDL2");
        }

        // Link with libbacktrace for backtrace support on sandbox
        println!("cargo:rustc-link-arg=-lbacktrace");

        // When libu-boot.a is built with AddressSanitizer or coverage, its
        // objects reference __asan_*/__gcov_* symbols that come from libasan
        // and libgcov respectively. rust-lld does not pull these in via
        // -fsanitize=address, so list the libraries explicitly.
        if env::var("UBOOT_ASAN").as_deref() == Ok("y") {
            println!("cargo:rustc-link-arg=-lasan");
        }
        if env::var("UBOOT_COVERAGE").as_deref() == Ok("y") {
            println!("cargo:rustc-link-arg=-lgcov");
        }
    }

    // For dynamic linking, link required system libraries normally
    if env::var("UBOOT_DYNAMIC").is_ok() {
        println!("cargo:rustc-link-lib=pthread");
        println!("cargo:rustc-link-lib=dl");
        println!("cargo:rustc-link-lib=rt");
    }

    // Optional SDL2 support (if available) - only for dynamic linking
    if env::var("UBOOT_DYNAMIC").is_ok() {
        if let Ok(sdl_libs) = env::var("SDL_LIBS") {
            for lib in sdl_libs.split_whitespace() {
                if let Some(lib_name) = lib.strip_prefix("-l") {
                    println!("cargo:rustc-link-lib={}", lib_name);
                }
            }
        }
    }

    // Define symbols that may be missing (from U-Boot troubleshooting docs)
    // Note: __stack_chk_guard is provided by U-Boot's common/stackprot.c
    println!("cargo:rustc-link-arg=-Wl,--defsym,sandbox_sdl_sync=0");

    // For static linking, we need to ensure libc is available for atexit and other functions
    if env::var("UBOOT_DYNAMIC").is_err() {
        println!("cargo:rustc-link-arg=-lc");
    }

    // Add include path for headers if needed for bindgen (future enhancement)
    let uboot_include = uboot_build_path.join("include");
    if uboot_include.exists() {
        println!("cargo:include={}", uboot_include.display());
    }

    // Recompile if main source changes
    println!("cargo:rerun-if-changed=src/demo.rs");
}
