# Turnip A8xx LRZ/UBWC refactor note

## Current bottlenecks

- LRZ can be invalidated for the rest of a render pass in cases where a draw only needs a temporary LRZ skip, which is expensive on A8xx depth-heavy scenes.
- LRZ per-draw decision paths have no explicit counters, making A8xx tuning difficult and causing regressions to be hard to root-cause.
- UBWC fallback logic is spread across `tu_image_init()`, especially mutable-format handling, which makes A8xx-specific policy tuning harder.

## Risky areas

- LRZ + fragment-shader depth side-effects: aggressive enable can break correctness if direction tracking assumptions are wrong.
- Mutable image UBWC decisions: changing when UBWC is dropped can affect compatibility and linear fallback behavior.
- GMEM/sysmem behavior: LRZ/UBWC changes must not silently increase fallback cost.

## Refactor structure

1. Add centralized A8xx policy helpers used by LRZ and UBWC code.
2. Track LRZ decision counters per render pass (enabled/rejected/invalidation/fast-clear/fallback path) and log summary once per pass.
3. Use A8xx policy to keep FS-driven LRZ skips temporary when hardware direction tracking is available, instead of invalidating LRZ state for the whole pass.
4. Refactor mutable-format UBWC fallback decision into small helper functions with explicit reason strings.

## Expected performance wins

- Fewer full-renderpass LRZ invalidations on A8xx where temporary skip is enough.
- Lower hot-path CPU overhead for LRZ diagnosis and tuning via explicit counters (less manual trace spelunking).
- Cleaner UBWC policy split for mutable-format fallback paths, reducing accidental UBWC disablement in future tuning work.
