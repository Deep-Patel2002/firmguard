#![forbid(unsafe_code)]
use firmguard::{parse, MAX_INPUT};
use std::{
    hint::black_box,
    io::{self, Read},
    process::ExitCode,
    time::Instant,
};

fn read_input(path: &str) -> io::Result<Vec<u8>> {
    let reader: Box<dyn Read> = if path == "-" {
        Box::new(io::stdin())
    } else {
        Box::new(std::fs::File::open(path)?)
    };
    let mut input = Vec::with_capacity(MAX_INPUT + 1);
    reader
        .take((MAX_INPUT + 1) as u64)
        .read_to_end(&mut input)?;
    Ok(input)
}
fn text(bytes: &[u8]) -> String {
    bytes.iter().map(|b| char::from(*b)).collect()
}
fn run() -> Result<bool, String> {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 3 {
        return Err("usage: fg-rust inspect FILE|- | bench FILE ITERATIONS [THREADS]".into());
    }
    let input = read_input(&args[2]).map_err(|_| "input read failed")?;
    if args[1] == "inspect" && args.len() == 3 {
        match parse(&input) {
            Err(e) => {
                println!("{{\"status\":\"{}\",\"code\":{}}}", e.name(), e as i32);
                Ok(false)
            }
            Ok(m) => {
                print!("{{\"status\":\"ok\",\"device\":\"{}\",\"payload_size\":{},\"sequence\":{},\"images\":[", text(m.device), m.payload_size, m.sequence);
                for (i, image) in m.images().iter().enumerate() {
                    if i != 0 {
                        print!(",");
                    }
                    print!(
                        "{{\"path\":\"{}\",\"offset\":{},\"size\":{},\"version\":{},\"sha256\":\"",
                        text(image.path),
                        image.offset,
                        image.size,
                        image.version
                    );
                    for byte in image.sha256 {
                        print!("{byte:02x}");
                    }
                    print!("\"}}");
                }
                println!("]}}");
                Ok(true)
            }
        }
    } else if args[1] == "bench" && (args.len() == 4 || args.len() == 5) {
        let iterations = args[3]
            .parse::<u64>()
            .map_err(|_| "invalid iteration count")?;
        let threads = if args.len() == 5 {
            args[4]
                .parse::<usize>()
                .map_err(|_| "invalid thread count")?
        } else {
            1
        };
        if !(1..=100_000_000).contains(&iterations) || !(1..=32).contains(&threads) {
            return Err("benchmark bounds exceeded".into());
        }
        let start = Instant::now();
        let checksum = std::thread::scope(|scope| {
            let mut workers = Vec::new();
            for _ in 0..threads {
                let data = &input;
                workers.push(scope.spawn(move || {
                    let mut sum = 0_u64;
                    for _ in 0..iterations {
                        let value = match parse(black_box(data)) {
                            Ok(m) => m.sequence.wrapping_add(m.images().len() as u64),
                            Err(e) => e as u64,
                        };
                        sum = sum.wrapping_add(black_box(value));
                    }
                    sum
                }));
            }
            workers
                .into_iter()
                .map(|w| w.join().expect("benchmark worker panicked"))
                .fold(0_u64, u64::wrapping_add)
        });
        println!(
            "{{\"iterations\":{},\"threads\":{},\"ns\":{},\"checksum\":{}}}",
            iterations * threads as u64,
            threads,
            start.elapsed().as_nanos(),
            checksum
        );
        Ok(true)
    } else {
        Err("invalid command or arguments".into())
    }
}
fn main() -> ExitCode {
    match run() {
        Ok(true) => ExitCode::SUCCESS,
        Ok(false) => ExitCode::from(1),
        Err(e) => {
            eprintln!("{e}");
            ExitCode::from(2)
        }
    }
}
