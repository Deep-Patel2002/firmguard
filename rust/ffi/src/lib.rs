//! Test bridge only. Production Rust consumers use the safe core crate.
use firmguard::{parse, Error, MAX_INPUT};

/// # Safety
/// Input must address `size` immutable bytes; output must address `capacity`
/// writable bytes; `written` must address a writable usize. All three regions
/// must be disjoint and remain valid for the call. NULL data is allowed at size 0.
#[no_mangle]
pub unsafe extern "C" fn fg_rust_canonical(
    data: *const u8,
    size: usize,
    output: *mut u8,
    capacity: usize,
    written: *mut usize,
) -> i32 {
    if written.is_null() {
        return Error::Argument as i32;
    }
    // SAFETY: writable, nonaliasing output is an embedding-caller obligation.
    unsafe {
        *written = 0;
    }
    if data.is_null() && size != 0 {
        return Error::Argument as i32;
    }
    if size > MAX_INPUT {
        return Error::Size as i32;
    }
    // SAFETY: validated size is bounded; caller guarantees allocation/lifetime.
    let input = if size == 0 {
        &[]
    } else {
        unsafe { std::slice::from_raw_parts(data, size) }
    };
    match parse(input) {
        Err(error) => error as i32,
        Ok(manifest) => {
            if output.is_null() {
                return Error::Argument as i32;
            }
            let encoded = manifest.canonical();
            if capacity < encoded.len() {
                return Error::Size as i32;
            }
            // SAFETY: caller provides disjoint writable capacity as documented.
            unsafe {
                std::ptr::copy_nonoverlapping(encoded.as_ptr(), output, encoded.len());
                *written = encoded.len();
            }
            0
        }
    }
}
