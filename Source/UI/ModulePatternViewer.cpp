#include "ModulePatternViewer.h"

#include <cmath>

// ---- PatternGridComponent ----
// Renders the tracker-style pattern grid.

class PatternGridComponent : public juce::Component
{
public:
    static constexpr int rowHeight           = 14;
    static constexpr int headerHeight        = 18;
    static constexpr int rowNumWidth         = 38;
    static constexpr int defaultChannelWidth = 100;
    static constexpr int minChannelWidth     = 10;

    // Column-width tiers the cell/header painters switch on -- see paintCell().
    static constexpr int fullCellMinWidth    = 70; // note+inst+vol+effect
    static constexpr int compactCellMinWidth = 30; // note+inst only
    static constexpr int minimalCellMinWidth = 14; // note only, tiny font
    // below minimalCellMinWidth: a bare colour swatch, no text at all

    PatternGridComponent()
    {
        setSize(rowNumWidth, headerHeight + 10);
    }

    // fitWidth <= 0: fixed defaultChannelWidth columns, scrolled interactively
    // (the on-screen pattern viewer). fitWidth > 0: shrink every column so all
    // of the pattern's channels fit within fitWidth with no horizontal
    // scrolling/clipping -- used for baked video export, which renders one
    // static frame per row and has no scrollbar. Never grows a column past
    // defaultChannelWidth.
    //
    // centeredHeight > 0 switches to the centered view: the component is
    // exactly that tall and paints its rows around viewRow (see setViewRow())
    // instead of laying the whole pattern out for the viewport to scroll.
    void setPattern(const ModulePattern* pat, int patIndex, int fitWidth = -1, int centeredHeight = -1)
    {
        pattern      = pat;
        patternIndex = patIndex;
        centered     = centeredHeight > 0;

        if (pat != nullptr && pat->numChannels > 0)
        {
            channelWidth = fitWidth > 0
                ? juce::jlimit(minChannelWidth, defaultChannelWidth,
                                (fitWidth - rowNumWidth) / pat->numChannels)
                : defaultChannelWidth;
            setSize(rowNumWidth + pat->numChannels * channelWidth,
                    centered ? centeredHeight : headerHeight + pat->numRows * rowHeight);
        }
        else
        {
            channelWidth = defaultChannelWidth;
            setSize(rowNumWidth, centered ? centeredHeight : headerHeight + 10);
        }

        if (pat != nullptr)
            viewRow = juce::jlimit(0.0, (double) juce::jmax(0, pat->numRows - 1), viewRow);
        repaint();
    }

    // Patterns drawn dimmed above row 0 / below the last row in the centered
    // view (song-order neighbours); nullptr for none.
    void setGhostPatterns(const ModulePattern* before, const ModulePattern* after)
    {
        if (ghostBefore == before && ghostAfter == after) return;
        ghostBefore = before;
        ghostAfter  = after;
        if (centered) repaint();
    }

    // Page view: the playing row, kept visible by scrolling the viewport.
    void setCurrentRow(int row)
    {
        if (currentRow == row) return;
        currentRow = row;
        repaint();
        if (! centered)
            scrollToRow(row);
    }

    // Centered view: the (fractional) row on the center line. `following`
    // draws the play band there; otherwise only a faint marker line.
    void setViewRow(double row, bool following)
    {
        if (row == viewRow && following == followingPlayback) return;
        viewRow = row;
        followingPlayback = following;
        if (centered) repaint();
    }

    double getViewRow() const { return viewRow; }
    bool isFollowingPlayback() const { return followingPlayback; }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        // The centered view scrolls its own rows (while not following
        // playback); sideways scrolling still goes to the viewport.
        if (! centered || std::abs(wheel.deltaX) > std::abs(wheel.deltaY) || pattern == nullptr)
        {
            Component::mouseWheelMove(e, wheel);
            return;
        }
        if (followingPlayback) return;
        setViewRow(juce::jlimit(0.0, (double) juce::jmax(0, pattern->numRows - 1),
                                std::round(viewRow - wheel.deltaY * 16.0)),
                   false);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(bgColor);

        if (pattern == nullptr)
            return;

        if (centered)
        {
            paintCentered(g);
            return;
        }

        auto clip = g.getClipBounds();

        if (clip.getY() < headerHeight)
            paintHeader(g);

        int firstRow = juce::jmax(0, (clip.getY() - headerHeight) / rowHeight);
        int lastRow  = juce::jmin(pattern->numRows - 1,
                                  (clip.getBottom() - headerHeight) / rowHeight + 1);

