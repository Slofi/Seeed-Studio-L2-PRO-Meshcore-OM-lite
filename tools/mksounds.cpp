// SPDX-License-Identifier: MIT
// EST-361: render the shared OM Lite sound library to WAVs on the host, plus INDEX.md.
//
// This is the SAME sound_synth.cpp the firmware compiles, at the codec's 16 kHz, 16-bit mono, so the
// file Filip listens to is the waveform the device streams (the ES8311 output stage adds only its
// own analog gain). Nothing here runs on the device.
//
// Build and run from the firmware folder:
//   g++ -std=c++17 -O2 -I src tools/mksounds.cpp src/sound_synth.cpp -o /tmp/mksounds
//   /tmp/mksounds docs/sounds
//
// One file per library sound, named <event>-<n>-<name>.wav where event is the sound's home event and
// n is its index in that event's own set. The index lists the global library id too, so the id in
// NVS and the preview file can be matched. A clipped or empty render fails the tool with non-zero.
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

static std::string notesText(uint8_t id) {
  char buf[192];
  size_t used = 0;
  buf[0] = 0;
  for (int i = 0; i < soundNoteCount(id); ++i) {
    NoteInfo n;
    if (!soundNote(id, i, &n)) break;
    const int wrote = snprintf(buf + used, sizeof(buf) - used, "%s%u Hz@%u+%u", i ? ", " : "",
                               (unsigned)n.hz, (unsigned)n.atMs, (unsigned)n.durMs);
    if (wrote <= 0) break;
    used += (size_t)wrote;
    if (used >= sizeof(buf)) break;
  }
  return std::string(buf);
}

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : "docs/sounds";
  mkdir(dir.c_str(), 0755);   // EEXIST is fine

  static int16_t buf[kMaxSamples];
  int written = 0;
  int failed = 0;
  std::vector<std::string> index;
  index.push_back("# OM Lite sound library (EST-361)");
  index.push_back("");
  index.push_back("Every file is the exact waveform the device streams (16 kHz, 16-bit mono, the same "
                  "sound_synth.cpp). `id` is the shared-library id stored in NVS; `n` is the sound's "
                  "index in its home event's own set. Any sound may be chosen for any notification.");
  index.push_back("");
  index.push_back("| file | event | n | id | family | name | notes (Hz@at+dur ms) | ms | peak |");
  index.push_back("|---|---|---:|---:|---|---|---|---:|---:|");

  printf("%-8s %-2s %-3s %-18s %-7s %5s %7s %8s %8s\n", "event", "n", "id", "name", "family", "ms",
         "samples", "peak", "rms");
  for (int e = 0; e < kEventCount; ++e) {
    const Event ev = (Event)e;
    for (int v = 0; v < variantCount(ev); ++v) {
      const uint8_t id = variantSoundId(ev, (uint8_t)v);
      if (!validSound(id)) {
        printf("FAIL no id for %s v%d\n", eventKey(ev), v);
        ++failed;
        continue;
      }
      RenderInfo info{};
      const int n = renderSound(id, buf, kMaxSamples, &info);
      if (n <= 0) {
        printf("FAIL render %s v%d (%s)\n", eventKey(ev), v, soundName(id));
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
      char name[80];
      snprintf(name, sizeof(name), "%s-%d-%s.wav", eventKey(ev), v, slug(soundName(id)).c_str());
      const std::string path = dir + "/" + name;
      if (!writeWav(path, buf, n)) {
        printf("FAIL write %s\n", path.c_str());
        ++failed;
        continue;
      }
      ++written;
      printf("%-8s v%-2d %-3u %-18s %-7s %5d %7d %8d %8.1f  %s\n", eventKey(ev), v, (unsigned)id,
             soundName(id), familyName(soundFamily(id)), soundMs(id), n, peak, rms, name);
      char row[320];
      snprintf(row, sizeof(row), "| %s | %s | %d | %u | %s | %s | %s | %d | %d |", name,
               eventKey(ev), v, (unsigned)id, familyName(soundFamily(id)), soundName(id),
               notesText(id).c_str(), soundMs(id), peak);
      index.push_back(row);
    }
  }

  std::ofstream idx(dir + "/INDEX.md");
  if (!idx) {
    printf("FAIL write %s/INDEX.md\n", dir.c_str());
    ++failed;
  } else {
    for (const std::string& line : index) idx << line << '\n';
    idx.close();
    if (!idx) {
      printf("FAIL close %s/INDEX.md\n", dir.c_str());
      ++failed;
    }
  }

  printf("\n%d WAV file(s) in %s, %d failure(s); INDEX.md lists all %d\n", written, dir.c_str(),
         failed, written);
  return failed == 0 && written == soundCount() ? 0 : 1;
}
