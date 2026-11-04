#pragma once
// Per-instrument DSP patch graph (#75 phase 2, first slice). A patch is data:
// typed parameters bound to fixed host pool indices, versioned blocks from the
// Vox DSP Core, typed connections, one output and a voice policy. It is
// validated and compiled off the audio thread into a fixed plan; the audio
// thread only steps that plan (no allocation, locks, parsing or file access).
//
// This is NOT the Rack audio bus / FX graph: it describes one instrument's
// voice. Slice 1 supports a mono last-note-priority voice and five blocks:
//   env.adsr@1      out:control   params attack, decay, sustain, release
//   osc.blep@1      out:audio     params wave            (pitch from the voice)
//   filter.svf.lp@1 in:audio  out:audio   params cutoff, resonance
//   amp.vca@1       in:audio env:control out:audio   (in * env * velocity)
//   amp.gain@1      in:audio  out:audio   params gain
// The voice ends when its amplitude envelope (`voice.ampEnvelope`) is idle.
#include "dspcore/Primitives.h"
#include "instrument/InstrumentContract.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace vstengine::dspcore {

inline constexpr std::size_t maxPatchNodes = 64;
inline constexpr const char *graphProviderId = "vox.graph";

struct PatchParameter {
  instrument::ParameterDescriptor descriptor; // id, range, type, poolIndex ...
  double smoothingSeconds{};                  // 0: applied immediately
};

struct PatchNode {
  std::string id;
  std::string type;                          // e.g. "osc.blep@1"
  std::map<std::string, std::string> params; // block parameter -> patch parameter id
  std::map<std::string, std::string> inputs;  // block input -> "node.port"
};

struct PatchSpec {
  int graphVersion{1};
  std::string instrumentId, name, vendor;
  std::uint32_t instrumentVersion{1};
  std::string voicePolicy{"mono-last"};
  std::string ampEnvelope;                   // node id of the voice's amplitude env
  std::vector<PatchParameter> parameters;
  std::vector<PatchNode> nodes;
  std::string output;                        // "node.port"
};

enum class BlockKind : std::uint8_t { envAdsr, oscBlep, svfLowpass, vca, gain };

struct CompiledNode {
  BlockKind kind{};
  int params[4]{-1, -1, -1, -1}; // patch parameter indices, block-specific order
  int inputs[2]{-1, -1};         // signal slots
  int output{-1};                // signal slot
  int state{-1};                 // index into the per-kind state arrays
};

// Immutable result of a successful compile; shared by all instances.
struct CompiledPatch {
  instrument::InstrumentDescriptor descriptor;
  std::vector<double> smoothing;     // per parameter, seconds (0 = none)
  std::vector<CompiledNode> plan;    // evaluation order
  int outputSlot{-1};
  int ampEnvelopeState{-1};
  int signalSlots{};
  int envCount{}, oscCount{}, svfCount{};
};

// Controller thread. Returns false with a reason; `out` is untouched then.
[[nodiscard]] bool compilePatch(const PatchSpec &spec, CompiledPatch &out, std::string &why);

// Mono last-note-priority voice running a compiled plan (slice 1). All memory
// is sized in prepare(); process functions are noexcept and allocation-free.
class PatchVoice final {
public:
  explicit PatchVoice(const CompiledPatch &patch);
  void prepare(double sampleRate);
  void reset() noexcept;          // silence, release pedal, snap smoothing
  void setParameter(std::size_t index, double plain) noexcept { targets_[index] = plain; }
  double parameter(std::size_t index) const noexcept { return targets_[index]; }
  void midi(const std::uint8_t *bytes, std::uint32_t size) noexcept;
  void allNotesOff() noexcept;
  void allSoundOff() noexcept { silence(); }
  double next() noexcept; // one output sample

private:
  void silence() noexcept;
  void noteOn(int key, int velocity) noexcept;
  void noteOff(int key) noexcept;
  void startRelease() noexcept;

  const CompiledPatch &patch_;
  double sampleRate_{48000.0};
  std::vector<double> targets_;      // plain parameter values
  std::vector<Smoothed> smoothed_;   // per parameter (used when smoothing > 0)
  std::vector<double> current_;      // smoothed/current per parameter, per sample
  std::vector<double> signals_;
  std::vector<LinearAdsr> envs_;
  std::vector<BlepOscillator> oscs_;
  std::vector<SvfLowpass> svfs_;
  int held_[16]{};
  int heldCount_{};
  int note_{-1};
  bool sustainPedal_{}, sustained_{};
  double velocity_{};
};

} // namespace vstengine::dspcore