        for (int r = firstRow; r <= lastRow; ++r)
            paintRow(g, r, *pattern, r, headerHeight + r * rowHeight, false);

        if (currentRow >= 0 && currentRow < pattern->numRows)
            paintPlayBand(g, headerHeight + currentRow * rowHeight, currentRow);
    }

private:
    double centerRowTop() const
    {
        return headerHeight + (getHeight() - headerHeight - rowHeight) / 2.0;
    }

    // Centered view: rows are placed relative to viewRow, so they run past
    // both ends of the pattern; those rows come from the ghost patterns.
    void paintCentered(juce::Graphics& g)
    {
        const double centerTop = centerRowTop();
        const int numRows = pattern->numRows;
        const int firstRow = (int) std::floor(viewRow - (centerTop - headerHeight) / rowHeight) - 1;
        const int lastRow  = (int) std::ceil(viewRow + (getHeight() - centerTop) / rowHeight) + 1;
        const int beforeRows = ghostBefore != nullptr ? ghostBefore->numRows : 0;
        const int afterRows  = ghostAfter  != nullptr ? ghostAfter->numRows  : 0;

        {
            juce::Graphics::ScopedSaveState state(g);
            g.reduceClipRegion(0, headerHeight, getWidth(), getHeight() - headerHeight);

            for (int r = juce::jmax(firstRow, -beforeRows); r <= juce::jmin(lastRow, numRows - 1 + afterRows); ++r)
            {
                const int y = juce::roundToInt(centerTop + (r - viewRow) * rowHeight);
                if (r < 0)
                    paintRow(g, r, *ghostBefore, beforeRows + r, y, true);
                else if (r >= numRows)
                    paintRow(g, r, *ghostAfter, r - numRows, y, true);
                else
                    paintRow(g, r, *pattern, r, y, false);
            }

            const int bandY = juce::roundToInt(centerTop);
            if (followingPlayback)
            {
                paintPlayBand(g, bandY, currentRow);
            }
            else
            {
                g.setColour(playColour.withAlpha(0.4f));
                g.drawRect(rowNumWidth, bandY, getWidth() - rowNumWidth, rowHeight + 1, 1);
            }
        }

        paintHeader(g);
    }

    // Playing row: a translucent orange band, drawn over the cells, plus a
    // highlighted row number (as exTracker draws it).
    void paintPlayBand(juce::Graphics& g, int y, int row)
    {
        g.setColour(playColour.withAlpha(0.22f));
        g.fillRect(rowNumWidth, y, getWidth() - rowNumWidth, rowHeight);
        g.setColour(playColour);
        g.drawRect(rowNumWidth, y, getWidth() - rowNumWidth, rowHeight + 1, 2);
        g.fillRect(0, y, rowNumWidth, rowHeight + 1);

        if (row >= 0)
        {
            g.setColour(juce::Colours::black);
            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::bold));
            g.drawText(juce::String(row).paddedLeft('0', 3), 2, y, rowNumWidth - 4,
                       rowHeight, juce::Justification::centredLeft);
        }
    }

    void paintHeader(juce::Graphics& g)
    {
        g.setColour(headerBgColor);
        g.fillRect(0, 0, getWidth(), headerHeight);

        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::bold));
        g.setColour(headerTextColor);

        g.drawText("Row", 2, 0, rowNumWidth - 4, headerHeight, juce::Justification::centredLeft);

        if (pattern != nullptr)
        {
            for (int c = 0; c < pattern->numChannels; ++c)
            {
                int x = rowNumWidth + c * channelWidth;

                if (channelWidth >= 44)
                    g.drawText("Ch " + juce::String(c + 1), x + 4, 0, channelWidth - 8,
                               headerHeight, juce::Justification::centredLeft);
                else if (channelWidth >= 16)
                    g.drawText(juce::String(c + 1), x + 2, 0, channelWidth - 4,
                               headerHeight, juce::Justification::centred);

                g.setColour(separatorColor);
                g.drawVerticalLine(x - 1, 0.0f, (float)headerHeight);
                g.setColour(headerTextColor);
            }
        }

        g.setColour(separatorColor.withAlpha(0.8f));
        g.drawHorizontalLine(headerHeight - 1, 0.0f, (float)getWidth());
    }

    // Paints row `shownRow` of `source` at y. `ghost` rows belong to a
    // neighbouring pattern and are dimmed; `row` is unused beyond that.
    void paintRow(juce::Graphics& g, int row, const ModulePattern& source, int shownRow, int y, bool ghost)
    {
        juce::ignoreUnused(row);
        const bool isBeat = (shownRow % 4 == 0);

        // Background
        if (isBeat)
            g.setColour(beatRowBg);
        else
            g.setColour((shownRow % 2 == 0) ? rowEvenBg : rowOddBg);
        g.fillRect(0, y, getWidth(), rowHeight);

        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain));

        // Row number
        g.setColour(rowNumColor);
        g.drawText(juce::String(shownRow).paddedLeft('0', 3), 2, y, rowNumWidth - 4,
                   rowHeight, juce::Justification::centredLeft);

        // Channel cells (a ghost pattern may have fewer channels)
        const int channels = pattern != nullptr ? juce::jmin(source.numChannels, pattern->numChannels)
                                                : source.numChannels;
        for (int c = 0; c < channels; ++c)
        {
            int x = rowNumWidth + c * channelWidth;

            g.setColour(separatorColor);
            g.drawVerticalLine(x - 1, (float)y, (float)(y + rowHeight));

            paintCell(g, source.at(shownRow, c), x, y);
        }

        if (ghost)
        {
            g.setColour(bgColor.withAlpha(0.65f));
            g.fillRect(0, y, getWidth(), rowHeight);
        }
    }

    // Column width decides how much detail a cell can show -- see the width
    // tiers declared above. High channel counts (32/64ch) shrink columns past
    // the point where all four fields are legible, so cells progressively drop
    // the effect, then volume, then instrument column rather than clipping text.
    void paintCell(juce::Graphics& g, const ModuleNote& note, int x, int y)
    {
        auto noteStr = note.getNoteString();
        bool emptyNote = (noteStr == "---");

        if (channelWidth < minimalCellMinWidth)
        {
            // No room for any text -- a colour swatch still shows the pattern's
            // shape (which channels/rows have notes) across many channels.
            g.setColour(emptyNote ? emptyColor.withAlpha(0.4f) : noteColor.withAlpha(0.85f));
            g.fillRect(x + 1, y + 2, juce::jmax(1, channelWidth - 2), rowHeight - 4);
            return;
        }

        if (channelWidth < compactCellMinWidth)
        {
            // Minimal: note only, small font.
            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 8.0f, juce::Font::plain));
            g.setColour(emptyNote ? emptyColor : noteColor);
            g.drawText(noteStr, x + 1, y, channelWidth - 2, rowHeight, juce::Justification::centred);
            return;
        }

        if (channelWidth < fullCellMinWidth)
        {
            // Compact: note + instrument, no volume/effect columns.
            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
            const int noteW = channelWidth * 6 / 10;
            g.setColour(emptyNote ? emptyColor : noteColor);
            g.drawText(noteStr, x + 2, y, noteW - 2, rowHeight, juce::Justification::centredLeft);

            auto instStr = note.getInstString();
            g.setColour(instStr == "--" ? emptyColor : instColor);
            g.drawText(instStr, x + noteW, y, juce::jmax(0, channelWidth - noteW - 2), rowHeight,
                       juce::Justification::centredLeft);
            return;
        }

        // Full: note(3 chars) inst(2 chars) vol(3 chars) effect(3 chars), positions
        // scaled from the original 100px-wide layout so wider fit-to-width columns
        // (fewer channels) still look proportioned rather than left-clipped.
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain));
        const float scale = channelWidth / (float) defaultChannelWidth;
        auto scaled = [scale](int px) { return juce::roundToInt(px * scale); };

        g.setColour(emptyNote ? emptyColor : noteColor);
        g.drawText(noteStr, x + scaled(3), y, scaled(21), rowHeight, juce::Justification::centredLeft);

        auto instStr = note.getInstString();
        g.setColour(instStr == "--" ? emptyColor : instColor);
        g.drawText(instStr, x + scaled(27), y, scaled(17), rowHeight, juce::Justification::centredLeft);

        auto volStr = note.getVolString();
        g.setColour(volStr == ".." ? emptyColor : volColor);
        g.drawText(volStr, x + scaled(44), y, scaled(24), rowHeight, juce::Justification::centredLeft);

        auto effStr = note.getEffectString();
        g.setColour(effStr == "..." ? emptyColor : effectColor);
        g.drawText(effStr, x + scaled(68), y, scaled(30), rowHeight, juce::Justification::centredLeft);
    }

    void scrollToRow(int row)
    {
        if (auto* vp = findParentComponentOfClass<juce::Viewport>())
        {
            int y         = headerHeight + row * rowHeight;
            int viewTop    = vp->getViewPositionY();
            int viewBottom = viewTop + vp->getViewHeight();

            if (y < viewTop)
                vp->setViewPosition(vp->getViewPositionX(), y);
            else if (y + rowHeight > viewBottom)
                vp->setViewPosition(vp->getViewPositionX(),
                                    y + rowHeight - vp->getViewHeight());
        }
    }

    const ModulePattern* pattern = nullptr;
    const ModulePattern* ghostBefore = nullptr;
    const ModulePattern* ghostAfter  = nullptr;
    int patternIndex = 0;
    int currentRow   = -1;
    int channelWidth = defaultChannelWidth;
    bool centered = false;
    double viewRow = 0.0;
    bool followingPlayback = false;

    // Dark tracker colour scheme
    const juce::Colour bgColor        { 0xff141420 };
    const juce::Colour headerBgColor  { 0xff0c0c18 };
    const juce::Colour headerTextColor{ 0xff8899aa };
    const juce::Colour rowEvenBg      { 0xff141420 };
    const juce::Colour rowOddBg       { 0xff181828 };
    const juce::Colour beatRowBg      { 0xff1e1e32 };
    const juce::Colour playColour     { 0xffff7a1a };
    const juce::Colour separatorColor { 0xff252540 };
    const juce::Colour rowNumColor    { 0xff556677 };
    const juce::Colour emptyColor     { 0xff303050 };
    const juce::Colour noteColor      { 0xff77ee77 };
    const juce::Colour instColor      { 0xffffcc44 };
    const juce::Colour volColor       { 0xff44ccff };
    const juce::Colour effectColor    { 0xffff9944 };
};

