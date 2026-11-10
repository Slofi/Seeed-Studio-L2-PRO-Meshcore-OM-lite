// SPDX-License-Identifier: MIT
// EST-359: render every event variant of the OM Lite sound bank to a WAV on the host.
//
// This is the SAME sound_synth.cpp the firmware compiles, at the codec's 16 kHz, 16-bit mono, so the
// file Claude listens to is the waveform the device streams (the ES8311 output stage adds only its
// own analog gain). Nothing here runs on the device.
//
// Build and run from the firmware folder:
//   g++ -std=c++17 -O2 -I src tools/mksounds.cpp src/sound_synth.cpp -o /tmp/mksounds
//   /tmp/mksounds docs/sounds
//
// It writes one file per variant (nine events, three variants each) and prints the length, peak and
// RMS of every one; a clipped or empty render fails the tool with a non-zero exit.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <fstream>
#include <string>
#include <vector>

#include "sound_synth.h"

using namespace om::sound;

static void putU16(std::vector<uint8_t>& b, uint16_t v) {
  b.push_back((uint8_t)(v & 0xFF));
  b.push_back((uint8_t)((v >> 8) & 0xFF));
}

static void putU32(std::vector<uint8_t>& b, uint32_t v) {
  b.push_back((uint8_t)(v & 0xFF));
  b.push_back((uint8_t)((v >> 8) & 0xFF));
  b.push_back((uint8_t)((v >> 16) & 0xFF));
  b.push_back((uint8_t)((v >> 24) & 0xFF));
}

static void putTag(std::vector<uint8_t>& b, const char* tag) {
  for (int i = 0; i < 4; ++i) b.push_back((uint8_t)tag[i]);
}

// A file name that reads well in a folder listing and on any OS.
static std::string slug(const char* text) {
  std::string out;
  for (const char* p = text; p != nullptr && *p != 0; ++p) {
    const char c = *p;
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out.push_back(c);
    else if (c >= 'A' && c <= 'Z') out.push_back((char)(c + 32));
    else if (c == ' ' || c == '_' || c == '-') {
      if (!out.empty() && out.back() != '-') out.push_back('-');
    }
  }
  while (!out.empty() && out.back() == '-') out.pop_back();
  return out;
}

static bool writeWav(const std::string& path, const int16_t* samples, int n) {
  std::vector<uint8_t> h;
  const uint32_t dataBytes = (uint32_t)n * 2u;
  putTag(h, "RIFF");
  putU32(h, 36u + dataBytes);
  putTag(h, "WAVE");
  putTag(h, "fmt ");
  putU32(h, 16u);                 // PCM fmt chunk size
  putU16(h, 1u);                  // PCM
  putU16(h, 1u);                  // mono
  putU32(h, kRate);               // sample rate
  putU32(h, kRate * 2u);          // byte rate
  putU16(h, 2u);                  // block align
  putU16(h, 16u);                 // bits per sample
  putTag(h, "data");
  putU32(h, dataBytes);

  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  f.write((const char*)h.data(), (std::streamsize)h.size());
  f.write((const char*)samples, (std::streamsize)dataBytes);
  return f.good();
}

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : "docs/sounds";
  mkdir(dir.c_str(), 0755);   // EEXIST is fine

  static int16_t buf[kMaxSamples];
  int written = 0;
  int failed = 0;
  printf("%-8s %-3s %-12s %5s %7s %8s %8s\n", "event", "var", "name", "ms", "samples", "peak", "rms");
  for (int e = 0; e < kEventCount; ++e) {
    const Event ev = (Event)e;
    for (int v = 0; v < variantCount(ev); ++v) {
      RenderInfo info{};
      const int n = render(ev, (uint8_t)v, buf, kMaxSamples, &info);
      if (n <= 0) {
        printf("FAIL render %s v%d\n", eventKey(ev), v);
        ++failed;
        continue;
      }
      int peak = 0;
      bool clipped = false;
      double rms = 0.0;
      for (int i = 0; i < n; ++i) {
        const int m = buf[i] < 0 ? -buf[i] : buf[i];
        if (m > peak) peak = m;
        if (buf[i] == 32767 || buf[i] == -32768) clipped = true;
        rms += (double)buf[i] * (double)buf[i];
      }
      rms = sqrt(rms / (double)n);
      if (clipped || peak > kPeakTarget || buf[n - 1] != 0) {
        printf("FAIL headroom/end %s v%d: peak=%d last=%d\n", eventKey(ev), v, peak, buf[n - 1]);
        ++failed;
      }
      char name[64];
      snprintf(name, sizeof(name), "%s-%d-%s.wav", eventKey(ev), v,
               slug(variantName(ev, (uint8_t)v)).c_str());
      const std::string path = dir + "/" + name;
      if (!writeWav(path, buf, n)) {
        printf("FAIL write %s\n", path.c_str());
        ++failed;
        continue;
      }
      ++written;
      printf("%-8s v%-2d %-12s %5d %7d %8d %8.1f  %s\n", eventKey(ev), v,
             variantName(ev, (uint8_t)v), eventMs(ev, (uint8_t)v), n, peak, rms, name);
    }
  }
  printf("\n%d WAV file(s) in %s, %d failure(s)\n", written, dir.c_str(), failed);
  return failed == 0 && written == 27 ? 0 : 1;
}
