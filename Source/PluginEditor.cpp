// Crystal Voice – Oberfläche im Stil eines Studio-Magnettonschranks der 1960er
// (Hammerschlaglack, Bakelit-Module, Drehspulinstrumente, Rändelschrauben).
#include "PluginEditor.h"
#include "BinaryData.h"

namespace cvui {

static constexpr int W = 940, H = 640;

// Lack und Bauteile
const juce::Colour cabinet(0xff9aa6ae), panelCol(0xffaeb9c1), engrave(0xff22262a);
const juce::Colour bakelite(0xff111111), print(0xfff1efe8);
const juce::Colour chromeHi(0xfff6f6f2), chromeMid(0xffc3c5c4), chromeLo(0xff70736f);
const juce::Colour face(0xfff5f2ea), needleCol(0xff141414), redZone(0xffc8261c);

namespace L {
const juce::Rectangle<float> panelA(16, 88, 908, 182), panelB(16, 276, 908, 244), panelC(16, 526, 908, 100);
const juce::Rectangle<float> logo(66, 110, 128, 104);
const juce::Rectangle<float> meters(222, 102, 480, 152), lamps(726, 102, 134, 152);
const juce::Rectangle<float> modVocal(66, 290, 112, 216), modInstr(192, 290, 112, 216), modSync(326, 290, 164, 216),
    modMic(512, 290, 196, 216), modPres(730, 290, 130, 216);
const juce::Rectangle<float> modData(66, 538, 794, 76);
const juce::Rectangle<float> card(470, 556, 286, 42);
}

// ---------------------------------------------------------------------------------- Assets

static float hash2(int x, int y) {
    uint32_t h = (uint32_t) x * 374761393u + (uint32_t) y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float) ((h ^ (h >> 16)) & 0xffffff) / 16777215.0f;
}