// ---- ModulePatternViewer ----

ModulePatternViewer::ModulePatternViewer()
{
    titleLabel = std::make_unique<juce::Label>("title", "Pattern Viewer");
    titleLabel->setColour(juce::Label::textColourId, juce::Colour(0xff99bbdd));
    titleLabel->setFont(juce::Font(13.0f, juce::Font::bold));
    addAndMakeVisible(*titleLabel);

    patternLabel = std::make_unique<juce::Label>("pat", "");
    patternLabel->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(*patternLabel);

    viewButton = std::make_unique<juce::TextButton>();
    viewButton->setTooltip("Center: the playing row stays on the middle line and the pattern scrolls under it.\n"
                           "Page: the whole pattern scrolls to keep the playing row in view.");
    viewButton->onClick = [this] { setCenteredView(! centeredView); };
    addAndMakeVisible(*viewButton);

    prevButton = std::make_unique<juce::TextButton>("<");
    prevButton->onClick = [this] { showPattern(currentPatternIndex - 1); };
    addAndMakeVisible(*prevButton);

    nextButton = std::make_unique<juce::TextButton>(">");
    nextButton->onClick = [this] { showPattern(currentPatternIndex + 1); };
    addAndMakeVisible(*nextButton);

    statusLabel = std::make_unique<juce::Label>("status", "");
    statusLabel->setJustificationType(juce::Justification::centred);
    statusLabel->setColour(juce::Label::textColourId, juce::Colour(0xff6a6a8a));
    statusLabel->setVisible(false);
    addAndMakeVisible(*statusLabel);

    grid     = std::make_unique<PatternGridComponent>();
    viewport = std::make_unique<juce::Viewport>();
    viewport->setViewedComponent(grid.get(), false);
    viewport->setScrollBarsShown(! centeredView, true);
    viewport->setVisible(false);
    addAndMakeVisible(*viewport);

    // Glides the centered view once per display frame while playing.
    vblank = std::make_unique<juce::VBlankAttachment>(this, [this] { animateFrame(); });

    updateViewButton();
    updateNavButtons();
}

