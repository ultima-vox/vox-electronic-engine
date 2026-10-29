#include "ui/PatternGrid.h"
#include "ui/common/UiComponents.h"

namespace vstengine::ui {
namespace {
using metrics::spaceSm;
constexpr int toolbarHeight = 34;
constexpr int laneLabelWidth = 96;
constexpr int gap = 3;
const char *const timingNames[] = {"1/16", "1/8", "1/32", "1/8 T"};
} // namespace

PatternGrid::PatternGrid() : working(std::make_unique<pattern::Pattern>()) {
  setName("rack.pattern.grid");
  factory.setName("rack.pattern.factory");
  steps.setName("rack.pattern.steps");
  bars.setName("rack.pattern.bars");
  timing.setName("rack.pattern.timing");
  clear.setName("rack.pattern.clear");
  factory.setTextWhenNothingSelected("Factory pattern");
  for (auto *box : {&factory, &steps, &bars, &timing})
    addAndMakeVisible(box);
  for (std::size_t i = 0; i < barButtons.size(); ++i) {
    auto &b = barButtons[i];
    b.setName("rack.pattern.bar." + juce::String(static_cast<int>(i + 1)));
    b.setButtonText("BAR " + juce::String(static_cast<int>(i + 1)));
    b.setClickingTogglesState(false);
    styleButton(b);
    b.onClick = [this, i] { showBar(static_cast<std::uint32_t>(i)); };
    addAndMakeVisible(b);
  }
  styleButton(clear);
  addAndMakeVisible(clear);
  styleLabel(hint, 11.0f, juce::Justification::centredLeft, colours::mutedText);
  styleLabel(status, 11.0f, juce::Justification::centredRight, colours::mutedText);
  addAndMakeVisible(hint);
  addAndMakeVisible(status);

  factory.onChange = [this] {
    const int index = factory.getSelectedItemIndex();
    if (index < 0 || !onFactory)
      return;
    juce::String why;
    if (!onFactory(static_cast<std::size_t>(index), why))
      status.setText(why, juce::dontSendNotification);
    reload();
  };
  steps.onChange = [this] {
    const auto n = static_cast<std::uint32_t>(steps.getSelectedId());
    if (n == 0 || n == working->stepsPerBar)
      return;
    auto next = std::make_unique<pattern::Pattern>();
    pattern::reshape(*working, n, working->barCount, *next);
    commit(*next);
  };
  bars.onChange = [this] {
    const auto n = static_cast<std::uint32_t>(bars.getSelectedId());
    if (n == 0 || n == working->barCount)
      return;
    auto next = std::make_unique<pattern::Pattern>();
    pattern::reshape(*working, working->stepsPerBar, n, *next);
    commit(*next);
  };
  timing.onChange = [this] {
    const int id = timing.getSelectedId();
    if (id < 1 || static_cast<std::uint32_t>(id - 1) == working->timing)
      return;
    auto next = std::make_unique<pattern::Pattern>(*working);
    next->timing = static_cast<std::uint32_t>(id - 1);
    commit(*next);
  };
  clear.onClick = [this] {
    auto next = std::make_unique<pattern::Pattern>(*working);
    for (std::uint32_t lane = 0; lane < next->laneCount; ++lane)
      for (std::uint32_t step = 0; step < next->stepsPerBar; ++step)
        next->at(lane, bar, step) = pattern::defaultStep();
    commit(*next);
  };
}

void PatternGrid::setContent(std::shared_ptr<const pattern::Content> next,
                             std::string_view selectedFactory) {
  content = std::move(next);
  factory.clear(juce::dontSendNotification);
  steps.clear(juce::dontSendNotification);
  bars.clear(juce::dontSendNotification);
  timing.clear(juce::dontSendNotification);
  if (!content)
    return;
  for (std::size_t i = 0; i < content->factory.size(); ++i) {
    factory.addItem(content->factory[i].name, static_cast<int>(i + 1));
    if (content->factory[i].name == selectedFactory)
      factory.setSelectedItemIndex(static_cast<int>(i), juce::dontSendNotification);
  }
  for (std::uint32_t n = 1; n <= content->caps.maxStepsPerBar; ++n)
    steps.addItem(juce::String(static_cast<int>(n)) + " steps", static_cast<int>(n));
  for (std::uint32_t n = 1; n <= content->caps.maxBars; ++n)
    bars.addItem(juce::String(static_cast<int>(n)) + (n == 1 ? " bar" : " bars"),
                 static_cast<int>(n));
  for (int i = 0; i < 4; ++i)
    timing.addItem(timingNames[i], i + 1);
  juce::String text = "Click: step on/off";
  if (reads(VOX_FIELD_VELOCITY))
    text << "  |  drag up/down: velocity";
  if (reads(VOX_FIELD_ACCENT))
    text << "  |  shift-click: accent";
  hint.setText(text, juce::dontSendNotification);
  bar = 0;
  reload();
}

void PatternGrid::reload() {
  if (!content)
    return;
  auto current = std::make_unique<pattern::Pattern>();
  if (onReload && onReload(*current))
    *working = *current;
  if (bar >= working->barCount)
    bar = 0;
  syncControls();
  repaint();
}

bool PatternGrid::reads(std::uint32_t field) const noexcept {
  return content && (content->caps.stepFields & field) != 0;
}

void PatternGrid::syncControls() {
  steps.setSelectedId(static_cast<int>(working->stepsPerBar), juce::dontSendNotification);
  bars.setSelectedId(static_cast<int>(working->barCount), juce::dontSendNotification);
  timing.setSelectedId(static_cast<int>(working->timing) + 1, juce::dontSendNotification);
  const auto maxBars = content ? content->caps.maxBars : 0u;
  for (std::size_t i = 0; i < barButtons.size(); ++i) {
    auto &b = barButtons[i];
    b.setVisible(i < maxBars);
    b.setEnabled(i < working->barCount);
    b.setToggleState(i == bar, juce::dontSendNotification);
  }
}

void PatternGrid::showBar(std::uint32_t next) {
  if (next >= working->barCount)
    return;
  bar = next;
  syncControls();
  repaint();
}

void PatternGrid::commit(const pattern::Pattern &next) {
  juce::String why;
  if (onEdit && onEdit(next, why)) {
    status.setText({}, juce::dontSendNotification);
    factory.setSelectedItemIndex(-1, juce::dontSendNotification);
    if (content)
      for (std::size_t i = 0; i < content->factory.size(); ++i)
        if (*content->factory[i].pattern == next)
          factory.setSelectedItemIndex(static_cast<int>(i), juce::dontSendNotification);
  } else {
    status.setText(why.isEmpty() ? juce::String("Edit rejected") : why,
                   juce::dontSendNotification);
  }
  reload(); // always show what the owner holds
}

juce::Rectangle<int> PatternGrid::gridArea() const {
  auto area = getLocalBounds().reduced(spaceSm);
  area.removeFromTop(toolbarHeight * 2 + spaceSm);
  area.removeFromBottom(20);
  return area;
}

bool PatternGrid::cellAt(juce::Point<int> point, int &lane, int &step) const {
  const auto area = gridArea().withTrimmedLeft(laneLabelWidth);
  if (!content || !area.contains(point) || working->stepsPerBar == 0)
    return false;
  const auto laneHeight = area.getHeight() / static_cast<int>(working->laneCount);
  const auto stepWidth = area.getWidth() / static_cast<int>(working->stepsPerBar);
  if (laneHeight <= 0 || stepWidth <= 0)
    return false;
  lane = (point.y - area.getY()) / laneHeight;
  step = (point.x - area.getX()) / stepWidth;
  return lane >= 0 && lane < static_cast<int>(working->laneCount) && step >= 0 &&
         step < static_cast<int>(working->stepsPerBar);
}

juce::Rectangle<int> PatternGrid::cellBoundsForTesting(std::uint32_t lane,
                                                       std::uint32_t step) const {
  const auto area = gridArea().withTrimmedLeft(laneLabelWidth);
  const auto laneHeight = area.getHeight() / static_cast<int>(working->laneCount);
  const auto stepWidth = area.getWidth() / static_cast<int>(working->stepsPerBar);
  return {area.getX() + static_cast<int>(step) * stepWidth,
          area.getY() + static_cast<int>(lane) * laneHeight, stepWidth, laneHeight};
}

void PatternGrid::resized() {
  auto area = getLocalBounds().reduced(spaceSm);
  auto row = area.removeFromTop(toolbarHeight);
  factory.setBounds(row.removeFromLeft(200).reduced(2));
  row.removeFromLeft(spaceSm);
  steps.setBounds(row.removeFromLeft(104).reduced(2));
  bars.setBounds(row.removeFromLeft(90).reduced(2));
  timing.setBounds(row.removeFromLeft(80).reduced(2));
  row.removeFromLeft(spaceSm);
  clear.setBounds(row.removeFromLeft(96).reduced(2));
  status.setBounds(row.reduced(4, 0));
  area.removeFromTop(spaceSm);
  auto tabs = area.removeFromTop(toolbarHeight).withTrimmedLeft(laneLabelWidth);
  for (auto &b : barButtons)
    b.setBounds(tabs.removeFromLeft(84).reduced(2));
  hint.setBounds(getLocalBounds().reduced(spaceSm).removeFromBottom(20));
}

void PatternGrid::paint(juce::Graphics &g) {
  if (!content)
    return;
  const auto area = gridArea();
  const auto cells = area.withTrimmedLeft(laneLabelWidth);
  const auto lanes = static_cast<int>(working->laneCount);
  const auto stepsShown = static_cast<int>(working->stepsPerBar);
  const auto laneHeight = cells.getHeight() / lanes;
  const auto stepWidth = cells.getWidth() / stepsShown;
  const auto beat =
      std::max(1, static_cast<int>(std::lround(1.0 / pattern::stepLengthPpq(working->timing))));
  for (int lane = 0; lane < lanes; ++lane) {
    const auto y = cells.getY() + lane * laneHeight;
    g.setColour(colours::text);
    g.setFont(juce::FontOptions(12.0f));
    g.drawText(content->caps.laneNames[static_cast<std::size_t>(lane)], area.getX(), y,
               laneLabelWidth - spaceSm, laneHeight, juce::Justification::centredRight);
    for (int step = 0; step < stepsShown; ++step) {
      const auto &cell = working->at(static_cast<std::uint32_t>(lane), bar,
                                     static_cast<std::uint32_t>(step));
      auto r = juce::Rectangle<int>(cells.getX() + step * stepWidth, y, stepWidth, laneHeight)
                   .reduced(gap)
                   .toFloat();
      const bool downbeat = step % beat == 0;
      g.setColour(downbeat ? colours::panelRaised : colours::inactive);
      g.fillRoundedRectangle(r, 3.0f);
      if ((cell.flags & VOX_STEP_ON) != 0) {
        // Velocity is the filled height, so it reads without colour alone.
        const float level = reads(VOX_FIELD_VELOCITY) ? cell.velocity / 127.0f : 1.0f;
        auto fill = r.withTrimmedTop(r.getHeight() * (1.0f - level));
        g.setColour(colours::primary.withAlpha(0.85f));
        g.fillRoundedRectangle(fill, 3.0f);
        if ((cell.flags & VOX_STEP_ACCENT) != 0) {
          g.setColour(colours::warning);
          g.drawRoundedRectangle(r.reduced(1.0f), 3.0f, 2.0f);
          g.fillEllipse(r.getRight() - 9.0f, r.getY() + 3.0f, 6.0f, 6.0f);
        }
      }
      g.setColour(colours::border);
      g.drawRoundedRectangle(r, 3.0f, 1.0f);
    }
  }
}

void PatternGrid::mouseDown(const juce::MouseEvent &event) {
  int lane = -1, step = -1;
  dragLane = dragStep = -1;
  dragged = false;
  if (!cellAt(event.getPosition(), lane, step))
    return;
  const auto l = static_cast<std::uint32_t>(lane), s = static_cast<std::uint32_t>(step);
  const auto &cell = working->at(l, bar, s);
  const bool on = (cell.flags & VOX_STEP_ON) != 0;
  if (event.mods.isShiftDown() || event.mods.isPopupMenu()) {
    if (!on || !reads(VOX_FIELD_ACCENT))
      return;
    auto next = std::make_unique<pattern::Pattern>(*working);
    next->at(l, bar, s).flags ^= VOX_STEP_ACCENT;
    commit(*next);
    return;
  }
  if (!on) {
    auto next = std::make_unique<pattern::Pattern>(*working);
    next->at(l, bar, s).flags = VOX_STEP_ON;
    commit(*next);
    return;
  }
  // A sounding cell: drag to change velocity, or release to switch it off.
  dragLane = lane;
  dragStep = step;
  dragStartY = event.getPosition().y;
  dragStartVelocity = cell.velocity;
}

void PatternGrid::mouseDrag(const juce::MouseEvent &event) {
  if (dragLane < 0 || !reads(VOX_FIELD_VELOCITY))
    return;
  const int delta = dragStartY - event.getPosition().y;
  if (!dragged && std::abs(delta) < 4)
    return;
  dragged = true;
  const auto velocity = juce::jlimit(1, 127, dragStartVelocity + delta);
  auto next = std::make_unique<pattern::Pattern>(*working);
  auto &cell = next->at(static_cast<std::uint32_t>(dragLane), bar,
                        static_cast<std::uint32_t>(dragStep));
  if (cell.velocity == velocity)
    return;
  cell.velocity = static_cast<std::uint8_t>(velocity);
  commit(*next);
}

void PatternGrid::mouseUp(const juce::MouseEvent &) {
  if (dragLane >= 0 && !dragged) {
    auto next = std::make_unique<pattern::Pattern>(*working);
    next->at(static_cast<std::uint32_t>(dragLane), bar, static_cast<std::uint32_t>(dragStep)) =
        pattern::defaultStep();
    commit(*next);
  }
  dragLane = dragStep = -1;
  dragged = false;
}
} // namespace vstengine::ui