static float vnoise(float x, float y) {
    const int xi = (int) std::floor(x), yi = (int) std::floor(y);
    float fx = x - (float) xi, fy = y - (float) yi;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    const float a = hash2(xi, yi), b = hash2(xi + 1, yi), c = hash2(xi, yi + 1), d = hash2(xi + 1, yi + 1);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

Assets::Assets() {
    semi = juce::Typeface::createSystemTypefaceFor(BinaryData::BarlowCondensedSemiBold_ttf, BinaryData::BarlowCondensedSemiBold_ttfSize);
    bold = juce::Typeface::createSystemTypefaceFor(BinaryData::BarlowCondensedBold_ttf, BinaryData::BarlowCondensedBold_ttfSize);
    logo = juce::ImageCache::getFromMemory(BinaryData::logo_print_png, BinaryData::logo_print_pngSize);

    // Hammerschlag: Mulden (mittlere Skala) + feine Körnung + vereinzelte Poren
    const int w = W * 2, h = H * 2;
    hammer = juce::Image(juce::Image::ARGB, w, h, true);
    juce::Image::BitmapData bd(hammer, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float fx = (float) x, fy = (float) y;
            float n = 0.45f * vnoise(fx / 4.0f, fy / 4.0f) + 0.35f * vnoise(fx / 1.7f + 17.0f, fy / 1.7f)
                      + 0.20f * vnoise(fx / 14.0f, fy / 14.0f + 9.0f);
            n = std::tanh((n - 0.5f) * 3.2f);
            if (hash2(x * 7 + 3, y * 5 + 1) < 0.002f) n -= 0.6f;
            const float a = juce::jlimit(0.0f, 1.0f, std::abs(n) * 0.11f);
            const auto c = (n > 0 ? juce::Colours::white : juce::Colours::black).withAlpha(a);
            bd.setPixelColour(x, y, c);
        }
}

juce::Font Assets::font(float h, bool b) const {
    return juce::Font(juce::FontOptions(b ? bold : semi).withHeight(h).withKerningFactor(0.05f));
}

// ---------------------------------------------------------------------------------- Zeichenhilfen

static void shadow(juce::Graphics& g, const juce::Path& p, int radius, juce::Point<int> off, float alpha) {
    juce::DropShadow(juce::Colours::black.withAlpha(alpha), radius, off).drawForPath(g, p);
}

static void text(juce::Graphics& g, const juce::Font& f, const juce::String& t, juce::Rectangle<float> r,
                 juce::Colour c, juce::Justification j = juce::Justification::centred) {
    g.setFont(f);
    g.setColour(c);
    g.drawText(t, r, j, false);
}

// Gravur in den Lack: dunkle Schrift mit heller Unterkante
static void engraved(juce::Graphics& g, const juce::Font& f, const juce::String& t, juce::Rectangle<float> r,
                     juce::Justification j = juce::Justification::centred) {
    text(g, f, t, r.translated(0.0f, 1.0f), juce::Colours::white.withAlpha(0.45f), j);
    text(g, f, t, r, engrave, j);
}

// Schlitzschraube mit flach gewölbtem, vernickeltem Kopf; Schlitzwinkel zufällig aber fest
static void screw(juce::Graphics& g, float cx, float cy, float r = 9.0f) {
    juce::Path p;
    p.addEllipse(cx - r, cy - r, 2 * r, 2 * r);
    shadow(g, p, 5, { 3, 4 }, 0.55f);
    juce::ColourGradient cg(juce::Colour(0xffe9e9e4), cx - r * 0.5f, cy - r * 0.6f, juce::Colour(0xff7d7f7b), cx + r * 0.8f,
                            cy + r * 0.9f, true);
    cg.addColour(0.6, juce::Colour(0xffb4b6b2));
    g.setGradientFill(cg);
    g.fillPath(p);
    g.setColour(juce::Colour(0xff55574f));
    g.strokePath(p, juce::PathStrokeType(0.9f));
    // Schlitz
    const float an = hash2((int) (cx * 3.0f), (int) (cy * 7.0f)) * juce::MathConstants<float>::pi;
    const float dx = std::cos(an) * r * 0.86f, dy = std::sin(an) * r * 0.86f;
    g.setColour(juce::Colours::white.withAlpha(0.55f));
    g.drawLine(cx - dx + 0.6f, cy - dy + 0.9f, cx + dx + 0.6f, cy + dy + 0.9f, r * 0.26f);
    g.setColour(juce::Colour(0xff2b2c29));
    g.drawLine(cx - dx, cy - dy, cx + dx, cy + dy, r * 0.24f);
    // Glanz, durch den Schlitz unterbrochen wirkt er realistischer schwach
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    g.fillEllipse(cx - r * 0.62f, cy - r * 0.68f, r * 0.5f, r * 0.3f);
}

// Siebdruck auf dem Lack (wird vor der Hammerschlagstruktur gezeichnet)
const juce::Colour ink(0xff16181b);

// Schwarze Einlage hinter einem Ausschnitt im Blech: Innenschatten oben/links, Schnittkante
static void module(juce::Graphics& g, juce::Rectangle<float> r, float corner = 9.0f) {
    juce::Path hole;
    hole.addRoundedRectangle(r, corner);
    juce::ColourGradient cg(juce::Colour(0xff121212), r.getX(), r.getY(), juce::Colour(0xff0a0a0a), r.getX(), r.getBottom(), false);
    g.setGradientFill(cg);
    g.fillPath(hole);
    // matter Halbglanz der Einlage
    juce::ColourGradient sheen(juce::Colours::white.withAlpha(0.035f), r.getX(), r.getY(), juce::Colours::transparentWhite,
                               r.getX() + r.getWidth() * 0.6f, r.getBottom(), false);
    g.setGradientFill(sheen);
    g.fillPath(hole);
    // Innenschatten: das Blech liegt davor
    {
        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(hole);
        juce::Path frame;
        frame.addRectangle(r.expanded(40.0f));
        frame.addRoundedRectangle(r, corner);
        frame.setUsingNonZeroWinding(false);
        juce::DropShadow(juce::Colours::black.withAlpha(0.95f), 9, { 2, 4 }).drawForPath(g, frame);
    }
    // Schnittkante des Blechs: oben/links im Schatten, unten/rechts angestrahlt
    juce::ColourGradient eg(juce::Colours::black.withAlpha(0.55f), r.getX(), r.getY(), juce::Colours::white.withAlpha(0.65f),
                            r.getRight(), r.getBottom(), false);
    g.setGradientFill(eg);
    g.strokePath(hole, juce::PathStrokeType(1.4f));
}

// Zählwerk (Ziffernrollen hinter Fenster)
static void counter(juce::Graphics& g, const Assets& a, juce::Point<float> c, const juce::String& t) {
    int cells = 0;
    for (auto ch : t) if (ch != '.') ++cells;
    const float cw = 13.0f, ch = 20.0f, wTot = cw * (float) cells;
    const juce::Rectangle<float> win(c.x - wTot * 0.5f, c.y - ch * 0.5f, wTot, ch);
    g.setColour(juce::Colour(0xff3a3a3a));
    g.fillRoundedRectangle(win.expanded(3.0f), 3.0f);
    g.setColour(juce::Colour(0xff050505));
    g.fillRoundedRectangle(win.expanded(1.5f), 2.0f);
    float x = win.getX();
    for (auto chr : t) {
        if (chr == '.') {
            g.setColour(needleCol);
            g.fillEllipse(x - 2.0f, win.getBottom() - 5.5f, 3.2f, 3.2f);
            continue;
        }
        const juce::Rectangle<float> cell(x + 0.6f, win.getY(), cw - 1.2f, ch);
        juce::ColourGradient dg(juce::Colour(0xff8d8a82), 0.0f, cell.getY(), juce::Colour(0xff8d8a82), 0.0f, cell.getBottom(), false);
        dg.addColour(0.35, face);
        dg.addColour(0.65, face);
        g.setGradientFill(dg);
        g.fillRect(cell);
        text(g, a.font(16.0f), juce::String::charToString(chr), cell.translated(0.0f, 0.5f), needleCol);
        x += cw;
    }
    juce::ColourGradient gl(juce::Colours::white.withAlpha(0.22f), 0.0f, win.getY(), juce::Colours::transparentWhite, 0.0f,
                            win.getCentreY(), false);
    g.setGradientFill(gl);
    g.fillRect(win);
}

// Signallampe mit Glaskalotte in Chromfassung
static void lamp(juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, bool on) {
    juce::Path ring;
    ring.addEllipse(c.x - r - 4.0f, c.y - r - 4.0f, 2 * r + 8.0f, 2 * r + 8.0f);
    shadow(g, ring, 5, { 2, 3 }, 0.5f);
    juce::ColourGradient rg(chromeHi, c.x - r, c.y - r, chromeLo, c.x + r, c.y + r, false);
    g.setGradientFill(rg);
    g.fillPath(ring);
    if (on) {
        for (int i = 4; i > 0; --i) {
            const float gr = r + 4.0f + (float) i * 4.0f;
            g.setColour(col.withAlpha(0.06f));
            g.fillEllipse(c.x - gr, c.y - gr, 2 * gr, 2 * gr);
        }
    }
    juce::ColourGradient lg(on ? col.brighter(0.9f) : col.darker(2.2f), c.x - r * 0.2f, c.y - r * 0.25f,
                            on ? col.darker(0.4f) : col.darker(3.5f), c.x + r, c.y + r, true);
    g.setGradientFill(lg);
    g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
    // Facetten
    g.setColour(juce::Colours::black.withAlpha(on ? 0.10f : 0.25f));
    for (int i = 0; i < 6; ++i) {
        const float an = juce::MathConstants<float>::pi / 3.0f * (float) i;
        g.drawLine(c.x, c.y, c.x + r * std::cos(an), c.y + r * std::sin(an), 0.6f);
    }
    g.setColour(juce::Colours::white.withAlpha(on ? 0.85f : 0.35f));
    g.fillEllipse(c.x - r * 0.55f, c.y - r * 0.62f, r * 0.55f, r * 0.38f);
}

// Drehspulinstrument
struct Scale {
    juce::String unit;
    juce::Array<std::pair<float, juce::String>> marks; // Stellung 0..1, Beschriftung
    float redFrom = 2.0f;                               // ab hier rote Skala (0..1)
};

static void meter(juce::Graphics& g, const Assets& a, juce::Rectangle<float> r, float pos, const Scale& sc) {
    // Gehäuse
    juce::Path bez;
    bez.addRoundedRectangle(r, 5.0f);
    g.setColour(juce::Colour(0xff050505));
    g.fillPath(bez);
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.strokePath(bez, juce::PathStrokeType(1.0f));

    const auto f = r.reduced(9.0f, 8.0f);
    juce::ColourGradient fg(face, f.getX(), f.getY(), face.darker(0.08f), f.getX(), f.getBottom(), false);
    g.setGradientFill(fg);
    g.fillRect(f);

    const juce::Point<float> pv(f.getCentreX(), f.getBottom() - 16.0f);
    const float R = f.getHeight() - 30.0f;  // Skalenbogen
    const float span = juce::degreesToRadians(48.0f);
    auto at = [&](float p, float rad) {
        const float an = -span + 2.0f * span * p;
        return juce::Point<float>(pv.x + rad * std::sin(an), pv.y - rad * std::cos(an));
    };

    // Skalenbogen
    juce::Path arc;
    arc.addCentredArc(pv.x, pv.y, R, R, 0.0f, -span, span, true);
    g.setColour(needleCol);
    g.strokePath(arc, juce::PathStrokeType(1.0f));
    if (sc.redFrom < 1.0f) {
        juce::Path red;
        const float a0 = -span + 2.0f * span * sc.redFrom;
        red.addCentredArc(pv.x, pv.y, R + 2.5f, R + 2.5f, 0.0f, a0, span, true);
        g.setColour(redZone);
        g.strokePath(red, juce::PathStrokeType(4.0f));
    }
    // Teilstriche
    for (int i = 0; i <= 40; ++i) {
        const float p = (float) i / 40.0f;
        g.setColour(p >= sc.redFrom ? redZone : needleCol);
        g.drawLine({ at(p, R), at(p, R - 3.0f) }, 0.7f);
    }
    for (auto& m : sc.marks) {
        g.setColour(m.first >= sc.redFrom ? redZone : needleCol);
        g.drawLine({ at(m.first, R + 1.0f), at(m.first, R - 7.0f) }, 1.3f);
        const auto tp = at(m.first, R + 9.0f);
        text(g, a.font(10.5f, false), m.second, { tp.x - 14.0f, tp.y - 6.0f, 28.0f, 12.0f },
             m.first >= sc.redFrom ? redZone : needleCol);
    }
    text(g, a.font(15.0f, false), sc.unit, { pv.x - 30.0f, pv.y - R * 0.55f, 60.0f, 16.0f }, needleCol);

    // Zeiger mit Gegengewicht
    const float p = juce::jlimit(-0.02f, 1.03f, pos);
    const auto tip = at(p, R + 2.0f), tail = at(p, -10.0f);
    g.setColour(juce::Colours::black.withAlpha(0.18f));
    g.drawLine({ tail.translated(2.0f, 2.5f), tip.translated(2.0f, 2.5f) }, 1.4f);
    g.setColour(needleCol);
    g.drawLine({ tail, tip }, 1.4f);

    // Lagerabdeckung mit Messingschraube (wie am Vorbild)
    g.setColour(juce::Colour(0xff0c0c0c));
    g.fillEllipse(pv.x - 9.0f, pv.y - 9.0f, 18.0f, 18.0f);
    juce::ColourGradient br(juce::Colour(0xffe6c56c), pv.x - 3.0f, pv.y - 3.0f, juce::Colour(0xff7a5a1c), pv.x + 3.0f, pv.y + 3.0f, false);
    g.setGradientFill(br);
    g.fillEllipse(pv.x - 4.0f, pv.y - 4.0f, 8.0f, 8.0f);
    g.setColour(juce::Colour(0xff3a2a08));
    g.drawLine(pv.x - 3.0f, pv.y + 1.0f, pv.x + 3.0f, pv.y - 1.0f, 0.9f);
    // Nullpunkteinsteller
    g.setColour(juce::Colour(0xff0c0c0c));
    g.fillEllipse(f.getRight() - 16.0f, f.getBottom() - 15.0f, 9.0f, 9.0f);

    // Glas: Schatten der Oberkante und Reflex
    juce::ColourGradient sh(juce::Colours::black.withAlpha(0.28f), 0.0f, f.getY(), juce::Colours::transparentBlack, 0.0f,
                            f.getY() + 10.0f, false);
    g.setGradientFill(sh);
    g.fillRect(f);
    juce::Path refl;
    refl.startNewSubPath(f.getX(), f.getY());
    refl.lineTo(f.getX() + f.getWidth() * 0.55f, f.getY());
    refl.lineTo(f.getX() + f.getWidth() * 0.18f, f.getBottom());
    refl.lineTo(f.getX(), f.getBottom());
    refl.closeSubPath();
    g.setColour(juce::Colours::white.withAlpha(0.10f));
    g.fillPath(refl);
}

// Bakelit-Zeigerknopf (Hühnerkopf), Winkel in Bogenmaß ab oben im Uhrzeigersinn
static void pointerKnob(juce::Graphics& g, juce::Point<float> c, float r, float angle) {
    juce::Path k;
    k.addEllipse(-r, -r, 2 * r, 2 * r);
    juce::Path ptr;
    ptr.startNewSubPath(-r * 0.62f, 0.0f);
    ptr.lineTo(-r * 0.24f, -r * 1.85f);
    ptr.quadraticTo(0.0f, -r * 2.0f, r * 0.24f, -r * 1.85f);
    ptr.lineTo(r * 0.62f, 0.0f);
    ptr.lineTo(r * 0.5f, r * 0.95f);
    ptr.lineTo(-r * 0.5f, r * 0.95f);
    ptr.closeSubPath();
    k.addPath(ptr);
    k.setUsingNonZeroWinding(true);
    const auto tf = juce::AffineTransform::rotation(angle).translated(c);
    k.applyTransform(tf);

    shadow(g, k, 8, { 4, 6 }, 0.6f);
    juce::ColourGradient cg(juce::Colour(0xff3c3c3c), c.x - r, c.y - r, juce::Colour(0xff050505), c.x + r, c.y + r * 1.2f, false);
    g.setGradientFill(cg);
    g.fillPath(k);
    g.setColour(juce::Colours::white.withAlpha(0.18f));
    g.strokePath(k, juce::PathStrokeType(0.9f));
    // Kuppe
    juce::ColourGradient dg(juce::Colour(0xff4a4a4a), c.x - r * 0.5f, c.y - r * 0.5f, juce::Colour(0xff0a0a0a), c.x + r * 0.6f,
                            c.y + r * 0.6f, true);
    g.setGradientFill(dg);
    g.fillEllipse(c.x - r * 0.62f, c.y - r * 0.62f, r * 1.24f, r * 1.24f);
    // eingelegte Zeigerlinie
    const juce::Point<float> a(c.x + r * 0.7f * std::sin(angle), c.y - r * 0.7f * std::cos(angle));
    const juce::Point<float> e(c.x + r * 1.8f * std::sin(angle), c.y - r * 1.8f * std::cos(angle));
    g.setColour(print);
    g.drawLine({ a, e }, 2.2f);
}

// ---------------------------------------------------------------------------------- Drehschalter

RotarySwitch::RotarySwitch(juce::StringArray l, juce::Array<float> a) : labels(std::move(l)), angles(std::move(a)) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

juce::Point<float> RotarySwitch::centre() const {
    return { (float) getWidth() * 0.5f, (float) getHeight() * 0.60f };
}

void RotarySwitch::setIndex(int i) {
    i = juce::jlimit(0, labels.size() - 1, i);
    if (i == index) return;
    index = i;
    repaint();
}

void RotarySwitch::select(int i) {
    i = juce::jlimit(0, labels.size() - 1, i);
    if (i == index) return;
    setIndex(i);
    if (onChange) onChange(index);
}

void RotarySwitch::mouseDown(const juce::MouseEvent& e) {
    const auto c = centre();
    const auto d = e.position - c;
    if (d.getDistanceFromOrigin() < 24.0f) { // Klick auf den Knopf: weiterschalten
        select((index + 1) % labels.size());
        return;
    }
    const float an = juce::radiansToDegrees(std::atan2(d.x, -d.y));
    int best = 0;
    for (int i = 1; i < angles.size(); ++i)
        if (std::abs(angles[i] - an) < std::abs(angles[best] - an)) best = i;
    select(best);
}

void RotarySwitch::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    if (w.deltaY > 0) select(index + 1);
    else if (w.deltaY < 0) select(index - 1);
}