ModulePatternViewer::~ModulePatternViewer() = default;

void ModulePatternViewer::setPatternData(const ModulePatternData* data)
{
    patternData         = data;
    currentPatternIndex = 0;
    gliding             = false;
    grid->setGhostPatterns(nullptr, nullptr);
    grid->setViewRow(0.0, false);

#ifdef SOUNDPLAYER_OPENMPT_SUPPORT
    if (data != nullptr && data->isLoaded() && data->getNumPatterns() > 0)
    {
        statusLabel->setVisible(false);
        viewport->setVisible(true);
        showPattern(0);
        return;
    }
    statusLabel->setText(data != nullptr ? "Failed to read pattern data."
                                         : "No module file loaded.",
                         juce::dontSendNotification);
#else
    statusLabel->setText("Module pattern viewer requires libopenmpt.\n"
                         "Install libopenmpt-dev and rebuild.",
                         juce::dontSendNotification);
    juce::ignoreUnused(data);
#endif

    grid->setPattern(nullptr, 0);
    statusLabel->setVisible(true);
    viewport->setVisible(false);
    updateNavButtons();
}

void ModulePatternViewer::setCurrentRow(int row)
{
    grid->setCurrentRow(row);
    if (! gliding)
        grid->setViewRow(row, false);
}

void ModulePatternViewer::setFitChannelsToWidth(bool shouldFit)
{
    fitChannelsToWidth = shouldFit;
    // No scrollbar needed once every channel is made to fit -- and hiding it
    // stops it from stealing width the fit calculation is counting on.
    viewport->setScrollBarsShown(! shouldFit && ! centeredView, ! shouldFit);
    refitGrid();
}

