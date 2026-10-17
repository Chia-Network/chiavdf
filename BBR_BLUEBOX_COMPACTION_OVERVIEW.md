# BBR / Chia Bluebox VDF Proof Compaction — Overview + Implemented Performance Tricks

This document summarizes how Chia “bluebox” compaction jobs are computed in this repo, and the performance tweaks we implemented in `bbr_chiavdf/` (a fork/copy of `chiavdf/`).

It is written to be understandable even if you’re not already fluent with Chia’s VDF / classgroup implementation details.

## 1) What “bluebox compaction” computes

Chia blocks include a VDF output (“VDFInfo”) for some VDF “slot” (end-of-slot, signage point, infusion point, etc.). A full node can accept a **compact proof of time** (“VDFProof”) for a given VDFInfo:

- Input element `x` (for compaction jobs this is always the **default/canonical classgroup element**, i.e. identity-ish input used by Chia)
- Discriminant `D` derived from the VDF challenge
- Number of iterations `T`
- Output element `y` (already known from the block’s VDFInfo)

The bluebox worker’s job is to compute the **compact Wesolowski witness** `π` (witness_type = 0) such that the proof verifies for `(D, x, y, T)`.

### Inputs (per proof job)

All values are byte strings unless noted otherwise.

- `challenge`: 32 bytes (`VDFInfo.challenge`)
- `T`: `u64` (`VDFInfo.number_of_iterations`)
- `y_ref`: 100 bytes serialized classgroup element (`VDFInfo.output`)
- `size_bits`: discriminant size in bits (typically 1024; Chia consensus constant)
- `x0`: canonical input element for compaction:
  - `x0_bytes = ClassgroupElement.get_default_element().data` (100 bytes)

### Outputs (per proof job)

- `y`: serialized output element (should equal `y_ref`)
- `proof`: serialized witness element `π` (same size as `y`)
- In our C ABI wrappers we return `y || proof` (concatenation, typically 200 bytes for 1024-bit discriminants).

## 2) Underlying primitives (high-level)

Chia’s VDF uses the class group of binary quadratic forms for a negative discriminant `D`:

- `D` is derived deterministically from `(challenge, size_bits)` via `CreateDiscriminant(...)`.
- Elements are represented as reduced forms `form { a, b, c }` (each is a GMP big integer).
- The VDF evaluation is the deterministic repeated squaring chain:
  - `f(0) = x0`
  - `f(t+1) = square(f(t))` (with reduction)
  - `y = f(T) = x0^(2^T)` in the class group

The compact proof is a Wesolowski proof, which (in this implementation) uses a per-proof prime `B` derived from the input and output:

- `B = GetB(D, x0_form, y_ref_form)` where `GetB` hashes serialized forms then runs `HashPrime(...)`.
- Because `B` depends on `y_ref`, if `y_ref` is known up front then **`B` is known before squaring starts**.

## 3) Baseline chiavdf “one-weso” compaction (two-phase)

The upstream chiavdf compact witness path (“one-weso”) is:

1. **Squaring phase**
   - Run the VDF evaluation (sequential squaring) from `x0` to iteration `T`.
   - Store many intermediate forms (“checkpoints”) in an array at a fixed cadence.

2. **Proof phase**
   - After squaring is finished, scan those stored checkpoints.
   - Multiply them into “buckets” `ys[j][b]` using a mapping (`GetBlock`) that depends on `B`.
   - Fold the bucket structure into a final `proof_form`.

### Proof parameters `k, l, kl`

chiavdf computes parameters:

- `(k, l) = ApproximateParameters(T)`
- `kl = k * l`
- Number of checkpoint indices:
  - `limit = ceil(T / kl)` (the number of checkpoint positions that may be used)

These parameters control how many checkpoints are used and how bucket folding is structured.

### Costs (baseline)

- Memory: stores `O(ceil(T/kl))` checkpoint forms (each form holds several GMP big integers).
- Time: wall-clock is essentially `t_total = t_square + t_proof` because proof work happens after squaring.

## 4) Tweak / Trick 1 — “Streaming one-weso” using known output (`y_ref`)

### Key idea

For bluebox compaction, `y_ref` is already known from the block. Because `B` depends on `y_ref`, we can compute `B` before starting squaring.

That lets us avoid storing checkpoint forms and instead update the proof buckets **as soon as each checkpoint is reached**, using the current `f(t)` value.

### Algorithm (single job, streaming buckets)

Inputs: `(challenge, size_bits, x0_bytes, y_ref_bytes, T)`