void RotarySwitch::paint(juce::Graphics& g) {
    const auto c = centre();
    for (int i = 0; i < labels.size(); ++i) {
        const float an = juce::degreesToRadians(angles[i]);
        const juce::Point<float> dot(c.x + 44.0f * std::sin(an), c.y - 44.0f * std::cos(an));
        g.setColour(print);
        g.fillEllipse(dot.x - 2.5f, dot.y - 2.5f, 5.0f, 5.0f);
        const bool twoLines = labels[i].contains("\n");
        const float lr = twoLines ? 66.0f : 60.0f;
        const juce::Point<float> lp(c.x + lr * std::sin(an), c.y - lr * std::cos(an) - (twoLines ? 4.0f : 0.0f));
        const bool on = i == index;
        g.setColour(on ? print : print.withAlpha(0.55f));
        g.setFont(assets->font(13.0f, on));
        g.drawFittedText(labels[i], juce::Rectangle<int>((int) lp.x - 40, (int) lp.y - 15, 80, 30),
                         juce::Justification::centred, 2, 1.0f);
    }
    pointerKnob(g, c, 18.0f, juce::degreesToRadians(angles[index]));
}

// ---------------------------------------------------------------------------------- LookAndFeel

Look::Look() {
    setColour(juce::TextButton::textColourOffId, print);
    setColour(juce::TextButton::textColourOnId, print);
}

