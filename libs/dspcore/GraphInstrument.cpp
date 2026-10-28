#include "dspcore/GraphInstrument.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vstengine::dspcore {
namespace {
using instrument::ParameterType;

bool readString(const juce::var &v, std::string &out) {
  if (!v.isString())
    return false;
  out = v.toString().toStdString();
  return true;
}

bool readNumber(const juce::var &v, double &out) {
  if (!(v.isInt() || v.isInt64() || v.isDouble()) || v.isBool())
    return false;
  out = static_cast<double>(v);
  return std::isfinite(out);
}

// A finite number that converts to the destination without undefined behaviour.
bool readNumberIn(const juce::var &v, double lowest, double highest, double &out) {
  return readNumber(v, out) && out >= lowest && out <= highest;
}

bool readInteger(const juce::var &v, double lowest, double highest, double &out) {
  return readNumberIn(v, lowest, highest, out) && out == std::floor(out);
}

bool readStringMap(const juce::var &v, std::map<std::string, std::string> &out) {
  if (v.isVoid())
    return true;
  auto *object = v.getDynamicObject();
  if (object == nullptr)
    return false;
  for (const auto &property : object->getProperties()) {
    if (!property.value.isString())
      return false;
    out[property.name.toString().toStdString()] = property.value.toString().toStdString();
  }
  return true;
}

class Instance final : public instrument::InstrumentInstance {
public:
  explicit Instance(std::shared_ptr<const CompiledPatch> patch)
      : patch_(std::move(patch)), voice_(*patch_) {}

  bool prepare(const instrument::PrepareSpec &spec) override {
    if (!(spec.sampleRate >= 8000.0) || spec.maximumBlockSize == 0 || spec.outputChannels == 0)
      return false;
    voice_.prepare(spec.sampleRate);
    outputChannels_ = spec.outputChannels;
    return true;
  }
  void reset() noexcept override { voice_.reset(); }
  void suspend() noexcept override { suspended_ = true; }
  void resume() noexcept override { suspended_ = false; }
  void setBypassed(bool bypassed) noexcept override { bypassed_ = bypassed; }

  bool setParameter(std::string_view id, float plain) noexcept override {
    const auto &parameters = patch_->descriptor.parameters;
    for (std::size_t i = 0; i < parameters.size(); ++i) {
      const auto &p = parameters[i];
      if (p.id != id)
        continue;
      if (!std::isfinite(plain) || plain < p.minimum || plain > p.maximum ||
          (p.type != ParameterType::floating && plain != std::floor(plain)))
        return false;
      voice_.setParameter(i, plain);
      return true;
    }
    return false;
  }

  void process(const instrument::ProcessBlock &block) noexcept override {
    if (suspended_ || block.outputs.size() != outputChannels_) {
      silence(block);
      return;
    }
    std::size_t next = 0;
    for (std::uint32_t i = 0; i < block.sampleCount; ++i) {
      while (next < block.midi.size() && block.midi[next].sampleOffset <= i) {
        voice_.midi(block.midi[next].data, block.midi[next].size);
        ++next;
      }
      const auto v = static_cast<float>(voice_.next());
      for (auto *channel : block.outputs)
        channel[i] = bypassed_ ? 0.0f : v;
    }
  }

  bool loadState(std::uint32_t, std::span<const std::byte> payload) noexcept override {
    return payload.empty(); // all sound state lives in the host's named values
  }
  bool saveState(std::span<std::byte>, std::uint32_t &bytesWritten) const noexcept override {
    bytesWritten = 0;
    return true;
  }
  std::uint32_t latencySamples() const noexcept override { return 0; }
  std::uint32_t tailSamples() const noexcept override { return 0; }

private:
  static void silence(const instrument::ProcessBlock &block) noexcept {
    for (auto *channel : block.outputs)
      std::fill_n(channel, block.sampleCount, 0.0f);
  }
  std::shared_ptr<const CompiledPatch> patch_;
  PatchVoice voice_;
  std::size_t outputChannels_{};
  bool suspended_{}, bypassed_{};
};

class Provider final : public instrument::InstrumentProvider {
public:
  explicit Provider(std::shared_ptr<const CompiledPatch> patch) : patch_(std::move(patch)) {}
  std::span<const instrument::InstrumentDescriptor> descriptors() const noexcept override {
    return {&patch_->descriptor, 1};
  }
  std::unique_ptr<instrument::InstrumentInstance>
  create(std::string_view id, const instrument::CreateContext &) override {
    if (id != patch_->descriptor.id)
      return nullptr;
    return std::make_unique<Instance>(patch_);
  }

private:
  std::shared_ptr<const CompiledPatch> patch_;
};
} // namespace

