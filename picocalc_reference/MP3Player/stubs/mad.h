#ifndef MAD_H
#define MAD_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { MAD_DECODER_MODE_SYNC = 0 } mad_decoder_mode;
enum mad_flow { MAD_FLOW_CONTINUE = 0 };
typedef int mad_fixed_t;
typedef int mad_timer_t;

struct mad_stream { char *bufend; char *buffer; char *next_frame; int error; };
struct mad_header {};
struct mad_pcm {};
struct mad_frame {};

struct mad_decoder {};

int mad_decoder_init(struct mad_decoder *decoder,
                     void *data,
                     mad_flow (*input)(void *, struct mad_stream *),
                     mad_flow (*header)(void *, struct mad_header const *),
                     mad_flow (*filter)(void *, struct mad_header const *, struct mad_pcm *),
                     mad_flow (*output)(void *, struct mad_header const *, struct mad_pcm *),
                     mad_flow (*error)(void *, struct mad_stream *, struct mad_frame *),
                     void *message);

int mad_decoder_run(struct mad_decoder *decoder, mad_decoder_mode mode);
void mad_decoder_finish(struct mad_decoder *decoder);
inline void mad_timer_reset(mad_timer_t *timer) { (void)timer; }

#ifdef __cplusplus
}
#endif

#endif // MAD_H
