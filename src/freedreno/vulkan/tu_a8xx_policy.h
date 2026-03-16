/* SPDX-License-Identifier: MIT */

#ifndef TU_A8XX_POLICY_H
#define TU_A8XX_POLICY_H

#include "common/freedreno_dev_info.h"

static inline bool
tu_a8xx_relaxed_fs_lrz_disable(const struct fd_dev_info *info)
{
   /*
    * A8xx has reliable GPU-side LRZ direction tracking, so FS-side effects can
    * be handled with a temporary LRZ disable in more cases instead of
    * invalidating LRZ for the remainder of the render pass.
    */
   return info->chip >= 8 && info->props.has_lrz_dir_tracking;
}

static inline bool
tu_a8xx_mutable_ubwc_policy(const struct fd_dev_info *info)
{
   /*
    * Keep mutable-format UBWC decisions centralized so per-chip quirks can be
    * added without scattering chip checks in tu_image.cc.
    */
   return info->chip >= 8;
}

static inline bool
tu_a8xx_lrz_has_slice_pitch(const struct fd_dev_info *info)
{
   return info->chip >= 8;
}

static inline bool
tu_lrz_dir_tracking_supported(const struct fd_dev_info *info)
{
   return info->props.has_lrz_dir_tracking;
}

static inline bool
tu_a8xx_cp_set_marker_uses_gmem_bit(const struct fd_dev_info *info)
{
   return info->chip >= 8;
}

static inline bool
tu_a8xx_sysmem_autotune_default(const struct fd_dev_info *info)
{
   /* Keep A8xx autotune conservative by default to reduce mode-flip jitter. */
   return info->chip >= 8;
}

static inline bool
tu_a8xx_mutable_ubwc_requires_format_list_check(const struct fd_dev_info *info)
{
   return info->chip >= 8;
}

#endif /* TU_A8XX_POLICY_H */