void Look::drawLinearSlider(juce::Graphics& g, int, int, int, int, float pos, float, float,
                            juce::Slider::SliderStyle, juce::Slider& s) {
    const float cx = (float) s.getWidth() * 0.5f + 8.0f;
    const float top = (float) s.getPositionOfValue(s.getMaximum());
    const float bot = (float) s.getPositionOfValue(s.getMinimum());

    // Skala (weiß auf Bakelit)
    for (int i = 0; i <= 20; ++i) {
        const float v = (float) i * 5.0f, yy = (float) s.getPositionOfValue(v);
        const bool major = i % 4 == 0;
        g.setColour(print);
        g.fillRect(cx + 10.0f, yy - 0.5f, major ? 9.0f : 5.0f, major ? 1.3f : 0.9f);
        if (major)
            text(g, assets->font(11.0f, false), juce::String((int) v),
                 { cx - 46.0f, yy - 6.5f, 28.0f, 13.0f }, print, juce::Justification::centredRight);
    }

    // Führungsschlitz
    const juce::Rectangle<float> slot(cx - 3.0f, top - 6.0f, 6.0f, bot - top + 12.0f);
    g.setColour(juce::Colour(0xff000000));
    g.fillRoundedRectangle(slot, 3.0f);
    g.setColour(juce::Colours::white.withAlpha(0.18f));
    g.drawRoundedRectangle(slot.expanded(1.0f), 3.5f, 0.8f);

    // Bakelit-Griff mit Alu-Einlage
    const juce::Rectangle<float> cap(cx - 17.0f, pos - 12.0f, 34.0f, 24.0f);
    juce::Path cp;
    cp.addRoundedRectangle(cap, 4.0f);
    shadow(g, cp, 6, { 3, 5 }, 0.8f);
    juce::ColourGradient cg(juce::Colour(0xff484848), cap.getX(), cap.getY(), juce::Colour(0xff080808), cap.getX(), cap.getBottom(), false);
    cg.addColour(0.5, juce::Colour(0xff1c1c1c));
    g.setGradientFill(cg);
    g.fillPath(cp);
    g.setColour(juce::Colours::white.withAlpha(0.22f));
    g.strokePath(cp, juce::PathStrokeType(0.8f));
    const juce::Rectangle<float> inlay(cap.getX() + 4.0f, pos - 2.0f, cap.getWidth() - 8.0f, 4.0f);
    juce::ColourGradient ig(chromeHi, 0.0f, inlay.getY(), chromeLo, 0.0f, inlay.getBottom(), false);
    g.setGradientFill(ig);
    g.fillRoundedRectangle(inlay, 1.5f);
}