void ModulePatternViewer::setCenteredView(bool shouldCenter)
{
    if (centeredView == shouldCenter) return;
    centeredView = shouldCenter;
    viewport->setScrollBarsShown(! fitChannelsToWidth && ! centeredView, ! fitChannelsToWidth);
    viewport->setViewPosition(viewport->getViewPositionX(), 0);
    updateViewButton();
    refitGrid();
}

void ModulePatternViewer::setTopRightReserve(int width)
{
    if (topRightReserve == width) return;
    topRightReserve = width;
    resized();
}

void ModulePatternViewer::updateViewButton()
{
    viewButton->setButtonText(centeredView ? "View: Center" : "View: Page");
}

void ModulePatternViewer::refitGrid()
{
    if (patternData == nullptr || !patternData->isLoaded()) return;

    const auto* pattern = patternData->getPattern(currentPatternIndex);
    const int fitWidth = fitChannelsToWidth ? viewport->getWidth() : -1;

    int centeredHeight = -1;
    if (centeredView)
    {
        // The centered view scrolls its rows itself, so the grid is exactly as
        // tall as the viewport, less the horizontal scrollbar when it shows.
        const int width = pattern != nullptr && pattern->numChannels > 0 && fitWidth <= 0
            ? PatternGridComponent::rowNumWidth + pattern->numChannels * PatternGridComponent::defaultChannelWidth
            : 0;
        const bool horizontalBar = ! fitChannelsToWidth && width > viewport->getWidth();
        centeredHeight = juce::jmax(PatternGridComponent::headerHeight + PatternGridComponent::rowHeight,
                                    viewport->getHeight() - (horizontalBar ? viewport->getScrollBarThickness() : 0));
    }

    grid->setPattern(pattern, currentPatternIndex, fitWidth, centeredHeight);
}

void ModulePatternViewer::updateGhostPatterns(int orderPosition)
{
    if (patternData == nullptr) return;

    // Only when the order position really is the shown pattern -- not while
    // the user browses other patterns with < and >.
    const auto& order = patternData->getOrder();
    const ModulePattern* before = nullptr;
    const ModulePattern* after  = nullptr;
    if (orderPosition >= 0 && orderPosition < (int) order.size()
        && order[(size_t) orderPosition] == currentPatternIndex)
    {
        for (int o = orderPosition - 1; o >= 0 && order[(size_t) o] != ModulePatternData::kOrderEnd; --o)
            if (order[(size_t) o] >= 0) { before = patternData->getPattern(order[(size_t) o]); break; }
        for (int o = orderPosition + 1; o < (int) order.size() && order[(size_t) o] != ModulePatternData::kOrderEnd; ++o)
            if (order[(size_t) o] >= 0) { after = patternData->getPattern(order[(size_t) o]); break; }
    }
    grid->setGhostPatterns(before, after);
}

void ModulePatternViewer::setPlaybackPosition(int patternIndex, int row)
{
    if (patternIndex >= 0 && patternIndex != currentPatternIndex)
        showPattern(patternIndex);
    if (row >= 0)
        setCurrentRow(row);
}

