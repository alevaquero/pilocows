//! Decodes G.711 A-law WAV clips (as produced by the handheld, see
//! `handheld_crowpanel/main/session_storage.c`) back to plain 16-bit PCM WAV
//! before serving them to the frontend — browser/webview native A-law
//! decoding isn't reliably guaranteed across platforms, so the backend
//! always hands back a universally-supported format. The DB itself still
//! stores the compact A-law bytes as synced from the handheld.

const WAV_HEADER_BYTES: usize = 44;
const WAVE_FORMAT_PCM: u16 = 1;
const WAVE_FORMAT_ALAW: u16 = 6;

/// Standard ITU-T G.711 A-law decode (reference algorithm).
fn alaw_decode_sample(a_val: u8) -> i16 {
    let a_val = a_val ^ 0x55;
    let sign = a_val & 0x80;
    let exponent = (a_val & 0x70) >> 4;
    let mantissa = a_val & 0x0F;
    let mut sample: i16 = ((mantissa as i16) << 4) + 8;
    if exponent != 0 {
        sample += 0x100;
        sample <<= exponent - 1;
    }
    if sign != 0 {
        -sample
    } else {
        sample
    }
}

/// Rewraps a stored WAV clip as plain 16-bit PCM, decoding it first if it's
/// A-law. Clips synced before this feature existed are already PCM and are
/// returned unchanged. Malformed/too-short input is returned as-is — the
/// caller is just serving bytes, not validating them.
pub fn to_playable_pcm_wav(wav: &[u8]) -> Vec<u8> {
    if wav.len() < WAV_HEADER_BYTES {
        return wav.to_vec();
    }

    let audio_format = u16::from_le_bytes([wav[20], wav[21]]);
    if audio_format != WAVE_FORMAT_ALAW {
        // Already PCM (or some other format we don't know how to touch) —
        // pass through unchanged.
        return wav.to_vec();
    }

    let sample_rate = u32::from_le_bytes([wav[24], wav[25], wav[26], wav[27]]);
    let alaw_data = &wav[WAV_HEADER_BYTES..];

    let pcm_bytes: Vec<u8> = alaw_data
        .iter()
        .flat_map(|&b| alaw_decode_sample(b).to_le_bytes())
        .collect();

    let data_size = pcm_bytes.len() as u32;
    let byte_rate = sample_rate * 2;

    let mut out = Vec::with_capacity(WAV_HEADER_BYTES + pcm_bytes.len());
    out.extend_from_slice(b"RIFF");
    out.extend_from_slice(&(36 + data_size).to_le_bytes());
    out.extend_from_slice(b"WAVE");
    out.extend_from_slice(b"fmt ");
    out.extend_from_slice(&16u32.to_le_bytes());
    out.extend_from_slice(&WAVE_FORMAT_PCM.to_le_bytes());
    out.extend_from_slice(&1u16.to_le_bytes()); // mono
    out.extend_from_slice(&sample_rate.to_le_bytes());
    out.extend_from_slice(&byte_rate.to_le_bytes());
    out.extend_from_slice(&2u16.to_le_bytes()); // block_align
    out.extend_from_slice(&16u16.to_le_bytes()); // bits_per_sample
    out.extend_from_slice(b"data");
    out.extend_from_slice(&data_size.to_le_bytes());
    out.extend_from_slice(&pcm_bytes);
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn alaw_decode_matches_known_reference_values() {
        // 0x55 and 0xD5 are G.711 A-law's smallest-magnitude codewords
        // ("positive/negative zero"), universally documented as decoding
        // to +8 / -8 — a real, checkable correctness fact about the
        // algorithm, not just a self-consistency check.
        assert_eq!(alaw_decode_sample(0x55), 8);
        assert_eq!(alaw_decode_sample(0xD5), -8);
    }

    #[test]
    fn to_playable_pcm_wav_passes_through_existing_pcm_unchanged() {
        // A clip synced before this feature existed is already PCM
        // (audio_format=1) and must come back byte-for-byte identical.
        let mut wav = vec![0u8; 44 + 4];
        wav[0..4].copy_from_slice(b"RIFF");
        wav[8..12].copy_from_slice(b"WAVE");
        wav[20..22].copy_from_slice(&1u16.to_le_bytes()); // WAVE_FORMAT_PCM
        wav[44..48].copy_from_slice(&[1, 2, 3, 4]);
        assert_eq!(to_playable_pcm_wav(&wav), wav);
    }

    #[test]
    fn to_playable_pcm_wav_decodes_alaw_to_correct_length_and_rate() {
        let mut wav = vec![0u8; 44 + 3];
        wav[20..22].copy_from_slice(&6u16.to_le_bytes()); // WAVE_FORMAT_ALAW
        wav[24..28].copy_from_slice(&8000u32.to_le_bytes());
        wav[44] = 0x55;
        wav[45] = 0xD5;
        wav[46] = 0x55;

        let pcm = to_playable_pcm_wav(&wav);
        assert_eq!(u16::from_le_bytes([pcm[20], pcm[21]]), 1); // now PCM
        assert_eq!(u32::from_le_bytes([pcm[24], pcm[25], pcm[26], pcm[27]]), 8000);
        assert_eq!(u16::from_le_bytes([pcm[34], pcm[35]]), 16); // bits_per_sample
        // 3 A-law bytes -> 3 i16 samples -> 6 PCM bytes after the header
        assert_eq!(pcm.len(), 44 + 6);
        assert_eq!(i16::from_le_bytes([pcm[44], pcm[45]]), 8);
        assert_eq!(i16::from_le_bytes([pcm[46], pcm[47]]), -8);
    }
}
