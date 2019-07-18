#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstring>

#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <limits>
#include <chrono>

extern "C" {
#include "rnnoise-nu.h"
}

#include "portaudio.h"
#include "pa_linux_alsa.h"
#include "terminal.h"

static constexpr const int kNumFrames = 480; // This is how much rnnoise expects. 10ms at 48khz
static constexpr const int kSampleRate = 48000;

std::atomic_int g_calls{0};
std::atomic_int g_underflows{0};
std::atomic_int g_overflows{0};
std::atomic_int g_pa_input_underflows{0};
std::atomic_int g_pa_input_overflows{0};
std::atomic_int g_pa_output_underflows{0};
std::atomic_int g_pa_output_overflows{0};

struct StreamState {
  StreamState() {
    // Set up Rnnoise.
    sts = rnnoise_create(nullptr);
    rnnoise_set_param(sts, RNNOISE_PARAM_MAX_ATTENUATION, 5); // Empiracal test that 5db is good.
    rnnoise_set_param(sts, RNNOISE_PARAM_SAMPLE_RATE, kSampleRate);
  }

  void Open(int inDev, int outDev) {
    unsigned long framesPerBuffer = kNumFrames;
    PaStreamParameters inputParameters = {};
    inputParameters.channelCount = 2;
    inputParameters.device = inDev;
    inputParameters.hostApiSpecificStreamInfo = NULL;
    inputParameters.sampleFormat = paInt32;
    inputParameters.suggestedLatency = Pa_GetDeviceInfo(inDev)->defaultLowInputLatency ;
    inputParameters.hostApiSpecificStreamInfo = NULL;

    PaStreamParameters outputParameters = {};
    outputParameters.channelCount = 2;
    outputParameters.device = outDev;
    outputParameters.hostApiSpecificStreamInfo = NULL;
    outputParameters.sampleFormat = paInt16;
    outputParameters.suggestedLatency = Pa_GetDeviceInfo(outDev)->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;

    int err = Pa_OpenStream(
	&stream,
	&inputParameters,
	&outputParameters,
	kSampleRate,
	framesPerBuffer,
	paNoFlag,
	portAudioCallback, // your callback function
	(void *)this );
    if( err != paNoError ) {
    fprintf(stderr, "PortAudio stream error: %s\n", Pa_GetErrorText(err));
      exit(1);
    }
  }

  static int portAudioCallback(
      const void *inputBuffer, void *outputBuffer,
      unsigned long framesPerBuffer,
      const PaStreamCallbackTimeInfo* timeInfo,
      PaStreamCallbackFlags statusFlags,
      void *userData )
  {
    g_calls.fetch_add(1, std::memory_order_relaxed);
    g_pa_input_underflows.fetch_add((statusFlags & paInputUnderflow) != 0, std::memory_order_relaxed);
    g_pa_input_overflows.fetch_add((statusFlags & paInputOverflow) != 0, std::memory_order_relaxed);
    g_pa_output_underflows.fetch_add((statusFlags & paOutputUnderflow) != 0, std::memory_order_relaxed);
    g_pa_output_overflows.fetch_add((statusFlags & paOutputOverflow) != 0, std::memory_order_relaxed);

    /* Cast data passed through stream to our structure. */
    StreamState *state = static_cast<StreamState*>(userData);
    int16_t *out = (int16_t*)outputBuffer;
    int32_t *in  = (int32_t*)inputBuffer; /* Prevent unused variable warning. */
#if 0
    for (unsigned int i = 0; i < framesPerBuffer; ++i) {
      out[i*2] = in[i*2] >> 16;
      out[i*2 +1] = in[i*2 +1] >> 16;
    }
    return 0;
#endif

    for(unsigned int i=0; i<framesPerBuffer; i++ ) {
      // Mono Downmix and attenuate.
      state->mixbuffer[state->mixbuffer_fill] = ((in[i*2] / 2) + (in[i*2+1] / 2)) >> state->attenuation;
      state->mixbuffer_fill++;
      if (state->mixbuffer_fill == kNumFrames) {
        // Drop if we'd overflow output.
        if ((state->out_fill + kNumFrames) <= kOutQueueMax) {
          rnnoise_process_frame(state->sts, &state->out_queue[state->out_fill], &state->mixbuffer[0]);
          state->out_fill += kNumFrames;
        } else {
          g_overflows.fetch_add(1, std::memory_order_relaxed);
        }
        state->mixbuffer_fill = 0;
      }
    }


    // We're underflowing output.
    unsigned int frames_to_write = std::min(state->out_fill, framesPerBuffer);
    if (frames_to_write < framesPerBuffer) {
      g_underflows++;
    }
    for (unsigned int i = 0; i < frames_to_write; i++) {
      if (state->out_queue[i] > 0 && state->out_queue[i] > std::numeric_limits<int16_t>::max() - 10) {
	out[i*2] = std::numeric_limits<int16_t>::max();
	out[i*2 + 1] = std::numeric_limits<int16_t>::max();
      } else if (state->out_queue[i] < 0 && state->out_queue[i] < std::numeric_limits<int16_t>::min() + 10)  {
	out[i*2] = std::numeric_limits<int16_t>::min();
	out[i*2 + 1] = std::numeric_limits<int16_t>::min();
      } else {
	out[i*2] = state->out_queue[i];
	out[i*2+1] = state->out_queue[i];
      }
    }

    // Maintain the output buffer.
    if (frames_to_write < state->out_fill) {
      memcpy(state->out_queue, state->out_queue + frames_to_write, state->out_fill - frames_to_write);
      state->out_fill = state->out_fill - frames_to_write;
    } else {
      state->out_fill = 0;
    }
    return 0;
  }


