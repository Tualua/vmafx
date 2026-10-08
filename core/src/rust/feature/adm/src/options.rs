// Copyright 2026 Lusoris
// SPDX-License-Identifier: EUPL-1.2
//
// Options of the integer ADM twin. The C option table of `adm`
// (core/src/feature/integer_adm.c) owns names, aliases, defaults and ranges;
// the shim hands over the parsed values and this reads every one of them.

use vmafx_fex::{Error, FromOptions, OptionValues};

use crate::csf_weights::CsfConfig;

/// Every option of the C `adm` table; none is refused.
#[derive(Clone, Copy, Debug)]
pub struct AdmOptions {
    pub debug: bool,
    pub csf_scale: f64,
    pub csf_diag_scale: f64,
    pub dlm_weight: f64,
    pub enhn_gain_limit: f64,
    pub norm_view_dist: f64,
    pub ref_display_height: i32,
    pub csf_mode: i32,
    pub noise_weight: f64,
    pub skip_aim: bool,
    pub skip_scale0: bool,
    pub min_val: f64,
    pub p_norm: f64,
}

impl AdmOptions {
    /// The options the CSF weights depend on.
    pub const fn csf_config(&self) -> CsfConfig {
        CsfConfig {
            norm_view_dist: self.norm_view_dist,
            ref_display_height: self.ref_display_height,
            csf_mode: self.csf_mode,
            csf_scale: self.csf_scale,
            csf_diag_scale: self.csf_diag_scale,
        }
    }
}

impl FromOptions for AdmOptions {
    fn from_options(o: &OptionValues<'_>) -> Result<Self, Error> {
        Ok(Self {
            debug: o.bool(c"debug")?,
            csf_scale: o.f64(c"adm_csf_scale")?,
            csf_diag_scale: o.f64(c"adm_csf_diag_scale")?,
            dlm_weight: o.f64(c"adm_dlm_weight")?,
            enhn_gain_limit: o.f64(c"adm_enhn_gain_limit")?,
            norm_view_dist: o.f64(c"adm_norm_view_dist")?,
            ref_display_height: o.int(c"adm_ref_display_height")?,
            csf_mode: o.int(c"adm_csf_mode")?,
            noise_weight: o.f64(c"adm_noise_weight")?,
            skip_aim: o.bool(c"adm_skip_aim")?,
            skip_scale0: o.bool(c"adm_skip_scale0")?,
            min_val: o.f64(c"adm_min_val")?,
            p_norm: o.f64(c"adm_p_norm")?,
        })
    }
}