void Look::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end,
                            juce::Slider&) {
    // Alu-Drehknopf mit Rändel und Strichmarke (wie „Pegel“ am Vorbild)
    const auto b = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h);
    const auto c = b.getCentre();
    const float R = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f - 4.0f;
    const float an = start + pos * (end - start);

    juce::Path body;
    body.addEllipse(c.x - R, c.y - R, 2 * R, 2 * R);
    shadow(g, body, 10, { 5, 7 }, 0.7f);
    juce::ColourGradient rg(juce::Colour(0xffd8d9d6), c.x, c.y - R, juce::Colour(0xff6b6e6c), c.x, c.y + R, false);
    g.setGradientFill(rg);
    g.fillPath(body);
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    for (int i = 0; i < 72; ++i) { // Rändel
        const float t = juce::MathConstants<float>::twoPi * (float) i / 72.0f;
        g.drawLine(c.x + R * std::sin(t), c.y - R * std::cos(t), c.x + (R - 3.0f) * std::sin(t),
                   c.y - (R - 3.0f) * std::cos(t), 0.7f);
    }
    const float fr = R - 5.0f;
    juce::ColourGradient fg(juce::Colour(0xfffbfbf8), c.x - fr * 0.7f, c.y - fr * 0.8f, juce::Colour(0xff9c9f9c), c.x + fr * 0.7f,
                            c.y + fr * 0.9f, false);
    fg.addColour(0.5, juce::Colour(0xffd4d6d3));
    g.setGradientFill(fg);
    g.fillEllipse(c.x - fr, c.y - fr, 2 * fr, 2 * fr);
    g.setColour(juce::Colours::white.withAlpha(0.10f)); // Drehriefen
    for (float rr = 3.0f; rr < fr; rr += 2.2f) g.drawEllipse(c.x - rr, c.y - rr, 2 * rr, 2 * rr, 0.5f);
    g.setColour(juce::Colours::black.withAlpha(0.25f));
    g.drawEllipse(c.x - fr, c.y - fr, 2 * fr, 2 * fr, 0.8f);
    // Strichmarke
    g.setColour(juce::Colour(0xff1a1a1a));
    g.drawLine(c.x + fr * 0.15f * std::sin(an), c.y - fr * 0.15f * std::cos(an), c.x + fr * 0.92f * std::sin(an),
               c.y - fr * 0.92f * std::cos(an), 2.4f);
}

void Look::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(2.0f);
    // Chromrahmen
    juce::Path frame;
    frame.addRoundedRectangle(r, 5.0f);
    shadow(g, frame, 4, { 2, 3 }, 0.6f);
    juce::ColourGradient fg(chromeHi, 0.0f, r.getY(), chromeLo, 0.0f, r.getBottom(), false);
    g.setGradientFill(fg);
    g.fillPath(frame);
    // Bakelit-Taste
    auto k = r.reduced(3.0f).translated(0.0f, down ? 1.0f : 0.0f);
    juce::ColourGradient kg(juce::Colour(down ? 0xff1a1a1a : (over ? 0xff3a3a3a : 0xff2e2e2e)), 0.0f, k.getY(),
                            juce::Colour(0xff070707), 0.0f, k.getBottom(), false);
    g.setGradientFill(kg);
    g.fillRoundedRectangle(k, 3.0f);
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.drawRoundedRectangle(k, 3.0f, 0.8f);
}

void Look::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool down) {
    text(g, assets->font(14.0f), b.getButtonText(), b.getLocalBounds().toFloat().translated(0.0f, down ? 1.0f : 0.0f), print);
}

} // namespace cvui

// ======================================================================================== Editor

using namespace cvui;

static juce::String padded(double v, int intDigits, bool withSign, bool withDecimal) {
    const double av = std::abs(v);
    juce::String s = juce::String((int) std::floor(av + (withDecimal ? 0.0 : 0.5))).paddedLeft('0', intDigits);
    if (withDecimal) s << "." << juce::String(juce::roundToInt((av - std::floor(av)) * 10.0) % 10);
    if (withSign) s = (v < -0.05 ? "-" : "+") + s;
    return s;
}