  PaStream* stream;
  DenoiseState* sts;
  float mixbuffer[kNumFrames];
  unsigned long mixbuffer_fill = 0;

  static constexpr int kOutQueueMax = kNumFrames * 2;
  float out_queue[kOutQueueMax];
  unsigned long out_fill = 0;

  static constexpr int kMaxAttenuation = 13 + 1; // 32 -> 16 bit + 1 for extra channel
  std::atomic_int attenuation{kMaxAttenuation};
};

void try_set_realtime() {
  int ret;
  pthread_t this_thread = pthread_self();
  struct sched_param params = {};
  params.sched_priority = sched_get_priority_max(SCHED_FIFO);
  ret = pthread_setschedparam(this_thread, SCHED_FIFO, &params);
  if (ret != 0) {
    fprintf(stderr, "Failed to set realtime priority\n");
  }
}

int main(int argc, char* argv[]) {
  printf("Starting\n");

  // Initialize Port Audio.
  int err = Pa_Initialize();
  if( err != paNoError ) {
    fprintf(stderr, "PortAudio init error: %s\n", Pa_GetErrorText(err));
    return 1;
  }

  // Open the microphone.
  int numDevices;
  numDevices = Pa_GetDeviceCount();
  if( numDevices < 0 )
  {
    printf( "ERROR: Pa_CountDevices returned 0x%x\n", numDevices );
    return 1;
  }

  for (int i = 0; i < numDevices; ++i) {
    printf("id %d = %s\n", i, Pa_GetDeviceInfo(i)->name);
  }

  StreamState stream_state;
  stream_state.Open(2, 0);
  PaAlsa_EnableRealtimeScheduling(stream_state.stream, 1);
//  try_set_realtime();
  err = Pa_StartStream(stream_state.stream);
  if( err != paNoError ) {
    fprintf(stderr, "PortAudio stream start error: %s\n", Pa_GetErrorText(err));
    return 1;
  }

  char cmd;
  fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
  set_conio_terminal_mode();

  int last_num_calls = g_calls;
  int last_errors = g_pa_output_overflows + g_pa_output_underflows + g_pa_input_underflows + g_pa_input_overflows;
  while (1) {
    printf(".");
    fflush(stdout);
    for (int i = 0; i < 100; ++i) {
      int bytes = read(0, &cmd, 1);
      if (bytes > 0) {
        printf("\n");
        fflush(stdout);
        switch (cmd) {
          case '=':
          case '+':
            printf("l\n");
            stream_state.attenuation = std::max(0, stream_state.attenuation - 1);
            break;

          case '_':
          case '-':
            printf("q\n");
            stream_state.attenuation = std::min(17, stream_state.attenuation + 1);
            break;

          case 'q':
          case 'Q':
            goto end;
        }
      }
      Pa_Sleep(200);
      int cur_calls = g_calls;
      int errors = g_pa_output_overflows + g_pa_output_underflows + g_pa_input_underflows + g_pa_input_overflows;
      if (last_num_calls == cur_calls || (last_errors + 10) < errors) {
        // Wedged.
        goto end;
      }
      last_errors = errors;
      last_num_calls = cur_calls;
    }
  }
end:

  reset_terminal_mode();

  printf("Terminating after %d calls %d underflows %d overflows, %d pa_input_underflow, %d pa_input_overflow, %d pa_output_underflow, %d pa_output_overflow\n",
      g_calls.load(), g_underflows.load(), g_overflows.load(), g_pa_input_underflows.load(), g_pa_input_overflows.load(), g_pa_output_underflows.load(), g_pa_output_overflows.load());
  err = Pa_Terminate();
  if( err != paNoError ) {
    printf("PortAudio error: %s\n", Pa_GetErrorText(err));
  }
  return 0;
}