1. Compute `D = CreateDiscriminant(challenge, size_bits)` and `L = root(-D, 4)` (chiavdf convention).
2. Deserialize:
   - `x0_form = DeserializeForm(D, x0_bytes)`
   - `y_ref_form = DeserializeForm(D, y_ref_bytes)`
3. Compute:
   - `B = GetB(D, x0_form, y_ref_form)`
   - `(k, l) = ApproximateParameters(T)` (fallback `k=10,l=1` for small `T`)
   - `kl = k*l`
   - `limit = ceil(T/kl)`
4. Allocate buckets:
   - `ys[j][b]` for `j ∈ [0, l)` and `b ∈ [0, 2^k)`
   - Initialize all buckets to the identity form.
5. Run the VDF squaring chain up to `T`, but:
   - At each checkpoint time `t = i*kl`, compute `checkpoint = f(t)` and call `process_checkpoint(i, checkpoint)`:
     - For each `j ∈ [0, l)`:
       - `p = i*l + j`
       - If `T >= k*(p+1)`, compute `b = GetBlock(p, k, T, B)`
       - Multiply `ys[j][b] *= checkpoint` (via `nucomp_form`).
6. At the end, compute `y = f(T)` and check `y == y_ref_form` (debug/safety guard).
7. Fold buckets to compute the final proof form (same folding logic as chiavdf).
8. Serialize `y` and `proof` and return `y || proof`.

### What changed vs baseline

- We no longer store an array of checkpoint forms.
- Bucket multiplication occurs “online” during squaring.
- Folding/finalization stays the same as chiavdf.

### Costs / tradeoffs

- Memory becomes `O(l * 2^k)` forms (the bucket table) instead of `O(ceil(T/kl))` checkpoint forms.
- Runtime can sometimes overlap bucket updates with squaring, but in practice the speedup depends on which part dominates (squaring vs `nucomp_form` multiplications).

### Where this lives in `bbr_chiavdf/`

- C ABI entrypoints:
  - `chiavdf_prove_one_weso_fast_streaming(...)`
  - `chiavdf_prove_one_weso_fast_streaming_with_progress(...)`
- Implementation:
  - `bbr_chiavdf/src/c_bindings/fast_wrapper.cpp`
  - `StreamingOneWesolowskiCallback` and the bucket helper (`StreamingWesolowskiBuckets`).

## 5) GetBlock optimization (precompute `GetBlock(p)` table per job)

In streaming (and in baseline), for each checkpoint update we need:

- `b = GetBlock(p, k, T, B)`

Naively this uses per-`p` modular exponentiation and division, which is expensive with GMP big integers.

### Optimization idea

For fixed `(T, k, B)`, define:

- `r_p = 2^{T - k*(p+1)} mod B`
- `b_p = floor((r_p * 2^k) / B)` (integer division)

Then:

- `r_{p+1} = r_p * inv(2^k) mod B` where `inv(2^k)` is the modular inverse of `2^k mod B`

So we can compute all `b_p` iteratively in `O(#p)` time with one modular inverse, instead of `O(#p)` modular exponentiations.

### Tradeoff

We store `precomputed_blocks[p]` for all `p` used by the proof:

- Memory: `O(limit * l)` `u32` values per job.
  - For typical compaction-scale `T` this is often a few MB per job.

### Where this lives

- `bbr_chiavdf/src/c_bindings/fast_wrapper.cpp`:
  - `build_precomputed_getblocks(...)`
  - Used by:
    - `chiavdf_prove_one_weso_fast_streaming_getblock_opt(...)`
    - `chiavdf_prove_one_weso_fast_streaming_getblock_opt_with_progress(...)`

## 6) Trick 2 — discriminant reuse (“multi-target VDF engine”)

### Key observation

For a fixed group key `(challenge, size_bits, x0_bytes)`, the discriminant `D` and the entire squaring trajectory `f(t)` are identical for all jobs:

- Only `T_j` and `y_ref_j` differ across jobs.

Therefore, if you have `N` jobs sharing a group key:

- Without reuse: total squaring work is `Σ T_j`
- With reuse: total squaring work is exactly `T_max = max(T_j)`

### Grouping key

Jobs can be grouped if and only if:

- Same `challenge`
- Same `size_bits`
- Same `x0_bytes`

For bluebox compaction, `x0_bytes` is always the default element, so grouping is mostly “same challenge”.

### Algorithm (batch)

Inputs (shared):

- `challenge`, `x0_bytes`, `size_bits`

Inputs (per job `j`):

- `T_j`, `y_ref_j`

Per job setup (done before squaring starts):

