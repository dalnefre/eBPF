// general purpose utilities

/*
 * Modular increment
 * where `i` in [0, `m`).
 * Returns (`i`+1) % `m`.
 */
pub fn mod_inc(i: usize, m: usize) -> usize {
    //mod_add(i, 1, m)
    let mut n = i + 1;
    if n >= m {
        n = 0;
    };
    n
}

/*
 * Modular addition
 * where `i` and `j` in [0, `m`).
 * Returns (`i`+`j`) % `m`.
 */
pub fn mod_add(i: usize, j: usize, m: usize) -> usize {
    let mut n = i + j;
    while n >= m {
        n -= m;
    };
    n
}
