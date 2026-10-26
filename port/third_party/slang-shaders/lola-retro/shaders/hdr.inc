// Lola Retro - settings for HDR output, used only by the presets in the hdr folder.
// Linear light 1.0 (a full white field) is shown at the paper white level. Phosphor dots
// and scanline centres are brighter than that average and use the headroom up to the peak.
#pragma parameter LR_HDR_OUT "--- HDR output ---" 0.0 0.0 0.0 1.0
#pragma parameter LR_NITS_WHITE "HDR: paper white (nits)" 200.0 80.0 600.0 10.0
#pragma parameter LR_NITS_PEAK "HDR: display peak (nits)" 400.0 200.0 2000.0 50.0
