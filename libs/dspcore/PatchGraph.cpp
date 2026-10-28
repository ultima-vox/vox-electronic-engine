#include "dspcore/PatchGraph.h"

#include <algorithm>
#include <cstring>
#include <set>

namespace vstengine::dspcore {
namespace {

enum class PortType { audio, control };

// The values a block input accepts. The whole host range [min, max] of the
// parameter bound to it must lie inside, so no automation value can drive the
// DSP out of its stable domain (validated at compile time, not clamped).
struct BlockParam {
  const char *name;
  double lowest, highest;
  bool integral; // needs an int/choice/bool parameter without smoothing
};

struct BlockInfo {
  const char *type;
  BlockKind kind;
  std::vector<BlockParam> params; // fixed order -> CompiledNode::params
  std::vector<std::pair<const char *, PortType>> inputs;
  PortType output;
};

const std::vector<BlockInfo> &blockCatalog() {
  static const std::vector<BlockInfo> catalog = {
      {"env.adsr@1", BlockKind::envAdsr,
       {{"attack", 0.001, 60.0, false}, {"decay", 0.001, 60.0, false},
        {"sustain", 0.0, 1.0, false}, {"release", 0.001, 60.0, false}},
       {}, PortType::control},
      {"osc.blep@1", BlockKind::oscBlep, {{"wave", 0.0, 2.0, true}}, {}, PortType::audio},
      {"filter.svf.lp@1", BlockKind::svfLowpass,
       {{"cutoff", 20.0, 20000.0, false}, {"resonance", 0.0, 1.0, false}},
       {{"in", PortType::audio}}, PortType::audio},
      {"amp.vca@1", BlockKind::vca, {},
       {{"in", PortType::audio}, {"env", PortType::control}}, PortType::audio},
      {"amp.gain@1", BlockKind::gain, {{"gain", 0.0, 4.0, false}},
       {{"in", PortType::audio}}, PortType::audio},
  };
  return catalog;
}

const BlockInfo *findBlock(const std::string &type) {
  for (const auto &block : blockCatalog())
    if (type == block.type)
      return &block;
  return nullptr;
}

bool splitEndpoint(const std::string &endpoint, std::string &node, std::string &port) {
  const auto dot = endpoint.find('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 >= endpoint.size())
    return false;
  node = endpoint.substr(0, dot);
  port = endpoint.substr(dot + 1);
  return port == "out";
}

bool validParameter(const instrument::ParameterDescriptor &p) {
  if (p.id.empty() || p.name.empty() || !std::isfinite(p.minimum) || !std::isfinite(p.maximum) ||
      !std::isfinite(p.defaultValue) || p.minimum >= p.maximum || p.defaultValue < p.minimum ||
      p.defaultValue > p.maximum || p.poolIndex < 0 ||
      p.poolIndex >= static_cast<std::int16_t>(instrument::poolParametersPerSlot))
    return false;
  if (p.type == instrument::ParameterType::choice &&
      (p.minimum != 0 || p.choices.size() != static_cast<std::size_t>(p.maximum) + 1))
    return false;
  return true;
}
} // namespace

bool compilePatch(const PatchSpec &spec, CompiledPatch &out, std::string &why) {
  if (spec.graphVersion != 1) {
    why = "unsupported graphVersion (expected 1)";
    return false;
  }
  if (spec.voicePolicy != "mono-last") {
    why = "unsupported voice policy '" + spec.voicePolicy + "' (slice 1: mono-last)";
    return false;
  }
  if (spec.instrumentId.empty() || spec.name.empty() || spec.instrumentVersion == 0) {
    why = "instrument id, name and version are required";
    return false;
  }
  if (spec.nodes.empty() || spec.nodes.size() > maxPatchNodes) {
    why = "a patch needs 1.." + std::to_string(maxPatchNodes) + " nodes";
    return false;
  }
  // Parameters: valid, unique ids and pool indices.
  std::map<std::string, int> paramIndex;
  std::set<std::int16_t> pools;
  for (std::size_t i = 0; i < spec.parameters.size(); ++i) {
    const auto &p = spec.parameters[i];
    if (!validParameter(p.descriptor) || !(p.smoothingSeconds >= 0.0) ||
        p.smoothingSeconds > 1.0) {
      why = "invalid parameter '" + p.descriptor.id + "'";
      return false;
    }
    if (!paramIndex.emplace(p.descriptor.id, static_cast<int>(i)).second ||
        !pools.insert(p.descriptor.poolIndex).second) {
      why = "duplicate parameter id or pool index: '" + p.descriptor.id + "'";
      return false;
    }
  }
  // Nodes: known versioned blocks, unique ids, mapped parameters exist.
  std::map<std::string, std::size_t> nodeIndex;
  for (std::size_t i = 0; i < spec.nodes.size(); ++i) {
    const auto &node = spec.nodes[i];
    const auto *block = findBlock(node.type);
    if (block == nullptr) {
      why = "node '" + node.id + "': unknown block type or version '" + node.type + "'";
      return false;
    }
    if (node.id.empty() || node.id.find('.') != std::string::npos ||
        !nodeIndex.emplace(node.id, i).second) {
      why = "node id missing, containing '.', or duplicated: '" + node.id + "'";
      return false;
    }
    for (const auto &[blockParam, patchParam] : node.params) {
      const auto domain = std::find_if(block->params.begin(), block->params.end(),
                                       [&](const BlockParam &b) { return blockParam == b.name; });
      if (domain == block->params.end()) {
        why = "node '" + node.id + "': block has no parameter '" + blockParam + "'";
        return false;
      }
      const auto found = paramIndex.find(patchParam);
      if (found == paramIndex.end()) {
        why = "node '" + node.id + "': unknown patch parameter '" + patchParam + "'";
        return false;
      }
      const auto &bound = spec.parameters[static_cast<std::size_t>(found->second)];
      const auto &d = bound.descriptor;
      if (d.minimum < domain->lowest || d.maximum > domain->highest) {
        why = "node '" + node.id + "': parameter '" + patchParam + "' range [" +
              std::to_string(d.minimum) + ", " + std::to_string(d.maximum) + "] is outside " +
              node.type + " " + blockParam + " [" + std::to_string(domain->lowest) + ", " +
              std::to_string(domain->highest) + "]";
        return false;
      }
      if (domain->integral &&
          (d.type == instrument::ParameterType::floating || bound.smoothingSeconds > 0.0)) {
        why = "node '" + node.id + "': " + blockParam +
              " needs an int/choice parameter without smoothing";
        return false;
      }
    }
    for (const auto &required : block->params)
      if (!node.params.count(required.name)) {
        why = "node '" + node.id + "': parameter '" + required.name + "' is not mapped";
        return false;
      }
  }
  // Connections: every input wired to an existing node output of the right type.
  std::vector<std::vector<std::size_t>> deps(spec.nodes.size());
  for (std::size_t i = 0; i < spec.nodes.size(); ++i) {
    const auto &node = spec.nodes[i];
    const auto *block = findBlock(node.type);
    if (node.inputs.size() != block->inputs.size()) {
      why = "node '" + node.id + "': wrong number of inputs";
      return false;
    }
    for (const auto &[port, type] : block->inputs) {
      const auto it = node.inputs.find(port);
      std::string from, fromPort;
      if (it == node.inputs.end() || !splitEndpoint(it->second, from, fromPort) ||
          !nodeIndex.count(from)) {
        why = "node '" + node.id + "': input '" + port + "' is not connected to a node output";
        return false;
      }
      const auto source = nodeIndex[from];
      if (findBlock(spec.nodes[source].type)->output != type) {
        why = "node '" + node.id + "': input '" + port + "' has the wrong signal type";
        return false;
      }
      deps[i].push_back(source);
    }
  }
  std::string outNode, outPort;
  if (!splitEndpoint(spec.output, outNode, outPort) || !nodeIndex.count(outNode) ||
      findBlock(spec.nodes[nodeIndex[outNode]].type)->output != PortType::audio) {
    why = "output must name an audio node output ('node.out')";
    return false;
  }
  if (!nodeIndex.count(spec.ampEnvelope) ||
      findBlock(spec.nodes[nodeIndex[spec.ampEnvelope]].type)->kind != BlockKind::envAdsr) {
    why = "voice.ampEnvelope must name an env.adsr node";
    return false;
  }
  // Topological order (Kahn); envelopes first among ready nodes so the voice can
  // end before any audio block advances. A cycle is rejected (no delay block yet).
  std::vector<int> remaining(spec.nodes.size());
  for (std::size_t i = 0; i < spec.nodes.size(); ++i)
    remaining[i] = static_cast<int>(deps[i].size());
  std::vector<std::size_t> order;
  std::vector<bool> done(spec.nodes.size());
  while (order.size() < spec.nodes.size()) {
    std::size_t pick = spec.nodes.size();
    for (int pass = 0; pass < 2 && pick == spec.nodes.size(); ++pass)
      for (std::size_t i = 0; i < spec.nodes.size(); ++i)
        if (!done[i] && remaining[i] == 0 &&
            (pass == 1 || findBlock(spec.nodes[i].type)->kind == BlockKind::envAdsr)) {
          pick = i;
          break;
        }
    if (pick == spec.nodes.size()) {
      why = "the patch has a cycle";
      return false;
    }
    done[pick] = true;
    order.push_back(pick);
    for (std::size_t i = 0; i < spec.nodes.size(); ++i)
      for (const auto d : deps[i])
        if (d == pick)
          --remaining[i];
  }

  CompiledPatch patch;
  auto &d = patch.descriptor;
  d.id = spec.instrumentId;
  d.providerId = graphProviderId;
  d.name = spec.name;
  d.vendor = spec.vendor;
  d.instrumentVersion = spec.instrumentVersion;
  d.stateVersion = 1;
  d.contentVersion = 1;
  d.capabilities = instrument::Capability::notes | instrument::Capability::standardEditor;
  d.budget.maxVoices = 1;
  d.budget.maxMidiEventsPerBlock = 512;
  d.budget.maxStateBytes = 0;
  d.directParameters = !spec.parameters.empty();
  for (const auto &p : spec.parameters) {
    d.parameters.push_back(p.descriptor);
    patch.smoothing.push_back(p.smoothingSeconds);
  }
  std::vector<int> slotOf(spec.nodes.size(), -1);
  for (const auto i : order) {
    const auto &node = spec.nodes[i];
    const auto *block = findBlock(node.type);
    CompiledNode compiled;
    compiled.kind = block->kind;
    for (std::size_t k = 0; k < block->params.size(); ++k)
      compiled.params[k] = paramIndex[node.params.at(block->params[k].name)];
    for (std::size_t k = 0; k < block->inputs.size(); ++k) {
      std::string from, port;
      splitEndpoint(node.inputs.at(block->inputs[k].first), from, port);
      compiled.inputs[k] = slotOf[nodeIndex[from]];
    }
    compiled.output = slotOf[i] = patch.signalSlots++;
    switch (block->kind) {
    case BlockKind::envAdsr:
      compiled.state = patch.envCount++;
      if (node.id == spec.ampEnvelope)
        patch.ampEnvelopeState = compiled.state;
      break;
    case BlockKind::oscBlep:
      compiled.state = patch.oscCount++;
      break;
    case BlockKind::svfLowpass:
      compiled.state = patch.svfCount++;
      break;
    default:
      break;
    }
    patch.plan.push_back(compiled);
  }
  patch.outputSlot = slotOf[nodeIndex[outNode]];
  out = std::move(patch);
  why.clear();
  return true;
}

// ------------------------------------------------------------ voice
PatchVoice::PatchVoice(const CompiledPatch &patch)
    : patch_(patch), targets_(patch.descriptor.parameters.size()),
      smoothed_(patch.descriptor.parameters.size()), current_(patch.descriptor.parameters.size()),
      signals_(static_cast<std::size_t>(patch.signalSlots)),
      envs_(static_cast<std::size_t>(patch.envCount)),
      oscs_(static_cast<std::size_t>(patch.oscCount)),
      svfs_(static_cast<std::size_t>(patch.svfCount)) {
  for (std::size_t i = 0; i < targets_.size(); ++i)
    targets_[i] = patch.descriptor.parameters[i].defaultValue;
}

void PatchVoice::prepare(double sampleRate) {
  sampleRate_ = sampleRate;
  for (std::size_t i = 0; i < smoothed_.size(); ++i)
    if (patch_.smoothing[i] > 0.0)
      smoothed_[i].prepare(sampleRate, patch_.smoothing[i], targets_[i]);
  reset();
}

void PatchVoice::silence() noexcept {
  heldCount_ = 0;
  note_ = -1;
  sustained_ = false;
  for (auto &env : envs_)
    env.kill();
  for (auto &svf : svfs_)
    svf.reset();
}

void PatchVoice::reset() noexcept {
  silence();
  sustainPedal_ = false;
  for (auto &osc : oscs_)
    osc.reset();
  for (std::size_t i = 0; i < smoothed_.size(); ++i)
    smoothed_[i].snap(targets_[i]);
}

void PatchVoice::startRelease() noexcept {
  sustained_ = false;
  for (const auto &node : patch_.plan)
    if (node.kind == BlockKind::envAdsr)
      envs_[static_cast<std::size_t>(node.state)].startRelease(
          targets_[static_cast<std::size_t>(node.params[3])], sampleRate_);
}

void PatchVoice::noteOn(int key, int velocity) noexcept {
  int j = 0;
  for (int i = 0; i < heldCount_; ++i) // a repeated key moves to the top
    if (held_[i] != key)
      held_[j++] = held_[i];
  heldCount_ = j;
  if (heldCount_ == 16) {
    std::memmove(held_, held_ + 1, sizeof(int) * 15);
    --heldCount_;
  }
  held_[heldCount_++] = key;
  if (envs_[static_cast<std::size_t>(patch_.ampEnvelopeState)].idle()) {
    for (auto &osc : oscs_) // from silence: deterministic onset
      osc.reset();
    for (auto &svf : svfs_)
      svf.reset();
    for (std::size_t i = 0; i < smoothed_.size(); ++i)
      smoothed_[i].snap(targets_[i]);
  }
  note_ = key;
  velocity_ = static_cast<double>(static_cast<float>(velocity) / 127.0f);
  sustained_ = false;
  for (auto &env : envs_)
    env.gate(); // retrigger from the current level
}

void PatchVoice::noteOff(int key) noexcept {
  int j = 0;
  for (int i = 0; i < heldCount_; ++i)
    if (held_[i] != key)
      held_[j++] = held_[i];
  heldCount_ = j;
  if (key != note_)
    return;
  if (heldCount_ > 0) {
    note_ = held_[heldCount_ - 1]; // legato back to the last held key
    return;
  }
  if (sustainPedal_)
    sustained_ = true;
  else
    startRelease();
}

void PatchVoice::allNotesOff() noexcept {
  heldCount_ = 0;
  startRelease();
}

void PatchVoice::midi(const std::uint8_t *bytes, std::uint32_t size) noexcept {
  if (size < 3)
    return; // valid but unused short messages are ignored
  const auto status = static_cast<std::uint8_t>(bytes[0] & 0xF0u);
  const int data1 = bytes[1] & 0x7F;
  const int data2 = bytes[2] & 0x7F;
  if (status == 0x90u && data2 > 0) {
    noteOn(data1, data2);
  } else if (status == 0x80u || status == 0x90u) {
    noteOff(data1);
  } else if (status == 0xB0u) {
    if (data1 == 64) {
      sustainPedal_ = data2 >= 64;
      if (!sustainPedal_ && sustained_ && heldCount_ == 0)
        startRelease();
    } else if (data1 == 120) {
      silence();
    } else if (data1 == 123) {
      allNotesOff();
    }
  }
}

double PatchVoice::next() noexcept {
  for (std::size_t i = 0; i < current_.size(); ++i)
    current_[i] = patch_.smoothing[i] > 0.0 ? smoothed_[i].next(targets_[i]) : targets_[i];
  const auto param = [this](int index) { return current_[static_cast<std::size_t>(index)]; };
  bool envelopesDone = false;
  for (const auto &node : patch_.plan) {
    if (node.kind != BlockKind::envAdsr && !envelopesDone) {
      envelopesDone = true;
      // The voice ends with its amplitude envelope; filters restart clean.
      if (envs_[static_cast<std::size_t>(patch_.ampEnvelopeState)].idle() || note_ < 0) {
        note_ = -1;
        for (auto &svf : svfs_)
          svf.reset();
        return 0.0;
      }
    }
    double &out = signals_[static_cast<std::size_t>(node.output)];
    const auto in = [&](int k) { return signals_[static_cast<std::size_t>(node.inputs[k])]; };
    switch (node.kind) {
    case BlockKind::envAdsr:
      out = envs_[static_cast<std::size_t>(node.state)].next(param(node.params[0]),
                                                           param(node.params[1]),
                                                           param(node.params[2]), sampleRate_);
      break;
    case BlockKind::oscBlep:
      out = oscs_[static_cast<std::size_t>(node.state)].next(
          static_cast<int>(param(node.params[0])), midiNoteHz(note_) / sampleRate_);
      break;
    case BlockKind::svfLowpass:
      out = svfs_[static_cast<std::size_t>(node.state)].next(in(0), param(node.params[0]),
                                                           param(node.params[1]), sampleRate_);
      break;
    case BlockKind::vca:
      out = in(0) * in(1) * velocity_;
      break;
    case BlockKind::gain:
      out = in(0) * param(node.params[0]);
      break;
    }
  }
  return signals_[static_cast<std::size_t>(patch_.outputSlot)];
}

} // namespace vstengine::dspcore
