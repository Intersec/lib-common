/***************************************************************************/
/*                                                                         */
/* Copyright 2025 INTERSEC SA                                              */
/*                                                                         */
/* Licensed under the Apache License, Version 2.0 (the "License");         */
/* you may not use this file except in compliance with the License.        */
/* You may obtain a copy of the License at                                 */
/*                                                                         */
/*     http://www.apache.org/licenses/LICENSE-2.0                          */
/*                                                                         */
/* Unless required by applicable law or agreed to in writing, software     */
/* distributed under the License is distributed on an "AS IS" BASIS,       */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.*/
/* See the License for the specific language governing permissions and     */
/* limitations under the License.                                          */
/*                                                                         */
/***************************************************************************/

use std::fmt::Write;
use std::fs::{self, File};
use std::io::{self, Write as _};
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};

use clap::Parser;

mod bindings {
    #![allow(non_upper_case_globals)]
    #![allow(non_camel_case_types)]
    #![allow(non_snake_case)]
    include!("_bindings.rs");
}
use bindings::*;

impl From<&[u8]> for lstr_t {
    fn from(buf: &[u8]) -> Self {
        Self {
            __bindgen_anon_1: lstr_t__bindgen_ty_1 {
                s: buf.as_ptr() as *const i8,
            },
            len: buf.len() as i32,
            mem_pool: 0,
        }
    }
}

const PATHMAX: i32 = 4096;
const LZO_BUF_MEM_SIZE: usize = 1 << (14 + std::mem::size_of::<u32>());
const FARCH_MAX_SYMBOL_SIZE: usize = 128;

struct FarchEntry {
    pub name: String,
    pub compressed_size: i32,
    pub size: i32,
    pub nb_chunks: i32,
}

/// Processes a farch script
#[derive(Debug, Parser)]
#[command(name = "farch", about = "Processes a farch script")]
struct Opts {
    /// The farch script file to process
    script: PathBuf,

    /// Output file
    #[arg(short, long, value_name = "FILE")]
    out: Option<PathBuf>,

    /// Be verbose
    #[arg(short, long, action = clap::ArgAction::SetTrue)]
    verbose: bool,
}

fn rand_range(first: usize, last: usize) -> usize {
    rand::random_range(first..=last)
}

fn put_as_str(data: &[u8], output: &mut dyn Write) {
    for byte in data {
        write!(output, "\\x{:x}", byte).unwrap();
    }
}

fn put_chunk(chunk: &[u8], output: &mut dyn Write) {
    write!(output, "    LSTR_IMMED(\"").unwrap();
    put_as_str(chunk, output);
    writeln!(output, "\"),").unwrap();
}

fn obfuscate_data(data: &[u8], key: u64, output: &[u8]) {
    let src: lstr_t = data.into();
    let dst: lstr_t = output.into();

    unsafe {
        lstr_obfuscate(src, key, dst);
    }
}

fn dump_and_obfuscate(buf: &[u8], output: &mut dyn Write) -> i32 {
    let mut nb_chunks: i32 = 0;
    let mut len = buf.len();
    let mut start_slice: usize = 0;

    while len > 0 {
        let chunk_size = rand_range(FARCH_MAX_SYMBOL_SIZE / 2, FARCH_MAX_SYMBOL_SIZE);
        let chunk_size = std::cmp::min(chunk_size, len);
        let end_slice: usize = start_slice + chunk_size;
        let obfuscated_chunk_slice = &buf[start_slice..end_slice];
        let obfuscated_output_buf = [0u8; FARCH_MAX_SYMBOL_SIZE];
        let obfuscated_output = &obfuscated_output_buf[0..chunk_size];

        obfuscate_data(obfuscated_chunk_slice, chunk_size as u64, obfuscated_output);
        put_chunk(obfuscated_output, output);

        start_slice = end_slice;
        len -= chunk_size;
        nb_chunks += 1;
    }

    nb_chunks
}

fn dump_file(path: &Path, entry: &mut FarchEntry, output: &mut dyn Write) {
    let file_data = std::fs::read(path).unwrap_or_else(|e| {
        eprintln!("Error: unable to read file `{}`: {}", path.display(), e);
        std::process::exit(1);
    });

    entry.size = file_data.len() as i32;

    let mut clen = unsafe { lzo_cbuf_size(file_data.len()) };
    let mut cbuf = vec![0u8; clen];
    let mut lzo_buf = [0u8; LZO_BUF_MEM_SIZE];
    let start_ptr: *const u8 = file_data.as_ptr();
    let end_ptr: *const u8 = file_data.as_slice().last().unwrap();
    let ps = unsafe {
        pstream_t {
            __bindgen_anon_1: pstream_t__bindgen_ty_1 { b: start_ptr },
            __bindgen_anon_2: pstream_t__bindgen_ty_2 {
                b_end: end_ptr.offset(1),
            },
        }
    };

    unsafe {
        clen = qlzo1x_compress(
            cbuf.as_mut_ptr() as *mut std::os::raw::c_void,
            clen,
            ps,
            lzo_buf.as_mut_ptr() as *mut std::os::raw::c_void,
        );
    }

    if clen < file_data.len() {
        entry.nb_chunks = dump_and_obfuscate(&cbuf[0..clen], output);
        entry.compressed_size = clen as i32;
    } else {
        entry.nb_chunks = dump_and_obfuscate(&file_data, output);
        entry.compressed_size = file_data.len() as i32;
    }
}

