# A8xx enablement-focused deep code analysis (Freedreno/Turnip/IR3)

## A. Executive summary

- Ringbuffer growth is present but still seeded with legacy constants (`INIT_SIZE=0x1000` in drm/msm and many fixed initial `tu_cs_init(..., 4096/2048)` in Turnip cmd streams), which risks repeated BO churn for modern short-dispatch compute workloads. A small generation-aware sizing policy is likely high ROI. 
- Gallium batch creation already opportunistically enables growable rings when kernel supports unlimited command buffers, but still carries fixed nominal sizes (`0x100000`) in several subpass/ring paths; these should be policy-driven by workload class and gen.
- In drm/msm suballoc path, stateobj suballocation currently hardcodes `0x8000` BO allocation with a TODO comment; this is an obvious knob for adaptive allocation tuned by observed command density.
- Turnip `tu_cs` doubling behavior is straightforward and robust, but it has no upper-level feedback loop from per-queue history (dispatch-heavy vs render-heavy), so hot workloads can pay avoidable realloc/submit BO pressure.
- Synchronization and flush logic in Turnip is feature-rich and conservative in known hazard paths (CCU/UBWC quirk handling, renderpass barrier constraints), but compute-heavy paths would benefit from tighter accounting of “must flush” versus “currently flushed” to reduce fence/flush churn.
- Query paths in Gallium retain per-batch sample caches and period structures effectively, yet query-heavy micro-workloads still pay frequent sample-period slab operations and query buffer growth/reset behavior that could be amortized better.
- IR3 variant lookup is currently linear linked-list scan under lock; this is low complexity but becomes non-trivial with variant explosion. A hash-indexed fast path would cut CPU cost in shader-heavy scenes without destabilizing codegen.
- IR3 disk-cache keying is solid (serialized NIR + options + stream output), but A8xx-specific toggles likely need explicit key bits early to avoid cache aliasing during bring-up.
- Gen layering is mixed: Turnip has explicit entrypoint table split for a6xx/a7xx/a8xx, but most heavy logic is still shared+templated CHIP checks. This is good for incremental bring-up, but requires clear boundaries to avoid CHIP>=A8XX condition sprawl.
- Register XML side is already partly prepared with `a8xx_enums.xml` and `a8xx_descriptors.xml`, but no equivalent standalone a8xx core register XML like `a6xx.xml`; this implies bring-up should emphasize descriptor/packet deltas first and postpone broad register factoring until required by real HW differences.
- LRZ/UBWC paths already encode gen quirks and include early A8xx policy hooks (`tu_a8xx_policy.h`), which is the correct direction: central policy predicates plus shared machinery.
- The highest-value near-term work is not architectural rewrite; it is adaptive command-stream sizing, variant/cache pressure reduction, and narrowly scoped A8xx policy/quirk injection points.

## B. Codebase map for A8xx work

- `src/freedreno/vulkan/tu_cmd_buffer.cc`: core graphics/compute command emission, GMEM/sysmem decisions, barriers/flushes, and many CHIP-templated packets.
- `src/freedreno/vulkan/tu_cs.cc` + `tu_cs.h`: Turnip command stream allocator, BO growth, IB segmentation; key for submission overhead and short-dispatch efficiency.
- `src/freedreno/vulkan/tu_queue.cc`: queue submit composition, patchpoint handling, synchronization/fence progression; central to CPU submit overhead.
- `src/freedreno/vulkan/tu_device.cc`: device init, per-gen entrypoint wiring (`a6xx/a7xx/a8xx`), debug/perf toggles, global suballocators.
- `src/freedreno/vulkan/tu_lrz.cc` + `tu_a8xx_policy.h`: LRZ behavior and early A8xx-specific policy knobs.
- `src/gallium/drivers/freedreno/freedreno_batch.c`: batch lifetime, dependency flush ordering, ring allocations for draw/gmem/binning paths.
- `src/gallium/drivers/freedreno/freedreno_gmem.c` and `a6xx/fd6_gmem.cc`: GMEM/binning/sysmem emission and bandwidth-critical tile behavior.
- `src/gallium/drivers/freedreno/freedreno_query_hw.c`: HW query/sample lifetime and query result waits (important CPU overhead in query-heavy apps).
- `src/freedreno/drm/msm/msm_ringbuffer.c` and `src/freedreno/drm/freedreno_ringbuffer_sp.c`: low-level ring growth/suballoc mechanics impacting both Gallium and Vulkan stacks.
- `src/freedreno/ir3/ir3_shader.c`, `ir3_disk_cache.c`, `ir3_nir.c`, `ir3_lower_subgroups.c`, `ir3_compiler.c`: variant creation/cache, NIR lowering, subgroup lowering, wave/threadsize policy.
- `src/freedreno/registers/adreno/*.xml` + `meson.build`: register/packet schema generation; A8xx descriptor and enum groundwork.

