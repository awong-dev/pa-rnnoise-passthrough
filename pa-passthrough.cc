#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <termios.h>
#include <alsa/asoundlib.h>
#include <algorithm>
#include <limits>
#include <chrono>
extern "C" {
#include "rnnoise-nu.h"
}

static const snd_pcm_format_t kFormat = SND_PCM_FORMAT_S32_LE;
static const snd_pcm_format_t kPlayFormat = SND_PCM_FORMAT_S16_LE;
static const int kNumFrames = 480; // This is how much rnnoise expects. 10ms at 48khz
static const int kSampleRate = 48000;

struct termios orig_termios;

void reset_terminal_mode()
{
      tcsetattr(0, TCSANOW, &orig_termios);
}

void set_conio_terminal_mode()
{
  struct termios new_termios;

  /* take two copies - one for now, one for later */
  tcgetattr(0, &orig_termios);
  memcpy(&new_termios, &orig_termios, sizeof(new_termios));

  /* register cleanup handler, and set the new terminal mode */
  atexit(reset_terminal_mode);
  cfmakeraw(&new_termios);
  tcsetattr(0, TCSANOW, &new_termios);
}


snd_pcm_t* SetUpMic() {
  int err;
  // char *capture_device = argv[1];
  const char *capture_device = "hw:1,0";  // TODO(awong): Override!
  unsigned int rate = kSampleRate;

  snd_pcm_hw_params_t *hw_params;

  snd_pcm_t *capture_handle;
  if ((err = snd_pcm_open(&capture_handle, capture_device, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
    fprintf(stderr, "cannot open audio device %s (%s)\n", capture_device, snd_strerror (err));
    return nullptr;
  }

  fprintf(stdout, "audio interface opened\n");
		   
  if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0) {
    fprintf (stderr, "cannot allocate hardware parameter structure (%s)\n",
             snd_strerror (err));
    return nullptr;
  }

  fprintf(stdout, "hw_params allocated\n");
				 
  if ((err = snd_pcm_hw_params_any (capture_handle, hw_params)) < 0) {
    fprintf (stderr, "cannot initialize hardware parameter structure (%s)\n",
             snd_strerror (err));
    return nullptr;
  }

  fprintf(stdout, "hw_params initialized\n");
	
  if ((err = snd_pcm_hw_params_set_access (capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
    fprintf (stderr, "cannot set access type (%s)\n",
             snd_strerror (err));
    return nullptr;
  }

  fprintf(stdout, "hw_params access setted\n");
	
  if ((err = snd_pcm_hw_params_set_format (capture_handle, hw_params, kFormat)) < 0) {
    fprintf (stderr, "cannot set sample format (%s)\n",
             snd_strerror (err));
    return nullptr;
  }

  fprintf(stdout, "hw_params format setted\n");
	
  if ((err = snd_pcm_hw_params_set_rate_near (capture_handle, hw_params, &rate, 0)) < 0) {
    fprintf (stderr, "cannot set sample rate (%s)\n",
             snd_strerror (err));
    return nullptr;
  }
	
  fprintf(stdout, "hw_params rate setted at rate %d\n", rate);

  if ((err = snd_pcm_hw_params_set_channels (capture_handle, hw_params, 2)) < 0) {
    fprintf (stderr, "cannot set channel count (%s)\n",
             snd_strerror (err));
    return nullptr;
  }

  fprintf(stdout, "hw_params channels setted\n");
	
  if ((err = snd_pcm_hw_params (capture_handle, hw_params)) < 0) {
    fprintf (stderr, "cannot set parameters (%s)\n",
             snd_strerror (err));
    return nullptr;
  }

  fprintf(stdout, "hw_params setted\n");
	
  snd_pcm_hw_params_free (hw_params);

  fprintf(stdout, "hw_params freed\n");
	
  if ((err = snd_pcm_prepare (capture_handle)) < 0) {
    fprintf (stderr, "cannot prepare audio interface for use (%s)\n",
             snd_strerror (err));
    return nullptr;
  }

  fprintf(stdout, "audio interface prepared\n");

  return capture_handle;
}

snd_pcm_t* SetUpSpeaker() {
  int err;
  // char *capture_device = argv[1];
  const char *speaker_device = "hw:0,0";  // TODO(awong): Override!
  unsigned int rate = kSampleRate;

  snd_pcm_hw_params_t *hw_params;

  snd_pcm_t *speaker_handle;
  if ((err = snd_pcm_open(&speaker_handle, speaker_device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
    fprintf(stderr, "cannot open audio device %s (%s)\n", speaker_device, snd_strerror (err));
    return nullptr;
  }

  fprintf(stdout, "audio interface opened\n");
		   
  if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0) {
    fprintf (stderr, "cannot allocate hardware parameter structure (%s)\n",
             snd_strerror (err));
    return nullptr;
  }

  fprintf(stdout, "hw_params allocated\n");
				 
  if ((err = snd_pcm_hw_params_any (speaker_handle, hw_params)) < 0) {
    fprintf (stderr, "cannot initialize hardware parameter structure (%s)\n",
             snd_strerror (err));
    return nullptr;
  }

  fprintf(stdout, "hw_params initialized\n");
	
  if ((err = snd_pcm_hw_params_set_access (speaker_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
    fprintf (stderr, "cannot set access type (%s)\n",
             snd_strerror (err));
    return nullptr;
  }

  fprintf(stdout, "hw_params access setted\n");
	
  if ((err = snd_pcm_hw_params_set_format (speaker_handle, hw_params, kPlayFormat)) < 0) {
    fprintf (stderr, "cannot set sample format (%s)\n",
             snd_strerror (err));
    return nullptr;
  }

  fprintf(stdout, "hw_params format setted\n");
	
  if ((err = snd_pcm_hw_params_set_rate_near (speaker_handle, hw_params, &rate, 0)) < 0) {
    fprintf (stderr, "cannot set sample rate (%s)\n",
             snd_strerror (err));
    return nullptr;
  }
	
  fprintf(stdout, "hw_params rate setted at rate %d\n", rate);

  if ((err = snd_pcm_hw_params_set_channels (speaker_handle, hw_params, 1)) < 0) {
    fprintf (stderr, "cannot set channel count (%s)\n",
             snd_strerror (err));
    return nullptr;
  }

  fprintf(stdout, "hw_params channels setted\n");
	
  if ((err = snd_pcm_hw_params (speaker_handle, hw_params)) < 0) {
    fprintf (stderr, "cannot set parameters (%s)\n",
             snd_strerror (err));
    return nullptr;
  }

  fprintf(stdout, "hw_params setted\n");
	
  snd_pcm_hw_params_free (hw_params);

  fprintf(stdout, "hw_params freed\n");
	
  if ((err = snd_pcm_prepare (speaker_handle)) < 0) {
    fprintf (stderr, "cannot prepare audio interface for use (%s)\n",
             snd_strerror (err));
    return nullptr;
  }

  fprintf(stdout, "audio interface prepared\n");

  return speaker_handle;
}

int main(int argc, char* argv[]) {
  printf("Starting\n");
  const int kMaxAttenuation = 13 + 1; // 32 -> 16 bit + 1 for extra channel
  int attenuation = kMaxAttenuation;
  snd_pcm_t* capture_handle = SetUpMic();
  snd_pcm_t* speaker_handle = SetUpSpeaker();

  DenoiseState* sts;
  sts = rnnoise_create(nullptr);
  rnnoise_set_param(sts, RNNOISE_PARAM_MAX_ATTENUATION, 5); // Empiracal test that 5db is good.
  rnnoise_set_param(sts, RNNOISE_PARAM_SAMPLE_RATE, kSampleRate);

  if (sizeof(int32_t) == snd_pcm_format_width(kFormat)/8*2) {
    fprintf(stderr, "We have a weird format\n");
    return 1; 
  }
  int32_t buffer[kNumFrames*2];
  float mixbuffer[kNumFrames];
  int mixbuffer_fill = 0;

  fprintf(stdout, "buffer allocated\n");

  printf("Should be looping 10s here\n");

  uint64_t num_buffers = 0;
  char cmd;
  fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
  set_conio_terminal_mode();

  while (1) {
    printf(".");
    for (int i = 0; i < 100; ++i) {
      int bytes = read(0, &cmd, 1);
      if (bytes > 0) {
        switch (cmd) {
          case '=':
          case '+':
            printf("l\n");
            attenuation = std::max(0, attenuation - 1);
            break;

          case '_':
          case '-':
            printf("q\n");
            attenuation = std::min(17, attenuation + 1);
            break;

          case 'q':
          case 'Q':
            exit(0);
            break;
        }
      }
      int frames = 0;
      frames = snd_pcm_readi (capture_handle, buffer, kNumFrames);
      if (frames < 0) {
        fprintf (stderr, "read error: %d (%s)\n", frames, snd_strerror(frames));
        exit(1);
      }

      for (int i = 0; i < frames; ++i) {
        // Do mono downmix.
        mixbuffer[mixbuffer_fill] = buffer[i*2];
        mixbuffer[mixbuffer_fill] += buffer[i*2 + 1];
        // Signal needs to be downmixed from 32-bit resolution to 16-bit.
        mixbuffer[mixbuffer_fill] /= (1 << attenuation);

        mixbuffer_fill++;

        if (mixbuffer_fill == kNumFrames) {
          // Do DSP Processing here.
          rnnoise_process_frame(sts, &mixbuffer[0], &mixbuffer[0]);
          int16_t outbuffer[kNumFrames];
          for (int j = 0; j < kNumFrames; j++) {
            if (mixbuffer[j] > 0 && mixbuffer[j] > std::numeric_limits<int16_t>::max() - 10) {
              outbuffer[j] = std::numeric_limits<int16_t>::max();
            } else if (mixbuffer[j] < 0 && mixbuffer[j] < std::numeric_limits<int16_t>::min() + 10)  {
              outbuffer[j] = std::numeric_limits<int16_t>::min();
	    } else {
              outbuffer[j] = mixbuffer[j];
	    }
          }
          int out_frames = snd_pcm_writei(speaker_handle, outbuffer, kNumFrames);
          if (out_frames != kNumFrames) {
            if (out_frames < 0)  {
              fprintf (stderr, "write error: %d (%s)\n", out_frames, snd_strerror (out_frames));
              exit (2);
            } else {
              fprintf (stderr, "dropped %d frames\n", kNumFrames - out_frames);
            }
          }
          mixbuffer_fill = 0;
        }
      }
      num_buffers++;
    }
  }
  fprintf(stdout, "read buffers %lld done\n", num_buffers);

  printf("Terminating\n");
  return 0;
}