fn dump_entries(archname: &str, entries: &[FarchEntry], output: &mut dyn Write) {
    let mut chunk = 0;

    for entry in entries {
        let obfuscated_name = vec![0u8; entry.name.len()];

        obfuscate_data(
            entry.name.as_bytes(),
            entry.nb_chunks as u64,
            &obfuscated_name,
        );
        writeln!(output, "/* {{{{{{ {} */", entry.name).unwrap();
        writeln!(output, "{{").unwrap();
        write!(output, "    .name = LSTR_IMMED(\"").unwrap();
        put_as_str(&obfuscated_name, output);
        writeln!(output, "\"),").unwrap();
        writeln!(output, "    .chunks = &{}_data[{}],", archname, chunk).unwrap();
        writeln!(output, "    .size = {},", entry.size).unwrap();
        writeln!(output, "    .compressed_size = {},", entry.compressed_size).unwrap();
        writeln!(output, "    .nb_chunks = {},", entry.nb_chunks).unwrap();
        writeln!(output, "}},").unwrap();
        writeln!(output, "/* }}}}}} */").unwrap();
        chunk += entry.nb_chunks;
    }
}

fn do_work(opts: &Opts, reldir: &Path, output: &mut impl Write) {
    let content = std::fs::read_to_string(&opts.script).unwrap_or_else(|e| {
        eprintln!(
            "Error: could not read script file `{}`: {}",
            opts.script.display(),
            e
        );
        std::process::exit(1);
    });

    let mut lines = content.lines();
    let mut name_opt: Option<&str> = None;

    // Find first non-empty line and non-comment line for the name
    for line in &mut lines {
        let trimmed = line.trim();
        if trimmed.is_empty() || trimmed.starts_with('#') {
            continue;
        }
        name_opt = Some(trimmed);
        break;
    }

    let name = name_opt.unwrap_or_else(|| {
        eprintln!("Error: no variable name specified");
        std::process::exit(1);
    });

    if opts.verbose {
        eprintln!("farchc: creating `{}`", name);
    }

    writeln!(output, "/* This file is generated by farchc. */").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "#include <lib-common/farch.h>").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "static const farch_data_t {}_data[] = {{", name).unwrap();

    let mut entries = Vec::<FarchEntry>::new();
    let mut lineno = 2;

    for line in lines {
        lineno += 1;

        if line.len() > PATHMAX as usize {
            eprintln!("Error: line {} is too long", lineno);
            std::process::exit(1);
        }

        let path = line.trim();
        if path.is_empty() {
            continue;
        }

        if path.starts_with('#') {
            if opts.verbose {
                eprintln!("farchc: {}", path);
            }
            continue;
        }

        let fullpath = reldir.join(path);

        if opts.verbose {
            eprintln!("farchc: adding `{}` as `{}`", path, fullpath.display());
        }

        let mut entry = FarchEntry {
            name: path.to_owned(),
            size: 0,
            compressed_size: 0,
            nb_chunks: 0,
        };

        writeln!(output, "/* {{{{{{ {} */", path).unwrap();

        dump_file(&fullpath, &mut entry, output);
        writeln!(output, "/* }}}}}} */").unwrap();

        entries.push(entry);
    }

    writeln!(output, "}};").unwrap();
    writeln!(output).unwrap();
    writeln!(output, "static const farch_entry_t {}[] = {{", name).unwrap();

    dump_entries(name, &entries, output);
    writeln!(output, "{{   .name = LSTR_NULL }},").unwrap();
    writeln!(output, "}};").unwrap();
}

fn main() {
    let opts = Opts::parse();

    // Handle input file
    let script_path = &opts.script;

    let reldir = script_path.parent().unwrap_or_else(|| {
        eprintln!(
            "Error: unable to get parent directory of script `{}`",
            script_path.display()
        );
        std::process::exit(1);
    });

    let mut output = String::new();

    do_work(&opts, reldir, &mut output);

    // Output to a file
    if let Some(out_path) = &opts.out {
        // Write file
        let mut out_file = File::create(out_path).unwrap_or_else(|e| {
            eprintln!(
                "Error: unable to open `{}` for writing: {}",
                out_path.display(),
                e
            );
            std::process::exit(1);
        });
        out_file.write_all(output.as_bytes()).unwrap_or_else(|e| {
            eprintln!(
                "Error: unable to write file `{}`: {}",
                out_path.display(),
                e
            );
            std::process::exit(1);
        });

        // Set permissions on output
        if let Err(e) = fs::set_permissions(out_path, fs::Permissions::from_mode(0o440)) {
            eprintln!("Error: unable to chmod `{}`: {}", out_path.display(), e);
            std::process::exit(1);
        }
    } else {
        // Output to stdout
        io::stdout()
            .write_all(output.as_bytes())
            .unwrap_or_else(|e| {
                eprintln!("Error: unable to write to stdio: {}", e);
                std::process::exit(1);
            });
    }
}