## C. Top performance opportunities

### High impact

1. **Adaptive command-stream initial sizing and growth policy**
   - Subsystem: submission/ringbuffer
   - Affected files: `tu_cmd_buffer.cc`, `tu_cs.cc`, `msm_ringbuffer.c`, `freedreno_batch.c`
   - Current problem: Fixed initial sizes (`4096/2048`, `INIT_SIZE=0x1000`, `0x8000` suballoc fallback) force reactive growth under modern mixed workloads.
   - A8xx rationale: Expected higher dispatch density and async workloads increase short-burst command traffic.
   - Expected benefit: lower CPU realloc overhead, fewer BOs per submit, reduced kernel submit object pressure.
   - Difficulty: Medium.
   - Regression risk: Low/Medium if guarded with telemetry and conservative defaults.

2. **IR3 variant lookup/indexing to reduce CPU overhead under variant pressure**
   - Subsystem: compiler/frontend runtime
   - Affected files: `ir3_shader.c`
   - Current problem: variant lookup is O(n) linked-list scan under lock.
   - A8xx rationale: new feature bits and subgroup/wave policy likely increase variant cardinality during enablement.
   - Expected benefit: reduced CPU frame stutter and pipeline bind overhead in shader-rich scenes.
   - Difficulty: Medium.
   - Regression risk: Low if hash key uses existing `ir3_shader_key_equal` semantics.

3. **Flush-bit precision tightening for compute-heavy barrier paths**
   - Subsystem: Turnip synchronization/cache control
   - Affected files: `tu_cmd_buffer.cc`
   - Current problem: conservative flush propagation can over-serialize pipelines in repeated short dispatch/barrier chains.
   - A8xx rationale: compute/AI workflows are barrier-dense and sensitive to WFI/WAIT overuse.
   - Expected benefit: improved dispatch throughput and lower queue latency.
   - Difficulty: Medium/High (needs strong correctness validation).
   - Regression risk: Medium.

### Medium impact

4. **Query/sample allocation amortization in Gallium**
   - Subsystem: queries
   - Affected files: `freedreno_query_hw.c`, potentially `freedreno_batch.c`
   - Current problem: frequent slab period allocate/free and per-batch sample state churn in query-heavy workloads.
   - A8xx rationale: profiling and telemetry-heavy apps will stress query begin/end paths.
   - Expected benefit: lower CPU overhead for query-heavy traces.
   - Difficulty: Medium.
   - Regression risk: Low.

5. **Generation-aware suballocator policy for small BO classes**
   - Subsystem: BO management
   - Affected files: `tu_suballoc.cc`, `tu_device.cc`
   - Current problem: single cached_bo strategy is simple but can underperform when working set oscillates around default size.
   - A8xx rationale: higher descriptor/update throughput can amplify allocator churn.
   - Expected benefit: fewer BO alloc/free cycles, lower submit validation cost.
   - Difficulty: Medium.
   - Regression risk: Low/Medium.

### Low impact (but worthwhile)

6. **Centralize A8xx policy predicates to avoid CHIP-check scatter**
   - Subsystem: gen separation/maintainability
   - Affected files: `tu_a8xx_policy.h`, call sites in `tu_lrz.cc`, `tu_image.cc`, `tu_cmd_buffer.cc`
   - Current problem: direct CHIP gates can sprawl as bring-up progresses.
   - A8xx rationale: avoid copy-paste divergence and late-stage bug farms.
   - Expected benefit: faster review and safer incremental quirks.
   - Difficulty: Low.
   - Regression risk: Low.

## D. Refactoring recommendations worth doing

1. **Introduce a tiny “cmdstream sizing policy” helper shared by Turnip and drm/msm entry points.**
   Worth churn because it directly targets hot-path allocation churn and can be rolled out incrementally behind defaults.

2. **Replace `ir3_shader` variant linked-list search with optional hash table accelerator (keeping list for iteration order/debug).**
   Worth churn because compile/lookup path is hot in real apps and this reduces lock hold time without touching codegen.

3. **Split flush-bit derivation from emission in `tu_cmd_buffer.cc` into a compact, testable helper.**
   Worth churn because correctness and performance both depend on subtle stage/access interactions; this reduces accidental overflush as A8xx quirks are added.

4. **Create explicit A8xx policy module expansion (beyond current LRZ/UBWC predicates).**
   Worth churn because it prevents CHIP>=8 branches from contaminating high-volume packet emission code.

## E. Refactors NOT worth doing

