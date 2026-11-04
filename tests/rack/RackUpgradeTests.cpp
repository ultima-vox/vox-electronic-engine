// The production upgrade of a released instrument (owner decision, #78 §4)
// and the signed identity it depends on.
#include "rack/Rack.h"
#include "rack/RackMigration.h"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {
using namespace vstengine;
constexpr const char *kitId = "com.ultimavox.test.upgrade";
constexpr const char *signer = "0123456789abcdef";
int checks{};
void require(bool ok, const std::string &message) {
  ++checks;
  if (!ok) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

instrument::ParameterDescriptor parameter(const char *id, std::int16_t pool) {
  instrument::ParameterDescriptor p;
  p.id = id;
  p.name = id;
  p.minimum = 0;
  p.maximum = 1;
  p.defaultValue = 0.5f;
  p.step = 0.01f;
  p.poolIndex = pool;
  return p;
}

// Version 2 adds "drive" and declares the upgrade from version 1.
instrument::InstrumentDescriptor version(std::uint32_t v, std::uint32_t upgradeFrom = 1,
                                         const char *publisher = signer) {
  instrument::InstrumentDescriptor d;
  d.id = kitId;
  d.providerId = "test.upgrade";
  d.name = "Upgrade";
  d.vendor = "Tests";
  d.directParameters = true;
  d.instrumentVersion = v;
  d.upgradeFromVersion = v > 1 ? upgradeFrom : 0;
  d.publisherId = publisher;
  d.tailPolicy = instrument::TailPolicy::none;
  d.budget = {1, 2048, 64, 0, 0, 0, 0, 0, 4096};
  d.parameters = {parameter("gain", 0), parameter("tone", 1)};
  if (v > 1)
    d.parameters.push_back(parameter("drive", 2));
  return d;
}

rack::PersistentSlotState savedBy(const instrument::InstrumentDescriptor &d) {
  rack::PersistentSlotState s;
  s.slotId = instrument::initialSlotId(0);
  s.instrumentId = d.id;
  s.resolvedProviderId = d.providerId;
  s.publisherId = d.publisherId;
  s.instrumentVersion = d.instrumentVersion;
  s.instrumentStateVersion = d.stateVersion;
  s.contentVersion = d.contentVersion;
  for (const auto &p : d.parameters)
    s.directParameters.push_back(
        {p.id, 0.25f, p.poolIndex, instrument::hostparams::parameterSpecHash(p)});
  return s;
}

bool same(const rack::PersistentSlotState &a, const rack::PersistentSlotState &b) {
  if (a.instrumentVersion != b.instrumentVersion ||
      a.directParameters.size() != b.directParameters.size())
    return false;
  for (std::size_t i = 0; i < a.directParameters.size(); ++i)
    if (a.directParameters[i].id != b.directParameters[i].id ||
        a.directParameters[i].value != b.directParameters[i].value ||
        a.directParameters[i].poolIndex != b.directParameters[i].poolIndex ||
        a.directParameters[i].spec != b.directParameters[i].spec)
      return false;
  return true;
}

void rejects(rack::PersistentSlotState s, const instrument::InstrumentDescriptor &current,
             const std::string &why) {
  const auto before = s;
  require(rack::migrateAppendOnly(s, current) == rack::DirectMigration::incompatible &&
              same(s, before),
          why + " (state untouched)");
}

void upgrades() {
  const auto v1 = version(1), v2 = version(2);
  auto s = savedBy(v1);
  require(rack::migrateAppendOnly(s, v2) == rack::DirectMigration::migrated &&
              s.instrumentVersion == 2 && s.directParameters.size() == 3,
          "a declared same-publisher upgrade appends the new parameter");
  require(s.directParameters[0].value == 0.25f && s.directParameters[2].id == "drive" &&
              s.directParameters[2].value == 0.5f && s.directParameters[2].poolIndex == 2 &&
              s.directParameters[2].spec ==
                  instrument::hostparams::parameterSpecHash(v2.parameters[2]),
          "saved values stay, the new one takes its default, pool and meaning");
  auto again = s;
  require(rack::migrateAppendOnly(again, v2) == rack::DirectMigration::notNeeded,
          "the same version needs no upgrade");

  rejects(savedBy(v1), version(2, 0), "an undeclared upgrade is rejected");
  rejects(savedBy(version(1)), version(3, 2), "a version below the declared window");
  rejects(savedBy(version(3, 2)), version(2), "a future version");
  rejects(savedBy(version(1, 0, "fedcba9876543210")), v2, "another publisher");
  rejects(savedBy(version(1, 0, "")), v2, "an unsigned save of a signed instrument");
  auto changed = v2;
  changed.parameters[0].maximum = 2.0f; // gain still accepts 0.25
  rejects(savedBy(v1), changed, "a changed range (meaning) of an existing parameter");
  changed = v2;
  changed.parameters[1].unit = "Hz";
  rejects(savedBy(v1), changed, "a changed unit");
  changed = v2;
  changed.parameters[0].poolIndex = 7;
  rejects(savedBy(v1), changed, "a moved pool index");
  changed = v2;
  changed.parameters.erase(changed.parameters.begin() + 1);
  rejects(savedBy(v1), changed, "a removed parameter");
  auto unspec = savedBy(v1);
  unspec.directParameters[0].spec = 0;
  rejects(unspec, v2, "a value without its recorded meaning");
  auto unpooled = savedBy(v1);
  unpooled.directParameters[1].poolIndex = -1;
  rejects(unpooled, v2, "a value without its recorded pool index");
  auto duplicate = savedBy(v1);
  duplicate.directParameters.push_back(duplicate.directParameters[0]);
  rejects(duplicate, v2, "a duplicated id");
  auto content = savedBy(v1);
  content.contentVersion = 9;
  rejects(content, v2, "another content version");

  // Display-only changes (name, default) keep the meaning.
  changed = v2;
  changed.parameters[0].name = "Level";
  changed.parameters[0].defaultValue = 0.1f;
  auto display = savedBy(v1);
  require(rack::migrateAppendOnly(display, changed) == rack::DirectMigration::migrated,
          "a renamed label or new default is not a change of meaning");
}

class Instance final : public instrument::InstrumentInstance {
public:
  bool prepare(const instrument::PrepareSpec &) override { return true; }
  void reset() noexcept override {}
  void suspend() noexcept override {}
  void resume() noexcept override {}
  void setBypassed(bool) noexcept override {}
  void process(const instrument::ProcessBlock &block) noexcept override {
    for (auto *c : block.outputs)
      std::fill_n(c, block.sampleCount, 0.0f);
  }
  bool setParameter(std::string_view, float) noexcept override { return true; }
  bool loadState(std::uint32_t schema, std::span<const std::byte> payload) noexcept override {
    return schema == instrument::stateSchemaVersion && payload.empty();
  }
  bool saveState(std::span<std::byte>, std::uint32_t &bytes) const noexcept override {
    bytes = 0;
    return true;
  }
  std::uint32_t latencySamples() const noexcept override { return 0; }
  std::uint32_t tailSamples() const noexcept override { return 0; }
};

class Provider final : public instrument::InstrumentProvider {
public:
  explicit Provider(instrument::InstrumentDescriptor d) : d_(std::move(d)) {}
  std::span<const instrument::InstrumentDescriptor> descriptors() const noexcept override {
    return {&d_, 1};
  }
  std::unique_ptr<instrument::InstrumentInstance>
  create(std::string_view id, const instrument::CreateContext &) override {
    return id == d_.id ? std::make_unique<Instance>() : nullptr;
  }

private:
  instrument::InstrumentDescriptor d_;
};

// The same InstrumentId from another signer never loads into a saved slot.
void signedIdentity() {
  std::string diagnostic;
  instrument::InstrumentRegistry original;
  require(original.registerProvider(std::make_unique<Provider>(version(1)), diagnostic),
          "signed provider registers: " + diagnostic);
  rack::Rack a(original);
  require(a.prepare({48000.0, 64, 2}) && a.loadModule(0, kitId, nullptr, diagnostic),
          "signed instrument loads: " + diagnostic);
  const auto saved = a.snapshotState();
  require(saved[0].publisherId == signer, "the slot records the package signer");

  instrument::InstrumentRegistry impostor;
  require(impostor.registerProvider(std::make_unique<Provider>(version(1, 0, "fedcba9876543210")),
                                    diagnostic),
          "second provider registers");
  rack::Rack b(impostor);
  require(b.prepare({48000.0, 64, 2}) && b.replaceState(saved, nullptr, diagnostic),
          "the project opens");
  require(b.runtimeState()[0].resolution == instrument::ResolutionStatus::incompatibleSchema &&
              b.descriptor(0) == nullptr && b.state()[0].publisherId == signer,
          "another signer's instrument stays unresolved; the saved identity is kept");

  rack::Rack c(original);
  require(c.prepare({48000.0, 64, 2}) && c.replaceState(saved, nullptr, diagnostic) &&
              c.descriptor(0) != nullptr,
          "the original signer's instrument resolves again");

  // Same version, changed meaning: a provider error, the state is rejected.
  auto changed = version(1);
  changed.parameters[0].step = 0.1f;
  instrument::InstrumentRegistry drift;
  require(drift.registerProvider(std::make_unique<Provider>(changed), diagnostic), "drift registers");
  rack::Rack d(drift);
  require(d.prepare({48000.0, 64, 2}) && !d.replaceState(saved, nullptr, diagnostic) &&
              diagnostic.find("meaning") != std::string::npos,
          "a changed parameter meaning without a version bump rejects the state");
}
} // namespace

int main() {
  upgrades();
  signedIdentity();
  std::cout << "Rack upgrade tests passed (" << checks << ")\n";
  return 0;
}