1. Deserialize `y_ref_form_j`
2. Compute `B_j = GetB(D, x0_form, y_ref_form_j)`
3. Compute `(k_j, l_j)`, `kl_j`, `limit_j`
4. Allocate `ys_j` buckets (Trick 1)
5. Precompute `GetBlock` table for that job (GetBlock opt)

Shared squaring run:

- Run `repeated_square(T_max, ...)` once to generate `f(t)` for all times up to `T_max`.
- Maintain per job:
  - `next_checkpoint_t_j` initialized to `kl_j` (we process `i=0` immediately at `t=0`)
  - completion time `T_j`
- At each “event time” `t`:
  1. For every job where `t == next_checkpoint_t_j`:
     - `i = t / kl_j`
     - `ys_j` bucket update with checkpoint form `f(t)` (Trick 1)
     - `next_checkpoint_t_j += kl_j`
  2. For every job where `t == T_j`:
     - Debug check: `f(T_j) == y_ref_form_j`
       - If mismatch: abort (signals backend grouping/data bug).
     - Finalize proof for that job (fold buckets → proof form) and serialize result.
     - Free job state (buckets, GetBlock table) to reduce peak RAM.

### Concurrency / offloading finalization

- The shared squaring chain itself is sequential by definition.
- Bucket updates are triggered by exact `f(t)` values; in our implementation they are done on the squaring callback thread to avoid copying forms or storing a large checkpoint history.
- Finalization (folding + serialization) is **per job** and can be offloaded:
  - Once a job reaches `T_j` and passes the `f(T_j)==y_ref_j` check, its proof no longer depends on future squaring.
  - We offload finalization to a `std::thread` per completed job so the squaring run can continue toward larger `T`.

### Where this lives

- New C ABI:
  - `bbr_chiavdf/src/c_bindings/fast_wrapper.h`:
    - `ChiavdfBatchJob`
    - `chiavdf_prove_one_weso_fast_streaming_getblock_opt_batch(...)`
    - `chiavdf_free_byte_array_batch(...)`
- Implementation:
  - `bbr_chiavdf/src/c_bindings/fast_wrapper.cpp`:
    - `BatchOneWesolowskiCallback`
    - `BatchJobState`
    - Uses `StreamingWesolowskiBuckets` per job

### Error policy (mismatch)

We keep a strict mismatch check specifically for debugging backend grouping / job data issues:

- If the computed checkpoint `f(T_j)` differs from `y_ref_form_j`, the batch function returns `NULL` (fatal error).

This is expected to be “should never happen” in normal operation, but is useful to detect wrong grouping inputs early.

## 7) Rough resource model (what consumes time and RAM)

### Time

Three main contributors:

1. **Squaring chain** (`repeated_square(...)`): inherently sequential per group.
2. **Bucket updates**: `nucomp_form` multiplications at checkpoint times; scales with number of jobs and number of checkpoints.
3. **Finalization**: folding buckets into a proof; per job.

Trick 2 reduces (1) across jobs by reusing squaring work.

### Memory (per job, within a group)

Dominant memory terms:

- Buckets: `l * 2^k` forms (each form holds multiple GMP big ints) — often several MB per job.
- GetBlock precompute: `limit * l` `u32` values — often a few MB per job.

Peak memory per group is roughly linear in the number of jobs active at the same time (and drops as jobs complete and are freed).

## 8) Things to look at next (possible improvement areas)

This section is intentionally a “menu” for further investigation.

1. **Hotspots inside classgroup arithmetic**
   - If perf shows most time in `nucomp_form` / GMP, then:
     - reduce allocations (GMP mpz churn) with pooling or reuse
     - explore alternative big-int backends / tuned GMP build / CPU-specific flags
     - reduce constant factors in `nucomp_form` (algorithmic / assembly improvements)

2. **Reduce per-iteration callback overhead**
   - Today `OnIteration` is called for every iteration, even though we only act on sparse “event times”.
   - If this overhead becomes visible at huge `T`, consider:
     - extending the core loop to support “next event” iteration skipping (intrusive change)
     - or internal batching in the callback path

3. **Finalization optimization**
   - Each job finalization constructs a reducer and folds buckets.
   - Potential wins:
     - reuse reducers per thread
     - reduce intermediate `form` temporaries and copies

4. **Group sizing / scheduling**
   - For Trick 2, there’s a throughput vs RAM tradeoff.
   - Consider dynamic group size based on memory budget and `T` distribution.

5. **Optional: parallelize bucket updates (hard)**
   - Bucket updates need the checkpoint form `f(t)` at exact times.
   - Parallelizing this without copying/storing forms requires careful design (e.g. immutable snapshots, reference counting, or storing a checkpoint history).
   - This is the next “big step” if per-job proof work becomes the bottleneck even after squaring reuse.