CrystalVoiceEditor::CrystalVoiceEditor(CrystalVoiceProcessor& p) : AudioProcessorEditor(&p), proc(p) {
    setLookAndFeel(&look);
    setOpaque(true);

    for (auto* s : { &vocal, &instr }) {
        s->setSliderStyle(juce::Slider::LinearVertical);
        s->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s->setDoubleClickReturnValue(true, 50.0);
        s->onValueChange = [this] { repaint(); };
        addAndMakeVisible(s);
    }
    sync.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    sync.setRotaryParameters(juce::degreesToRadians(-135.0f), juce::degreesToRadians(135.0f), true);
    sync.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    sync.setDoubleClickReturnValue(true, 0.0);
    sync.setMouseDragSensitivity(500);
    sync.onValueChange = [this] { repaint(L::modSync.toNearestInt()); };
    addAndMakeVisible(sync);

    aVocal = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.params, "vocal", vocal);
    aInstr = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.params, "instrumental", instr);
    aSync = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.params, "sync", sync);

    aMic = std::make_unique<juce::ParameterAttachment>(*p.params.getParameter("mic"),
                                                       [this](float v) { micSwitch.setIndex(juce::roundToInt(v)); });
    micSwitch.onChange = [this](int i) { aMic->setValueAsCompleteGesture((float) i); };
    aMic->sendInitialUpdate();
    addAndMakeVisible(micSwitch);

    aPres = std::make_unique<juce::ParameterAttachment>(*p.params.getParameter("presence"),
                                                        [this](float v) { presSwitch.setIndex(v > 0.5f ? 1 : 0); });
    presSwitch.onChange = [this](int i) { aPres->setValueAsCompleteGesture(i == 1 ? 1.0f : 0.0f); };
    aPres->sendInitialUpdate();
    addAndMakeVisible(presSwitch);

    loadRef.onClick = [this] { chooseFile(true); };
    loadIr.onClick = [this] { chooseFile(false); };
    addAndMakeVisible(loadRef);
    addAndMakeVisible(loadIr);
    measure.setTooltip(juce::String::fromUTF8("Gibt einen 6-s-Sweep auf dem Ausgang dieses Kanals aus und misst den Raum. Nur bei gestopptem Transport."));
    measure.onClick = [this] {
        if (proc.getMeasureState() == CrystalVoiceProcessor::Idle) { proc.startMeasurement(); waitTicks = 0; }
        else proc.stopMeasurement();
    };
    addAndMakeVisible(measure);

    status.setFont(assets->font(15.0f, false));
    status.setColour(juce::Label::textColourId, juce::Colour(0xff1d1d1b));
    status.setJustificationType(juce::Justification::centredLeft);
    status.setMinimumHorizontalScale(0.7f);
    status.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(status);

    setSize(W, H);
    startTimerHz(30);
    timerCallback();
}

CrystalVoiceEditor::~CrystalVoiceEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void CrystalVoiceEditor::chooseFile(bool reference) {
    chooser = std::make_unique<juce::FileChooser>(reference ? "Referenz-Take (Gesang, trocken)" : "Raum-Impulsantwort",
                                                  juce::File(), "*.wav;*.aif;*.aiff;*.flac");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, reference](const juce::FileChooser& fc) {
                             const auto f = fc.getResult();
                             if (!f.existsAsFile()) return;
                             if (reference) proc.loadReference(f);
                             else proc.loadRoomIr(f);
                         });
}

// VU-Skala: 0 VU = −18 dBFS, Zeigerausschlag proportional zur Spannung
static float vuPos(float ms) {
    const float db = 10.0f * std::log10(ms + 1e-12f) + 18.0f;
    const float u = std::pow(10.0f, juce::jlimit(-40.0f, 6.0f, db) / 20.0f);
    return (u - 0.1f) / (1.4125f - 0.1f);
}

void CrystalVoiceEditor::timerCallback() {
    // Statuskarte: während der Messung eigene Texte
    const int ms = proc.getMeasureState();
    using P = CrystalVoiceProcessor;
    juce::String st;
    if (proc.wasMeasureRefused()) { refusedNote = juce::String::fromUTF8("Messung nur bei gestopptem Transport"); refusedTicks = 90; }
    if (ms == P::Requested) {
        st = ++waitTicks > 45 ? juce::String::fromUTF8("Warte auf Audio – Spur auf Monitoring/Aufnahme stellen")
                              : juce::String::fromUTF8("Messung startet …");
    } else if (ms == P::Running) {
        const double t = proc.getMeasureSeconds();
        const double pre = gh::IrMeasurement::kPreSeconds, sw = gh::IrMeasurement::kSweepSeconds,
                     tot = pre + sw + gh::IrMeasurement::kTailSeconds;
        const juce::String phase = t < pre ? juce::String::fromUTF8("Ruhe bitte – Grundrauschen")
                                 : t < pre + sw ? juce::String::fromUTF8("Sweep läuft")
                                                : juce::String::fromUTF8("Nachhall wird aufgenommen");
        st = phase + juce::String::fromUTF8("  ·  noch ") + juce::String(juce::jmax(0, (int) std::ceil(tot - t))) + " s";
    } else if (refusedTicks > 0) {
        --refusedTicks;
        st = refusedNote;
    } else {
        st = proc.getStatus();
    }
    status.setText(st.toUpperCase(), juce::dontSendNotification);
    measure.setButtonText(ms == P::Running || ms == P::Requested ? "STOP" : "MEASURE");
    measure.setEnabled(ms != P::Recorded && ms != P::Analyzing);

    const float target[3] = { vuPos(proc.getInputMs()), vuPos(proc.getOutputMs()),
                              juce::jlimit(0.0f, 1.0f, proc.getPlaybackReductionDb() / 30.0f) };
    bool moved = false;
    for (int i = 0; i < 3; ++i) {
        const float t = juce::jmax(-0.02f, target[i]);
        const float nv = firstTick ? t : needle[i] + 0.28f * (t - needle[i]); // Zeigerträgheit
        if (std::abs(nv - needle[i]) > 1e-4f) moved = true;
        needle[i] = nv;
    }
    firstTick = false;
    if (moved) repaint(L::meters.toNearestInt());

    const bool measuring = ms != P::Idle;
    blinkTicks = measuring ? blinkTicks + 1 : 0;
    const bool link = proc.isSidechainConnected(),
               room = measuring ? (blinkTicks / 8) % 2 == 0 : proc.isVocalStageActive(),
               fault = !proc.isModelAccepted();
    if (link != lampLink || room != lampRoom || fault != lampFault) {
        lampLink = link; lampRoom = room; lampFault = fault;
        repaint(L::lamps.expanded(20).toNearestInt());
        repaint(L::modData.toNearestInt());
    }
}