- Broad C/C++ style normalization across freedreno/turnip files (no measurable perf/correctness value).
- Massive “new a8xx driver fork” now; too risky and duplicates mature a6xx/a7xx paths before real hardware deltas are known.
- Large-scale file moves/renames of gmem/lrz logic before bring-up data identifies stable A8xx-only boundaries.
- Premature abstraction of every packet write helper into extra layers; likely harms readability/perf-tuning agility in hot emit paths.

## F. A8xx bring-up plan

1. **Minimal boot / basic submission**
   - Reuse a7xx shared paths with a8xx entrypoint table already wired.
   - Validate command submission, fence progression, minimal draw/dispatch.
2. **Shader/compiler enablement**
   - Enable conservative IR3 options for A8xx; verify subgroup/threadsize defaults and disk-cache key separation for A8xx toggles.
   - Add minimal A8xx-specific lowering only when proven needed.
3. **Render path correctness**
   - Bring up GMEM/sysmem transitions, UBWC, LRZ with conservative policy defaults.
   - Keep fallback paths but measure frequency.
4. **Turnip Vulkan readiness**
   - Validate renderpass/dynamic rendering/barrier interactions, query pools, and visibility stream patchpoint behavior under simultaneous-use.
5. **Performance tuning**
   - Tune cmdstream sizing, reduce overflush, improve variant/cache hit behavior, and tune BO suballocation defaults for dispatch-heavy and mixed workloads.

Shared-vs-new decision: keep **mostly shared with selective A8xx overrides** as default; only carve new A8xx files for subsystems with clearly divergent register/programming models.

## G. File-level action list (incremental patch slices)

1. `tu_cs.cc` / `tu_cmd_buffer.cc`
   - Add policy helper: `tu_cs_initial_size_for_stream(kind, queue_caps, chip)`.
   - Replace hardcoded initial sizes in `tu_create_cmd_buffer` stream init calls.

2. `msm_ringbuffer.c`
   - Replace static `INIT_SIZE` and `0x8000` stateobj fallback with policy-based values; add counters for grow events (debug/perf only).

3. `freedreno_batch.c`
   - Convert fixed `0x100000` ring seed sizes to policy calls keyed by batch type (draw/gmem/binning/nondraw).

4. `ir3_shader.c`
   - Add hash map index from `ir3_shader_key` to variant pointer; keep linked list for iteration/disasm.

5. `ir3_disk_cache.c`
   - Audit/extend variant key serialization when adding A8xx-specific compiler toggles (avoid accidental key aliasing).

6. `tu_cmd_buffer.cc`
   - Isolate `flush_bits` derivation into helper; add per-submit counters for flush categories (debug builds/perf tracepoints).

7. `tu_a8xx_policy.h` + consumers (`tu_lrz.cc`, `tu_image.cc`, `tu_cmd_buffer.cc`)
   - Expand policy predicates (UBWC mutable behavior, LRZ fallback thresholds, possible align constraints) to keep callsites clean.

8. `freedreno_query_hw.c`
   - Add optional freelist for `fd_hw_sample_period` objects per-query or per-context to reduce slab churn.

9. `src/freedreno/registers/adreno/*.xml`
   - Keep A8xx descriptor/enums evolution incremental; only introduce new XML domain files when proven by packet/register deltas.

## H. Benchmark and validation plan

### Bring-up correctness
- piglit/deqp smoke for draw/compute/query synchronization.
- Vulkan CTS subsets: synchronization2, dynamic rendering, pipeline barriers, query pools, subgroup behavior.
- Validate GMEM/sysmem switching correctness with renderpass-heavy traces.

### Optimization validation
- **Command submission overhead**: submits/sec, CPU time in queue submit, BO count per submit, cmd BOs per submit.
- **Shader compile time**: per-stage compile latency, variant creation rate, lock contention in variant lookup.
- **Shader cache behavior**: disk cache hit/miss by stage/key class; variant count distribution.
- **Bandwidth / resolves / GMEM**: GMEM hit rate, sysmem fallback frequency, resolves per frame, UBWC/LRZ enable rates.
- **Fence/flush latency**: average wait time, WFI/WAIT_MEM_WRITES event counts, queue idle gaps.
- **Graphics vs compute mix**: short-dispatch microbench + mixed render/compute scene traces.

## I. Final recommendation

- **Change now**: adaptive cmdstream/ring sizing policy, IR3 variant lookup acceleration, and A8xx policy centralization hooks.
- **Defer**: deeper flush optimization and BO suballocator multi-cache classes until baseline A8xx correctness is stable.
- **Do not touch now**: broad stylistic refactors, major file reshuffles, or full a8xx-only forks of shared a6xx/a7xx code.

## Explicit uncertainty

- Exact A8xx cache/compression and alignment constraints are not fully known from this tree alone; recommendations above intentionally favor reversible, policy-driven changes over hardcoded behavioral divergence.