bool parsePatchJson(const std::string &text, PatchSpec &out, std::string &why) {
  if (text.size() > 256 * 1024) {
    why = "graph.json larger than 256 KiB";
    return false;
  }
  juce::var json;
  const auto parsed = juce::JSON::parse(juce::String::fromUTF8(text.data(),
                                                               static_cast<int>(text.size())),
                                        json);
  if (parsed.failed() || json.getDynamicObject() == nullptr) {
    why = "graph.json is not a JSON object";
    return false;
  }
  PatchSpec spec;
  double number = 0;
  if (!readInteger(json["graphVersion"], 0, 1000000, number)) {
    why = "graphVersion must be an integer in 0..1000000";
    return false;
  }
  spec.graphVersion = static_cast<int>(number);
  const auto instrument = json["instrument"];
  if (!readString(instrument["id"], spec.instrumentId) ||
      !readString(instrument["name"], spec.name) ||
      !readString(instrument["vendor"], spec.vendor) ||
      !readInteger(instrument["version"], 1, 4294967295.0, number)) {
    why = "instrument {id, name, vendor, version} is required";
    return false;
  }
  spec.instrumentVersion = static_cast<std::uint32_t>(number);
  const auto voice = json["voice"];
  if (!readString(voice["policy"], spec.voicePolicy) ||
      !readString(voice["ampEnvelope"], spec.ampEnvelope)) {
    why = "voice {policy, ampEnvelope} is required";
    return false;
  }
  const auto *params = json["parameters"].getArray();
  const auto *nodes = json["nodes"].getArray();
  if (params == nullptr || nodes == nullptr || !readString(json["output"], spec.output)) {
    why = "parameters[], nodes[] and output are required";
    return false;
  }
  for (const auto &p : *params) {
    PatchParameter parameter;
    auto &d = parameter.descriptor;
    std::string type;
    double minimum = 0, maximum = 0, def = 0, pool = 0, smoothing = 0;
    constexpr double floatMax = std::numeric_limits<float>::max();
    if (!readString(p["id"], d.id) || !readString(p["name"], d.name) ||
        !readString(p["type"], type) || !readNumberIn(p["min"], -floatMax, floatMax, minimum) ||
        !readNumberIn(p["max"], -floatMax, floatMax, maximum) ||
        !readNumberIn(p["default"], -floatMax, floatMax, def) ||
        !readInteger(p["pool"], 0, 32767, pool)) {
      why = "parameter entries need id, name, type, min, max, default and pool";
      return false;
    }
    (void)readString(p["unit"], d.unit);
    (void)readString(p["group"], d.group);
    if (!p["smoothing"].isVoid() && !readNumber(p["smoothing"], smoothing)) {
      why = "parameter '" + d.id + "': smoothing must be seconds";
      return false;
    }
    d.type = type == "float"     ? ParameterType::floating
             : type == "int"     ? ParameterType::integer
             : type == "bool"    ? ParameterType::boolean
             : type == "choice"  ? ParameterType::choice
                                 : static_cast<ParameterType>(255);
    if (static_cast<unsigned>(d.type) == 255) {
      why = "parameter '" + d.id + "': unknown type '" + type + "'";
      return false;
    }
    if (const auto *choices = p["choices"].getArray())
      for (const auto &choice : *choices) {
        if (!choice.isString()) {
          why = "parameter '" + d.id + "': choices must be strings";
          return false;
        }
        d.choices.push_back(choice.toString().toStdString());
      }
    d.minimum = static_cast<float>(minimum);
    d.maximum = static_cast<float>(maximum);
    d.defaultValue = static_cast<float>(def);
    d.step = d.type == ParameterType::floating ? 0.0f : 1.0f;
    d.poolIndex = static_cast<std::int16_t>(pool);
    parameter.smoothingSeconds = smoothing;
    spec.parameters.push_back(std::move(parameter));
  }
  for (const auto &n : *nodes) {
    PatchNode node;
    if (!readString(n["id"], node.id) || !readString(n["type"], node.type) ||
        !readStringMap(n["params"], node.params) || !readStringMap(n["inputs"], node.inputs)) {
      why = "node entries need id, type and string maps params/inputs";
      return false;
    }
    spec.nodes.push_back(std::move(node));
  }
  out = std::move(spec);
  return true;
}

std::shared_ptr<const CompiledPatch> loadPatch(const std::string &json, std::string &why) {
  PatchSpec spec;
  if (!parsePatchJson(json, spec, why))
    return nullptr;
  auto patch = std::make_shared<CompiledPatch>();
  if (!compilePatch(spec, *patch, why))
    return nullptr;
  return patch;
}

std::unique_ptr<instrument::InstrumentProvider>
makeGraphProvider(std::shared_ptr<const CompiledPatch> patch) {
  return patch ? std::make_unique<Provider>(std::move(patch)) : nullptr;
}

} // namespace vstengine::dspcore