void CrystalVoiceEditor::paint(juce::Graphics& g) {
    const auto& a = *assets;
    const auto full = getLocalBounds().toFloat();

    // Schrank und Einschübe
    g.fillAll(cabinet);
    for (auto p : { L::panelA, L::panelB, L::panelC }) {
        juce::Path pp;
        pp.addRectangle(p);
        shadow(g, pp, 5, { 0, 3 }, 0.45f);
        g.setColour(panelCol);
        g.fillRect(p);
    }
    // Siebdruck: Name, Untertitel, Logo
    {
        text(g, a.font(34.0f).withExtraKerningFactor(0.24f), "CRYSTAL VOICE", { 0.0f, 12.0f, (float) W, 40.0f }, ink);
        g.setColour(ink);
        g.fillRect((float) W * 0.5f - 150.0f, 54.0f, 300.0f, 1.4f);
        text(g, a.font(12.5f).withExtraKerningFactor(0.12f), "ROOM REFLECTION  &  FEEDBACK CANCELLATION",
             { 0.0f, 59.0f, (float) W, 16.0f }, ink);
        if (a.logo.isValid()) {
            g.setColour(ink.withAlpha(0.94f));
            g.drawImage(a.logo, L::logo.reduced(6.0f), juce::RectanglePlacement::centred, true);
        }
    }
    g.drawImage(a.hammer, full, juce::RectanglePlacement::stretchToFit);
    for (auto p : { L::panelA, L::panelB, L::panelC }) {
        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.fillRect(p.getX(), p.getY(), p.getWidth(), 1.2f);
        g.fillRect(p.getX(), p.getY(), 1.2f, p.getHeight());
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillRect(p.getX(), p.getBottom() - 1.2f, p.getWidth(), 1.2f);
        g.fillRect(p.getRight() - 1.2f, p.getY(), 1.2f, p.getHeight());
        screw(g, p.getX() + 24.0f, p.getCentreY());
        screw(g, p.getRight() - 24.0f, p.getCentreY());
    }

    // Kennschild rechts oben
    {
        const juce::Rectangle<float> t(W - 92.0f, 24.0f, 50.0f, 34.0f);
        juce::Path tp;
        tp.addRoundedRectangle(t, 3.0f);
        shadow(g, tp, 4, { 2, 3 }, 0.5f);
        g.setColour(bakelite);
        g.fillPath(tp);
        text(g, a.font(15.0f), "CV 1", t, print);
        g.setColour(chromeMid);
        g.fillEllipse(t.getCentreX() - 2.0f, t.getY() + 2.0f, 4.0f, 4.0f);
        g.fillEllipse(t.getCentreX() - 2.0f, t.getBottom() - 6.0f, 4.0f, 4.0f);
    }

    // Instrumente
    module(g, L::meters);
    {
        Scale vu;
        vu.unit = "VU";
        for (auto [db, lbl] : { std::pair { -20.0f, "20" }, { -10.0f, "10" }, { -7.0f, "7" }, { -5.0f, "5" },
                                { -3.0f, "3" }, { 0.0f, "0" }, { 3.0f, "+3" } })
            vu.marks.add({ (std::pow(10.0f, db / 20.0f) - 0.1f) / 1.3125f, lbl });
        vu.redFrom = 0.9f / 1.3125f;
        Scale fb;
        fb.unit = "dB";
        for (int i = 0; i <= 3; ++i) fb.marks.add({ (float) i / 3.0f, juce::String(i * 10) });

        const juce::String names[3] = { "INPUT", "OUTPUT", juce::String::fromUTF8("FEEDBACK  −dB") };
        for (int i = 0; i < 3; ++i) {
            const juce::Rectangle<float> m(L::meters.getX() + 18.0f + (float) i * 152.0f, L::meters.getY() + 12.0f, 140.0f, 110.0f);
            meter(g, a, m, needle[i], i < 2 ? vu : fb);
            text(g, a.font(13.0f), names[i], { m.getX(), m.getBottom() + 6.0f, m.getWidth(), 14.0f }, print);
        }
    }

    // Signallampen
    module(g, L::lamps);
    text(g, a.font(13.0f), "STATUS", L::lamps.withHeight(24.0f).translated(0.0f, 8.0f), print);
    {
        const char* n[3] = { "LINK", "ROOM", "FAULT" };
        const juce::Colour c[3] = { juce::Colour(0xff4cd964), juce::Colour(0xffffb02e), juce::Colour(0xffff2a1a) };
        const bool on[3] = { lampLink, lampRoom, lampFault };
        for (int i = 0; i < 3; ++i) {
            const float cx = L::lamps.getX() + 28.0f + (float) i * 39.0f;
            lamp(g, { cx, L::lamps.getY() + 72.0f }, 9.0f, c[i], on[i]);
            text(g, a.font(11.5f), n[i], { cx - 22.0f, L::lamps.getY() + 96.0f, 44.0f, 13.0f }, print);
        }
        text(g, a.font(10.5f, false), juce::String::fromUTF8("SIDECHAIN  ·  ROOM  ·  MODEL"),
             { L::lamps.getX(), L::lamps.getBottom() - 30.0f, L::lamps.getWidth(), 12.0f }, print.withAlpha(0.5f));
    }

    // Bedienmodule
    for (auto r : { L::modVocal, L::modInstr, L::modSync, L::modMic, L::modPres }) module(g, r);
    text(g, a.font(14.0f), "VOCAL", L::modVocal.withHeight(22.0f).translated(0.0f, 8.0f), print);
    text(g, a.font(14.0f), "INSTRUMENTAL", L::modInstr.withHeight(22.0f).translated(0.0f, 8.0f), print);
    counter(g, a, { L::modVocal.getCentreX() - 6.0f, L::modVocal.getBottom() - 26.0f }, padded(vocal.getValue(), 3, false, false));
    counter(g, a, { L::modInstr.getCentreX() - 6.0f, L::modInstr.getBottom() - 26.0f }, padded(instr.getValue(), 3, false, false));
    text(g, a.font(12.0f, false), "%", { L::modVocal.getCentreX() + 18.0f, L::modVocal.getBottom() - 34.0f, 20.0f, 16.0f }, print);
    text(g, a.font(12.0f, false), "%", { L::modInstr.getCentreX() + 18.0f, L::modInstr.getBottom() - 34.0f, 20.0f, 16.0f }, print);

    // SYNC-Skala
    text(g, a.font(14.0f), "SYNC", L::modSync.withHeight(22.0f).translated(0.0f, 8.0f), print);
    {
        const juce::Point<float> c(L::modSync.getCentreX(), L::modSync.getY() + 104.0f);
        for (int i = 0; i <= 10; ++i) {
            const float an = juce::degreesToRadians(-135.0f + 27.0f * (float) i);
            const bool major = i % 5 == 0;
            g.setColour(print);
            g.drawLine(c.x + 46.0f * std::sin(an), c.y - 46.0f * std::cos(an), c.x + (major ? 54.0f : 51.0f) * std::sin(an),
                       c.y - (major ? 54.0f : 51.0f) * std::cos(an), major ? 1.6f : 1.0f);
            if (major) {
                const juce::String l = i == 0 ? juce::String::fromUTF8("−100") : juce::String(i == 5 ? "0" : "+100");
                text(g, a.font(11.0f, false), l,
                     { c.x + 64.0f * std::sin(an) - 18.0f, c.y - 64.0f * std::cos(an) - 7.0f, 36.0f, 14.0f }, print);
            }
        }
        counter(g, a, { c.x - 8.0f, L::modSync.getBottom() - 26.0f }, padded(sync.getValue(), 3, true, true));
        text(g, a.font(12.0f, false), "ms", { c.x + 34.0f, L::modSync.getBottom() - 34.0f, 22.0f, 16.0f }, print);
    }

    text(g, a.font(14.0f), "MICROPHONE", L::modMic.withHeight(22.0f).translated(0.0f, 8.0f), print);
    text(g, a.font(14.0f), "PRESENCE", L::modPres.withHeight(22.0f).translated(0.0f, 8.0f), print);
    text(g, a.font(11.5f, false), juce::String::fromUTF8("+2 dB  ·  3 kHz"), { L::modPres.getX(), L::modPres.getBottom() - 32.0f, L::modPres.getWidth(), 14.0f },
         print.withAlpha(0.7f));
    text(g, a.font(11.5f, false), "CAPSULE COMPENSATION", { L::modMic.getX(), L::modMic.getBottom() - 32.0f, L::modMic.getWidth(), 14.0f },
         print.withAlpha(0.7f));

    // Datenmodul
    module(g, L::modData);
    text(g, a.font(12.0f), "REFERENCE TAKE", { 84.0f, 544.0f, 112.0f, 14.0f }, print);
    text(g, a.font(12.0f), "ROOM IR", { 212.0f, 544.0f, 240.0f, 14.0f }, print);
    g.setColour(print.withAlpha(0.6f));
    g.fillRect(212.0f, 551.0f, 70.0f, 1.0f);
    g.fillRect(382.0f, 551.0f, 70.0f, 1.0f);
    {
        const auto c = L::card;
        g.setColour(juce::Colour(0xff3a3a3a));
        g.fillRoundedRectangle(c.expanded(3.0f), 3.0f);
        juce::ColourGradient cg(juce::Colour(0xffefe9d4), 0.0f, c.getY(), juce::Colour(0xffddd5bd), 0.0f, c.getBottom(), false);
        g.setGradientFill(cg);
        g.fillRect(c);
    }
    // Sidechain-Buchse aus braunem Bakelit
    {
        const juce::Point<float> sc(L::modData.getRight() - 64.0f, L::modData.getCentreY() + 6.0f);
        text(g, a.font(12.0f), "SC IN", { sc.x - 30.0f, 544.0f, 60.0f, 14.0f }, print);
        const juce::Rectangle<float> o(sc.x - 26.0f, sc.y - 13.0f, 52.0f, 26.0f);
        juce::ColourGradient rg(chromeHi, 0.0f, o.getY(), chromeLo, 0.0f, o.getBottom(), false);
        g.setGradientFill(rg);
        g.fillRoundedRectangle(o, 13.0f);
        const auto in = o.reduced(3.0f);
        juce::ColourGradient bg(juce::Colour(0xff6a2e22), 0.0f, in.getY(), juce::Colour(0xff35140e), 0.0f, in.getBottom(), false);
        g.setGradientFill(bg);
        g.fillRoundedRectangle(in, 10.0f);
        g.setColour(juce::Colour(0xff0a0303));
        g.fillRoundedRectangle(sc.x - 13.0f, sc.y - 4.5f, 3.5f, 9.0f, 1.5f);
        g.fillRoundedRectangle(sc.x - 1.75f, sc.y - 6.0f, 3.5f, 12.0f, 1.5f);
        g.fillRoundedRectangle(sc.x + 9.5f, sc.y - 4.5f, 3.5f, 9.0f, 1.5f);
    }
}