void ModulePatternViewer::setPlaybackPosition(const PatternPlayPosition& position, bool isPlaying)
{
    if (! position.isValid())
    {
        playing = false;
        return;
    }

    const bool patternChanged = position.pattern != currentPatternIndex;
    if (patternChanged)
        showPattern(position.pattern);
    updateGhostPatterns(position.orderPosition);
    grid->setCurrentRow(position.row);

    const double measured = position.row + position.rowFraction;
    const bool moved = patternChanged || std::abs(measured - measuredRow) > 1e-6;
    playing = isPlaying;
    if (moved)
    {
        measuredRow  = measured;
        measuredAtMs = juce::Time::getMillisecondCounterHiRes();
    }
    if (position.rowSeconds > 0.0)
        rowSeconds = position.rowSeconds;

    if (! playing)
    {
        // Stopped/paused: rest on the whole row; leave a wheel-scrolled view
        // alone until playback moves.
        gliding = false;
        if (moved || grid->isFollowingPlayback())
            grid->setViewRow(position.row, false);
    }
}

void ModulePatternViewer::setExactPlaybackPosition(const PatternPlayPosition& position)
{
    if (! position.isValid()) return;
    if (position.pattern != currentPatternIndex)
        showPattern(position.pattern);
    updateGhostPatterns(position.orderPosition);
    grid->setCurrentRow(position.row);
    grid->setViewRow(position.row + position.rowFraction, true);
}

void ModulePatternViewer::animateFrame()
{
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const double frameMs = nowMs - lastFrameMs;
    lastFrameMs = nowMs;

    if (! playing || ! centeredView || ! isShowing())
    {
        gliding = false;
        return;
    }

    // Where playback is now: the last poll plus the time since, at the row
    // length (capped so a stalled poll can't run the view far ahead).
    const double rowMs = rowSeconds > 0.0 ? rowSeconds * 1000.0 : 0.0;
    const double ahead = rowMs > 0.0 ? juce::jmin((nowMs - measuredAtMs) / rowMs, 1.5) : 0.0;
    const double target = measuredRow + ahead;

    double viewRow = grid->getViewRow();
    if (! gliding)
    {
        gliding = true;
        viewRow = target;
    }
    else
    {
        // Glide at the tempo and pull gently toward the measured position, so
        // polls that land a little late don't make the rows stutter. Jumps
        // (pattern change, seek, position-jump effects) snap.
        const double predicted = viewRow + (rowMs > 0.0 ? frameMs / rowMs : 0.0);
        const double error = target - predicted;
        viewRow = std::abs(error) > 2.0 ? target : predicted + error * 0.2;
    }
    grid->setViewRow(viewRow, true);
}

void ModulePatternViewer::showPattern(int index)
{
    if (patternData == nullptr || !patternData->isLoaded()) return;

    int numPats = patternData->getNumPatterns();
    if (numPats == 0) return;

    currentPatternIndex = juce::jlimit(0, numPats - 1, index);
    grid->setGhostPatterns(nullptr, nullptr);
    refitGrid();

    auto title = patternData->getTitle();
    titleLabel->setText(title.isNotEmpty() ? title : "Pattern Viewer",
                        juce::dontSendNotification);
    patternLabel->setText("Pattern: " + juce::String(currentPatternIndex + 1)
                          + " / " + juce::String(numPats),
                          juce::dontSendNotification);
    updateNavButtons();
}

void ModulePatternViewer::updateNavButtons()
{
    bool hasData = patternData != nullptr
                   && patternData->isLoaded()
                   && patternData->getNumPatterns() > 0;

    prevButton->setEnabled(hasData && currentPatternIndex > 0);
    nextButton->setEnabled(hasData && currentPatternIndex < patternData->getNumPatterns() - 1);
}

void ModulePatternViewer::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0e0e1a));

    g.setColour(juce::Colour(0xff252540));
    g.drawRect(getLocalBounds(), 1);
}

void ModulePatternViewer::resized()
{
    constexpr int barH = 28;
    auto area = getLocalBounds().reduced(1);

    auto bar = area.removeFromTop(barH).reduced(4, 3);
    bar.removeFromRight(topRightReserve);
    nextButton->setBounds(bar.removeFromRight(26));
    bar.removeFromRight(2);
    prevButton->setBounds(bar.removeFromRight(26));
    bar.removeFromRight(6);
    patternLabel->setBounds(bar.removeFromRight(110));
    bar.removeFromRight(4);
    viewButton->setBounds(bar.removeFromRight(96));
    bar.removeFromRight(4);
    titleLabel->setBounds(bar);

    viewport->setBounds(area);
    statusLabel->setBounds(area);

    refitGrid();
}
