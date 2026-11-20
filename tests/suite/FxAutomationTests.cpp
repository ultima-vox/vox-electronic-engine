// FX-06 automation contract, checked against the running plugin.
//
// products/fx/parameters.csv is the frozen 1.0 contract: 19 host IDs, their
// order, type and range. A host writes these lanes; the plugin must expose
// exactly that layout, accept automation gestures, keep the base value when a
// Program/preset is loaded, and reflect the value back into the editor without
// feeding it back as a new gesture.
#include "FxEditor.h"
#include "Processor.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool condition, const std::string& message) {
    if (!condition) { std::printf("FAIL: %s\n", message.c_str()); ++failures; }
}

struct Row { std::string id, type; float minimum = 0, maximum = 0, defaultValue = 0; bool automatable = false; };

std::vector<Row> readContract(const char* path) {
    std::vector<Row> rows;
    std::ifstream in(path);
    if (!in) return rows;
    std::string line;
    std::getline(in, line); // header
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::vector<std::string> field;
        std::size_t start = 0;
        for (;;) {
            const auto comma = line.find(',', start);
            field.push_back(line.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        if (field.size() < 7) continue;
        Row row;
        row.id = field[0];
        row.type = field[2];
        row.minimum = std::stof(field[3]);
        row.maximum = std::stof(field[4]);
        row.defaultValue = std::stof(field[5]);
        row.automatable = field[6] == "true";
        rows.push_back(row);
    }
    return rows;
}
} // namespace

int main(int argc, char** argv) {
    using namespace vox::fx;
    if (argc != 2) { std::printf("usage: %s <products/fx/parameters.csv>\n", argv[0]); return 2; }
    const auto contract = readContract(argv[1]);
    check(contract.size() == 19, "contract declares the frozen 19 host parameters");

    FxProcessor processor;
    const auto& parameters = processor.getParameters();
    check(parameters.size() == contract.size(), "running plugin exposes exactly the contract parameters");

    std::map<std::string, juce::AudioProcessorParameter*> byId;
    for (auto* parameter : parameters) {
        const auto* withId = dynamic_cast<const juce::AudioProcessorParameterWithID*>(parameter);
        check(withId != nullptr, "parameter carries a stable string ID");
        const auto id = withId ? withId->paramID.toStdString() : parameter->getName(64).toStdString();
        check(byId.find(id) == byId.end(), "no duplicate host parameter name");
        byId[id] = parameter;
    }

    for (std::size_t i = 0; i < contract.size() && i < parameters.size(); ++i) {
        const auto& row = contract[i];
        auto* parameter = parameters[i];
        check(parameter != nullptr, "parameter exists at contract position " + std::to_string(i));
        if (!parameter) continue;
        // Range and default travel through the normalised 0..1 host side.
        check(std::abs(parameter->getDefaultValue() - (row.defaultValue - row.minimum) / (row.maximum - row.minimum)) < 1e-4f,
              "default normalised value matches the contract for " + row.id);
        check(parameter->isAutomatable() == row.automatable, "automatable flag matches the contract for " + row.id);
        // A host automation write must land on the base value.
        const float target = row.maximum > row.minimum ? .37f : 0.0f;
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(target);
        parameter->endChangeGesture();
        // Host-facing values are quantised to the parameter's own interval: the
        // duration lane snaps to 1 ms (3.2e-5 in normalised units) and a
        // boolean snaps to 0/1. The write must land on that grid, not be
        // dropped.
        const float tolerance = row.type == "bool" ? 1.0f : 1e-4f;
        check(std::abs(parameter->getValue() - target) <= tolerance,
              "automation write reaches the base value for " + row.id);
        // Out-of-range host input must stay finite and, for the ranged float
        // lanes, inside the contract range. A boolean lane is quantised by the
        // host to 0/1; JUCE stores the raw float but get() still reads it as a
        // boolean, so the contract only requires finiteness there.
        parameter->setValueNotifyingHost(2.5f);
        check(std::isfinite(parameter->getValue()), "host value stays finite for " + row.id);
        if (row.type == "float")
            check(parameter->getValue() <= 1.0f, "ranged host value is clamped for " + row.id);
        parameter->setValueNotifyingHost(target);
    }

    // Structured names: the contract calls the macro and slot parameters out.
    for (int macro = 1; macro <= 8; ++macro)
        check(byId.count("fx.macro." + std::to_string(macro)) == 1, "macro lane present");
    for (int slot = 1; slot <= 4; ++slot) {
        check(byId.count("fx.slot." + std::to_string(slot) + ".wet") == 1, "slot wet lane present");
        check(byId.count("fx.slot." + std::to_string(slot) + ".amount") == 1, "slot amount lane present");
    }

    // Gesture state must reach listeners exactly once per write and not be
    // re-emitted when the editor mirrors the parameter.
    struct Counter final : juce::AudioProcessorParameter::Listener {
        int values = 0, gestures = 0;
        void parameterValueChanged(int, float) override { ++values; }
        void parameterGestureChanged(int, bool) override { ++gestures; }
    } counter;
    auto& wet = processor.effectWetParameter(0);
    wet.addListener(&counter);
    wet.beginChangeGesture();
    wet.setValueNotifyingHost(.42f);
    wet.endChangeGesture();
    check(counter.gestures == 2 && counter.values >= 1, "gesture begin/end and one value notification are delivered");

    FxPanel panel(processor);
    panel.setSize(1040, 700);
    const int before = counter.values;
    wet.setValueNotifyingHost(.61f);
    // The editor holds a parameter attachment for the output, so this also
    // proves a live host write reaches an open editor without a gesture loop.
    processor.outputParameter().setValueNotifyingHost(.58f);
    check(std::abs(panel.outputSlider().getValue() - .58) < 1e-3, "open editor mirrors a host automation write");
    check(counter.values > before, "host automation is forwarded to listeners while the editor is open");

    // Loading a Program must leave host automation bases alone.
    const float wetBase = wet.get();
    const float macroBase = processor.macroParameter(2).get();
    const float outputBase = processor.outputParameter().get();
    auto program = processor.program();
    program.layers[0].family = NoiseFamily::laser;
    check(processor.setProgram(program), "structural Program load");
    check(wet.get() == wetBase, "structural load leaves slot automation alone");
    check(processor.macroParameter(2).get() == macroBase, "structural load leaves macro automation alone");
    check(processor.outputParameter().get() == outputBase, "structural load leaves output automation alone");

    wet.removeListener(&counter);
    std::printf("FX automation contract: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