void CrystalVoiceEditor::paintOverChildren(juce::Graphics& g) {
    // Raumlicht von oben: oben etwas heller, unten etwas dunkler
    {
        juce::ColourGradient lg(juce::Colours::white.withAlpha(0.07f), 0.0f, 0.0f, juce::Colours::black.withAlpha(0.10f), 0.0f,
                                (float) getHeight(), false);
        lg.addColour(0.45, juce::Colours::transparentWhite);
        g.setGradientFill(lg);
        g.fillAll();
    }
    // Glasreflex über der Statuskarte
    const auto c = L::card;
    juce::ColourGradient gl(juce::Colours::white.withAlpha(0.25f), 0.0f, c.getY(), juce::Colours::transparentWhite, 0.0f,
                            c.getCentreY(), false);
    g.setGradientFill(gl);
    g.fillRect(c);
}

void CrystalVoiceEditor::resized() {
    vocal.setBounds(L::modVocal.toNearestInt().withTrimmedTop(32).withTrimmedBottom(50).reduced(8, 0));
    instr.setBounds(L::modInstr.toNearestInt().withTrimmedTop(32).withTrimmedBottom(50).reduced(8, 0));
    sync.setBounds((int) L::modSync.getCentreX() - 36, (int) L::modSync.getY() + 68, 72, 72);
    micSwitch.setBounds(L::modMic.toNearestInt().withTrimmedTop(26).withTrimmedBottom(44));
    presSwitch.setBounds(L::modPres.toNearestInt().withTrimmedTop(26).withTrimmedBottom(44));
    loadRef.setBounds(84, 562, 112, 38);
    loadIr.setBounds(212, 562, 112, 38);
    measure.setBounds(336, 562, 116, 38);
    status.setBounds(L::card.toNearestInt().reduced(10, 2));
}
