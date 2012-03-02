#! /bin/sh

src="/home/andrei/Documents/WebRTC/src/"

test "X$1" != "X" && src="$1"

(
baseorig=""
basedest=""
while IFS='	' read orig dest; do
    echo $orig '->' $dest
    case "X$orig" in
	*/)
	    baseorig="$orig"
	    basedest="$dest"
	    continue
	    ;;
	X)
	    ;;
    esac
    test -z "$dest" && dest="$orig"
    orig="$baseorig$orig"
    dest="$basedest$dest"
    if [ -f "$src/$orig" ]; then
	base=`dirname "$dest"`
	mkdir -p "$base"
	cp -a "$src/$orig" "$dest" || echo "Failed to copy: $orig" >&2
    else
	echo "Missing: $orig" >&2
    fi
done
)<<EOF
common_types.h
typedefs.h
modules/interface/	
module.h
module_common_types.h
common_audio/resampler/	audio/common/resampler/
resampler.cc
common_audio/resampler/include/	audio/common/resampler/
resampler.h
common_audio/signal_processing/	audio/common/processing/
auto_correlation.c
auto_corr_to_refl_coef.c
complex_bit_reverse.c
complex_fft.c
copy_set_operations.c
cross_correlation.c
division_operations.c
dot_product_with_scale.c
downsample_fast.c
energy.c
filter_ar.c
filter_ar_fast_q12.c
filter_ma_fast_q12.c
get_hanning_window.c
get_scaling_square.c
ilbc_specific_functions.c
levinson_durbin.c
lpc_to_refl_coef.c
min_max_operations.c
min_max_operations_neon.c
randomization_functions.c
refl_coef_to_lpc.c
resample.c
resample_48khz.c
resample_by_2.c
resample_by_2_internal.c
resample_by_2_internal.h
resample_fractional.c
splitting_filter.c
spl_sqrt.c
spl_sqrt_floor.c
spl_version.c
sqrt_of_one_minus_x_squared.c
vector_scaling_operations.c
webrtc_fft_t_1024_8.c
webrtc_fft_t_rad.c
common_audio/signal_processing/include/	audio/common/processing/
signal_processing_library.h
spl_inl.h
spl_inl_armv7.h
common_audio/vad/	audio/common/vad/
vad_core.c
vad_core.h
vad_defines.h
vad_filterbank.c
vad_filterbank.h
vad_gmm.c
vad_gmm.h
vad_sp.c
vad_sp.h
webrtc_vad.c
common_audio/vad/include/	audio/common/vad/
webrtc_vad.h
modules/audio_coding/codecs/iSAC/fix/source/	audio/coding_isac/fix/
arith_routines.c
arith_routines_hist.c
arith_routines_logist.c
arith_routins.h
bandwidth_estimator.c
bandwidth_estimator.h
codec.h
decode.c
decode_bwe.c
decode_plc.c
encode.c
entropy_coding.c
entropy_coding.h
fft.c
fft.h
filterbanks.c
filterbank_tables.c
filterbank_tables.h
filters.c
filters_neon.c
initialize.c
isacfix.c
lattice.c
lattice_c.c
lpc_masking_model.c
lpc_masking_model.h
lpc_tables.c
lpc_tables.h
pitch_estimator.c
pitch_estimator.h
pitch_filter.c
pitch_gain_tables.c
pitch_gain_tables.h
pitch_lag_tables.c
pitch_lag_tables.h
settings.h
spectrum_ar_model_tables.c
spectrum_ar_model_tables.h
structs.h
transform.c
modules/audio_coding/codecs/iSAC/fix/interface/	audio/coding_isac/fix/
isacfix.h
modules/audio_coding/codecs/iSAC/main/interface/	audio/coding_isac/main/
isac.h
modules/audio_coding/codecs/iSAC/main/source/	audio/coding_isac/main/
arith_routines.c
arith_routines.h
arith_routines_hist.c
arith_routines_logist.c
bandwidth_estimator.h
bandwidth_estimator.c
codec.h
crc.c
crc.h
decode.c
decode_bwe.c
encode.c
encode_lpc_swb.c
encode_lpc_swb.h
entropy_coding.c
entropy_coding.h
fft.c
fft.h
filterbanks.c
filterbank_tables.c
filterbank_tables.h
filter_functions.c
intialize.c
isac.c
lattice.c
lpc_analysis.c
lpc_analysis.h
lpc_gain_swb_tables.c
lpc_gain_swb_tables.h
lpc_shape_swb12_tables.c
lpc_shape_swb12_tables.h
lpc_shape_swb16_tables.c
lpc_shape_swb16_tables.h
lpc_tables.c
lpc_tables.h
os_specific_inline.h
pitch_estimator.c
pitch_estimator.h
pitch_filter.c
pitch_gain_tables.c
pitch_gain_tables.h
pitch_lag_tables.c
pitch_lag_tables.h
settings.h
spectrum_ar_model_tables.c
spectrum_ar_model_tables.h
structs.h
transform.c
modules/audio_processing/include/	audio/processing/
audio_processing.h
modules/audio_processing/	audio/processing/
audio_buffer.cc
audio_buffer.h
audio_processing_impl.cc
audio_processing_impl.h
echo_cancellation_impl.cc
echo_cancellation_impl.h
echo_control_mobile_impl.cc
echo_control_mobile_impl.h
gain_control_impl.cc
gain_control_impl.h
high_pass_filter_impl.cc
high_pass_filter_impl.h
level_estimator_impl.cc
level_estimator_impl.h
noise_suppression_impl.cc
noise_suppression_impl.h
processing_component.cc
processing_component.h
splitting_filter.cc
splitting_filter.h
voice_detection_impl.cc
voice_detection_impl.h
modules/audio_processing/aec/	audio/processing/aec/
aec_core.c
aec_core.h
aec_core_sse2.c
aec_rdft.c
aec_rdft.h
aec_rdft_sse2.c
aec_resampler.c
aec_resampler.h
echo_cancellation.c
modules/audio_processing/aec/include/	audio/processing/aec/
echo_cancellation.h
modules/audio_processing/aecm/include/	audio/processing/aecm/
echo_control_mobile.h
modules/audio_processing/aecm/	audio/processing/aecm/
aecm_core.c
aecm_core.h
aecm_core_neon.c
echo_control_mobile.c
modules/audio_processing/agc/	audio/processing/agc/
analog_agc.c
analog_agc.h
digital_agc.c
digital_agc.h
modules/audio_processing/agc/include/	audio/processing/agc/
gain_control.h
modules/audio_processing/ns/include/	audio/processing/ns/
noise_suppression.h
noise_suppression_x.h
modules/audio_processing/ns/	audio/processing/ns/
defines.h
noise_suppression.c
noise_suppression_x.c
ns_core.c
ns_core.h
nsx_core.c
nsx_core.h
nsx_core_neon.c
nsx_defines.h
windows_private.h
modules/audio_processing/utility/	audio/processing/utility/
delay_estimator.c
delay_estimator.h
delay_estimator_wrapper.c
delay_estimator_wrapper.h
fft4g.c
fft4g.h
ring_buffer.c
ring_buffer.h
system_wrappers/interface/	system_wrappers/
cpu_features_wrapper.h
critical_section_wrapper.h
file_wrapper.h
scoped_ptr.h
system_wrappers/source/	system_wrappers/
cpu_features.cc
cpu.cc
cpu_features_arm.c
cpu_info.cc
cpu_linux.cc
cpu_linux.h
cpu_mac.cc
cpu_mac.h
cpu_measurement_harness.cc
cpu_measurement_harness.h
cpu_no_op.cc
cpu_win.cc
cpu_win.h
critical_section.cc
critical_section_posix.cc
critical_section_posix.h
critical_section_win.cc
critical_section_win.h
file_impl.cc
file_impl.h
EOF
