#include "mp3_decoder_task.h"

// complete stub: remove heavy libmad logic

mp3_decoder_task::mp3_decoder_task(pcm_audio_interface & pcm_if, sd_reader_task & sd)
    : task("MP3 decoder", MP3_DECODER_STACKSIZE), _pcm_if(pcm_if), _sd_reader(sd) {}

void mp3_decoder_task::run() {}
void mp3_decoder_task::reset() {}
uint16_t mp3_decoder_task::get_position(unsigned long, int) { return 0; }
uint32_t mp3_decoder_task::get_total_seconds() { return 0; }

// callback stubs
enum mad_flow mp3_decoder_task::input(void *, struct mad_stream *) { return MAD_FLOW_CONTINUE; }
enum mad_flow mp3_decoder_task::header(void *, struct mad_header const *) { return MAD_FLOW_CONTINUE; }
enum mad_flow mp3_decoder_task::output(void *, mad_header const *, mad_pcm *) { return MAD_FLOW_CONTINUE; }
enum mad_flow mp3_decoder_task::error(void *, mad_stream *, mad_frame *) { return MAD_FLOW_CONTINUE; }
int16_t mp3_decoder_task::scale(mad_fixed_t) { return 0; }
