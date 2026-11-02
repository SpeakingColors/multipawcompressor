#include "PluginEditor.h"
#include "Layout.h"
#include "Theme.h"
#include "Presets.h"
#include <juce_dsp/juce_dsp.h>
#include <complex>

using namespace juce;
using theme::FontKind;

namespace
{
inline Colour argb (unsigned v) { return Colour ((uint32) v); }
inline Rectangle<float> R (const layout::Rc& r) { return { r.x, r.y, r.w, r.h }; }
inline float clampf (float v, float a, float b) { return jlimit (a, b, v); }
inline String U (const char* c) { return String::fromUTF8 (c); }
inline float l2d (float x) { return 20.0f * std::log10 (std::max (x, 1.0e-9f)); }
const Colour BC[4] { Colour (0xff6CC9F5), Colour (0xff02EBBA), Colour (0xffFFD100), Colour (0xffFF7BB1) };

// ---------------------------------------------------------------- number formats (same as the mockup)
String fHz (float v)
{
    if (v >= 1000.0f) { String s (v / 1000.0f, v >= 10000.0f ? 1 : 2); if (s.contains (".")) s = s.trimCharactersAtEnd ("0").trimCharactersAtEnd ("."); return s + " kHz"; }
    return (v >= 100.0f ? String (roundToInt (v)) : String (v, 1).replace (".0", "")) + " Hz";
}
String fDb (float v, int dp = 1) { return String (v > 0.049f ? "+" : "") + String (v, dp) + " dB"; }
String fMs (float ms)
{
    if (ms >= 1000.0f) return String (ms / 1000.0f, 2) + " s";
    if (ms >= 100.0f) return String (roundToInt (ms)) + " ms";
    if (ms >= 10.0f) return String (ms, 1) + " ms";
    return String (ms, 2) + " ms";
}
String fPct (float v) { return String (roundToInt (v)) + "%"; }

// ---------------------------------------------------------------- wood cheeks and logo
struct Mul32
{
    uint32_t a;
    double next() { a += 0x6D2B79F5u; uint32_t t = a ^ (a >> 15); t *= (1u | a); t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t; return (double) (t ^ (t >> 14)) / 4294967296.0; }
};
Image makeWood (uint32_t seed, bool edgeRight)
{
    const int w = 104, h = 1280;
    Image img (Image::ARGB, w, h, true);
    Graphics g (img);
    Mul32 r { seed };
    g.fillAll (Colour (0xff3d2210));
    for (int x = 0; x < w; ++x)
    {
        const double a = r.next();
        g.setColour (Colour::fromFloatRGBA (123 / 255.0f, 74 / 255.0f, 38 / 255.0f, (float) (0.40 * (0.4 + 0.6 * a))));
        g.fillRect (x, 0, 1, h);
        if (r.next() < 0.35)
        {
            const float al = (float) (0.75 * r.next());
            const double y0 = r.next() * h, len = h * (0.2 + 0.6 * r.next());
            g.setColour (Colour::fromFloatRGBA (90 / 255.0f, 52 / 255.0f, 25 / 255.0f, al));
            g.fillRect ((float) x, (float) y0, 1.0f, (float) len);
        }
        if (r.next() < 0.12) { g.setColour (Colours::black.withAlpha (0.35f)); g.fillRect (x, 0, 1, h); }
    }
    g.setColour (Colours::black);
    g.fillRect (edgeRight ? 0 : w - 3, 0, 3, h);
    return img;
}
std::unique_ptr<Drawable> makeLogo()
{
    static const char* svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64"><path d="M8 6 L24 22 H40 L56 6 L60 36 Q60 58 32 60 Q4 58 4 36 Z" fill="#FF7BB1"/><path d="M12 14 L21 23 H14 Z M52 14 L43 23 H50 Z" fill="#FBBDD0"/><ellipse cx="22" cy="36" rx="4" ry="5" fill="#16171a"/><ellipse cx="42" cy="36" rx="4" ry="5" fill="#16171a"/><path d="M29 44 H35 L32 48 Z" fill="#16171a"/><path d="M32 48 Q28 53 24 50 M32 48 Q36 53 40 50" stroke="#16171a" stroke-width="2" fill="none"/><path d="M2 40 H14 M2 46 H14 M62 40 H50 M62 46 H50" stroke="#FBBDD0" stroke-width="1.6"/></svg>)";
    if (auto xml = XmlDocument::parse (svg)) return Drawable::createFromSVG (*xml);
    return {};
}

const char* kPatNames[] = { "Off", "Kick Accentuator Simple", "Go Offbeat Go", "Offbeat Wave", "Hihat Rider", "Four Floor Pump", "Gate Eighths", "Rolling Swing", "Slow Breath" };
MultiPawProcessor::Nodes patternNodes (int k)
{
    MultiPawProcessor::Nodes a;
    auto P = [&] (float x, float y) { a.push_back ({ x, y }); };
    switch (k)
    {
        case 0: P (0, 1); P (1, 1); break;
        case 1: P (0, .4f); for (int b = 0; b < 4; ++b) { P (b / 4.0f + .004f, .25f); P (b / 4.0f + .05f, .7f); P (b / 4.0f + .2f, 1); } P (1, .4f); break;
        case 2: P (0, .3f); for (int b = 0; b < 4; ++b) { P (b / 4.0f + .01f, .2f); P (b / 4.0f + .07f, .9f); P (b / 4.0f + .125f, 1); P (b / 4.0f + .2f, .95f); } P (1, .3f); break;
        case 3: for (int k2 = 0; k2 <= 8; ++k2) P (k2 / 8.0f, k2 % 2 ? 1.0f : .35f); break;
        case 4: P (0, 1); for (int k2 = 0; k2 < 16; ++k2) { P (k2 / 16.0f + .006f, k2 % 2 ? .55f : .8f); P (k2 / 16.0f + .04f, 1); } P (1, 1); break;
        case 5: P (0, .3f); for (int b = 0; b < 4; ++b) { P (b / 4.0f + .003f, .18f); P (b / 4.0f + .12f, .85f); P (b / 4.0f + .2f, 1); } P (1, .3f); break;
        case 6: for (int k2 = 0; k2 < 8; ++k2) { P (k2 / 8.0f, 1); P (k2 / 8.0f + .058f, 1); P (k2 / 8.0f + .0625f, .05f); P (k2 / 8.0f + .12f, .05f); } P (1, 1); break;
        case 7: for (int k2 = 0; k2 <= 16; ++k2) P (k2 / 16.0f, k2 % 4 == 0 ? .35f : k2 % 4 == 2 ? .7f : 1.0f); break;
        default: P (0, .4f); P (.25f, 1); P (.5f, .4f); P (.75f, 1); P (1, .4f); break;
    }
    return a;
}
} // namespace

// ================================================================================================
struct MultiPawEditor::Impl
{
    enum Kind { Knob, Toggle, Power, Radio, Seg, Select, Btn, Cv };
    struct Item
    {
        int kind = 0; layout::Rc r {}, labR {}, valR {}, knobR {}; String tip, pid, text, cls, label, action; int idx = -1, band = -1, cv = -1, off = 0;
        Colour col; StringArray opts;
    };

    MultiPawEditor& ed; MultiPawProcessor& proc;
    int page = 0;
    char look = 'C';
    bool tips = true;
    std::vector<Item> items;
    Image woodL, woodR;
    std::unique_ptr<Drawable> logo;
    int hot = -1, tipItem = -1, tipTicks = 0; Point<float> tipPos;
    String tipText;
    // drag state
    int dragItem = -1; double dragStartNorm = 0; Point<float> dragStart; bool dragGesture = false;
    String dragKind; int dragBand = -1;
    int dragDpcBand = -1, dragDpcNode = -1;
    bool clipPre[2] { false, false }, clipPost[2] { false, false };
    // spectrum
    dsp::FFT fft { 12 };
    std::array<float, 4096> spPre {}, spPost {};
    std::array<float, 8192> work {};
    std::array<float, 2048> dbPre {}, dbPost {};
    std::array<float, 4096> win {};
    // history for the band displays (60 Hz)
    static constexpr int H = 480;
    std::array<std::array<float, H>, 4> hLvl {}, hGr {};
    int hPos = 0;
    String toastText; int toastTicks = 0;
    String presetName[2];

    Impl (MultiPawEditor& e, MultiPawProcessor& p) : ed (e), proc (p)
    {
        woodL = makeWood (0x1234abcdu, false); woodR = makeWood (0x9876fedcu, true);
        logo = makeLogo();
        for (int i = 0; i < 4096; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * MathConstants<float>::pi * (float) i / 4096.0f);
        dbPre.fill (-120.0f); dbPost.fill (-120.0f);
        for (auto& a : hLvl) a.fill (-90.0f);
        const auto lk = proc.getSetting (setting::look, "C").toString();
        if (lk.isNotEmpty()) look = lk[0];
        tips = (bool) proc.getSetting (setting::tips, true);
        page = (int) proc.getSetting (setting::page, 0);
        presetName[0] = proc.getSetting (setting::preset1, "").toString(); presetName[1] = proc.getSetting (setting::preset2, "").toString();
        build();
    }

    const theme::Look& T() const { return theme::look (look); }

    // ---------------------------------------------------------------- parameter access
    static const mp::PDef* defOf (const String& id)
    {
        for (const auto& d : mp::kParams) if (id == d.id) return &d;
        for (const auto& d : mp::kUi) if (id == d.id) return &d;
        return nullptr;
    }
    static bool isHost (const String& id) { for (const auto& d : mp::kParams) if (id == d.id) return true; return false; }
    float get (const String& id) const
    {
        if (isHost (id)) if (auto* p = proc.apvts.getParameter (id)) return p->convertFrom0to1 (p->getValue());
        const auto* d = defOf (id);
        return (float) (double) proc.getSetting (("ui." + id).toRawUTF8(), d ? d->df : 0.0f);
    }
    void set (const String& id, float v)
    {
        const auto* d = defOf (id); if (! d) return;
        v = clampf (v, d->mn, d->mx); if (d->isInt) v = std::round (v);
        if (isHost (id)) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); }
        else proc.setSetting (("ui." + id).toRawUTF8(), v);
        ed.repaint();
    }
    void begin (const String& id) { if (isHost (id)) if (auto* p = proc.apvts.getParameter (id)) p->beginChangeGesture(); }
    void end (const String& id) { if (isHost (id)) if (auto* p = proc.apvts.getParameter (id)) p->endChangeGesture(); }
    void setUser (const String& id, float v) { begin (id); set (id, v); end (id); }
    double posOf (const String& id, float v) const
    {
        const auto* d = defOf (id); if (! d) return 0;
        return d->log ? std::log (v / d->mn) / std::log (d->mx / d->mn) : (v - d->mn) / (d->mx - d->mn);
    }
    float valOf (const String& id, double t) const
    {
        const auto* d = defOf (id); if (! d) return 0;
        t = jlimit (0.0, 1.0, t);
        float v = d->log ? d->mn * (float) std::pow (d->mx / d->mn, t) : d->mn + (float) t * (d->mx - d->mn);
        return d->isInt ? std::round (v) : v;
    }
    int bands() const { return (int) get ("m.bands"); }

    // ---------------------------------------------------------------- value text on the knobs
    String fmt (const String& id, float v) const
    {
        const String k = id.fromFirstOccurrenceOf (".", false, false);
        if (k == "inPan" || k == "outPan") return v == 0 ? String ("C") : String (v < 0 ? "L" : "R") + String (roundToInt (std::abs (v)));
        if (k == "inG" || k == "outG" || k == "postG" || k == "e1g" || k == "e2g" || k == "lowVol" || k == "hiVol")
            return (id.startsWithChar ('b') && (k == "inG" || k == "outG")) ? fDb (v) : fDb (v);
        if (k == "ceil") return String (v, 1);
        if (k == "mix" || k == "lStr" || k == "hStr" || k == "dStr") return fPct (v);
        if (k == "rat")
        {
            const bool steps = get ("m.steps") > 0.5f;
            double u = v; if (steps) { const double r = u >= 0.998 ? 1e9 : 1.0 / (1.0 - u); u = r > 20.5 ? 1.0 : 1.0 - 1.0 / std::max (1.0, std::round (r)); }
            if (u >= 0.998) return "1 : inf";
            const double r = 1.0 / (1.0 - u); return "1 : " + String (r, r >= 10 ? 1 : 2);
        }
        if (k == "atk") return fMs (v);
        if (k == "rel" || k == "lRel") return v < 1 ? String ("AUTO") : fMs (v);
        if (k == "boost") return "+" + String (v, 1) + "dB";
        if (k == "lOut") return String (v > -0.05f ? "+0" : String (v, 1)) + "dB";
        if (k == "zoom") return String (roundToInt (1 + v * 7)) + "x";
        if (k == "sens") return String (v, 1) + "%";
        if (k == "offset") return String (roundToInt (v * 100)) + "%";
        if (k == "split" || k == "e1f" || k == "e2f") return fHz (v);
        if (k == "lAtk" || k == "hAtk") return String (v, 2) + " ms";
        if (k == "lAtkC" || k == "hAtkC" || k == "lRelC" || k == "hRelC") return String (v > 0 ? "+" : "") + String (roundToInt (v)) + "%";
        if (k == "lHold" || k == "hHold" || k == "lRel" || k == "hRel") return fMs (v);
        if (k == "e1w" || k == "e2w") return "Q " + String (v, 1);
        return String (v, 1);
    }

    // ---------------------------------------------------------------- items for the current page
    void build()
    {
        items.clear();
        auto addCtl = [&] (const layout::Ctl& c)
        {
            Item it; it.kind = c.kind; it.r = c.r; it.labR = c.labR; it.valR = c.valR; it.knobR = c.knobR; it.tip = U (c.tip); it.pid = c.pid; it.idx = c.idx;
            it.text = U (c.text); it.cls = c.cls; it.label = U (c.label); it.col = argb (c.color | 0xff000000u);
            if (c.kind == Select) { it.opts = StringArray::fromTokens (String (c.opts).removeCharacters ("[]\""), ",", ""); it.off = c.off; }
            if (it.pid.startsWithChar ('b') && it.pid[1] >= '0' && it.pid[1] <= '3') it.band = it.pid[1] - '0';
            items.push_back (it);
        };
        auto addOth = [&] (const layout::Oth& o)
        {
            Item it; it.kind = Btn; it.r = o.r; it.tip = U (o.tip); it.text = U (o.text); it.cls = o.cls; it.band = o.band; it.action = String (o.id).isNotEmpty() ? String (o.id) : U (o.text);
            items.push_back (it);
        };
        auto addCv = [&] (const layout::Cv& c)
        {
            Item it; it.kind = Cv; it.r = c.r; it.tip = U (c.tip); it.cv = c.id; it.band = c.band; it.cls = c.cls; items.push_back (it);
        };
        if (page == 0)
        {
            for (const auto& c : layout::ctls0) addCtl (c);
            for (const auto& o : layout::oths0) addOth (o);
            for (const auto& c : layout::cvs0) addCv (c);
        }
        else
        {
            for (const auto& c : layout::ctls1) addCtl (c);
            for (const auto& o : layout::oths1) addOth (o);
            for (const auto& c : layout::cvs1) addCv (c);
        }
        // the preview rig is not part of the plugin: drop its controls
        items.erase (std::remove_if (items.begin(), items.end(), [] (const Item& i) { return i.r.y > 650.0f && i.r.y < 700.0f; }), items.end());
        items.erase (std::remove_if (items.begin(), items.end(), [] (const Item& i) { return i.kind == Btn && i.r.y < 330 && i.r.y > 260 && (i.action == "Mute" || i.action == "Solo" || i.action == "M" || i.action == "S"); }), items.end());
        updateVisibility();
    }
    bool bandActive (int b) const { return b < 0 || b < bands(); }
    void updateVisibility() {}

    // ---------------------------------------------------------------- drawing
    void drawBtn (Graphics& g, Rectangle<float> r, const String& text, bool on, bool yel, float fs, bool tgb)
    {
        auto& t = T();
        g.setColour (on ? (yel ? Colour (0xff3a3300) : theme::sel) : t.plate);
        g.fillRoundedRectangle (r, tgb ? 3.0f : 4.0f);
        g.setColour (on ? (yel ? t.yellow : theme::selb) : t.line);
        g.drawRoundedRectangle (r.reduced (0.5f), tgb ? 3.0f : 4.0f, 1.0f);
        g.setColour (on ? (yel ? t.yellow : theme::lilac) : t.ink);
        g.setFont (theme::font (FontKind::regular, fs));
        g.drawText (text, r.expanded (20.0f, 0.0f), Justification::centred, false);
    }
    void drawKnobShape (Graphics& g, Rectangle<float> r, float t01, Colour col, bool bipolar, float t0)
    {
        const float s = jmin (r.getWidth(), r.getHeight()), cx = r.getCentreX(), cy = r.getCentreY(), k = s / 100.0f;
        auto arc = [&] (float a0, float a1, Colour c, float w)
        {
            Path p; p.addCentredArc (cx, cy, 43 * k, 43 * k, 0.0f, degreesToRadians (a0), degreesToRadians (a1), true);
            g.setColour (c); g.strokePath (p, PathStrokeType (w * k, PathStrokeType::curved, PathStrokeType::rounded));
        };
        arc (-135.0f, 135.0f, Colour (0xff2a2c33), 7.0f);
        const float a = -135.0f + 270.0f * t01, a0 = -135.0f + 270.0f * (bipolar ? t0 : 0.0f);
        if (std::abs (a - a0) > 0.5f) arc (jmin (a, a0), jmax (a, a0), col, 7.0f);
        ColourGradient grad (Colour (0xfff1f1f4), cx - 10 * k, cy - 15 * k, Colour (0xff6e6f78), cx + 35 * k, cy + 35 * k, true);
        grad.addColour (0.55, Colour (0xffb9bac2));
        g.setGradientFill (grad); g.fillEllipse (cx - 31 * k, cy - 31 * k, 62 * k, 62 * k);
        g.setColour (Colour (0xff4a4b53)); g.drawEllipse (cx - 31 * k, cy - 31 * k, 62 * k, 62 * k, 2 * k);
        const float rad = degreesToRadians (a);
        g.setColour (Colour (0xff16171a));
        g.drawLine (cx, cy, cx + std::sin (rad) * 24 * k, cy - std::cos (rad) * 24 * k, 7 * k);
    }

    void paintItem (Graphics& g, const Item& it, int index)
    {
        auto& t = T();
        auto r = R (it.r);
        const bool active = bandActive (it.band);
        if (! active) g.setOpacity (0.35f);
        switch (it.kind)
        {
            case Knob:
            {
                const float v = get (it.pid);
                const float pos = (float) posOf (it.pid, v);
                const auto* d = defOf (it.pid);
                const bool bip = d && d->mn < 0 && d->mx > 0 && ! d->log;
                drawKnobShape (g, R (it.knobR), pos, it.col, bip, d ? (float) posOf (it.pid, 0.0f) : 0.0f);
                g.setColour (t.ink); g.setFont (theme::font (FontKind::mono, 10.5f));
                g.drawText (fmt (it.pid, v), R (it.valR).expanded (24.0f, 2.0f), Justification::centred, false);
                g.setColour (t.title); g.setFont (theme::font (FontKind::semi, 12.0f).withExtraKerningFactor (0.8f / 12.0f));
                g.drawText (it.label.toUpperCase(), R (it.labR).expanded (24.0f, 2.0f), Justification::centred, false);
                break;
            }
            case Toggle: drawBtn (g, r, it.text, get (it.pid) > 0.5f, it.text == "DPC", 12.0f, true); break;
            case Power:
            {
                const bool on = get (it.pid) > 0.5f; const bool grn = it.cls.contains (" g");
                const Colour c = grn ? theme::green : theme::red;
                auto cr = r.withSizeKeepingCentre (16.0f, 16.0f);
                if (on) { g.setColour (c.withAlpha (0.35f)); g.fillEllipse (cr.expanded (4.0f)); }
                g.setColour (on ? c : Colour (0xff3a0a1c)); g.fillEllipse (cr);
                g.setColour (Colours::black); g.drawEllipse (cr, 1.0f);
                if (on) { g.setColour (Colours::white.withAlpha (0.5f)); g.fillEllipse (cr.reduced (5.0f).translated (-1.0f, -1.0f)); }
                break;
            }
            case Radio:
            {
                const bool on = (int) std::round (get (it.pid)) == it.idx;
                auto lamp = Rectangle<float> (r.getX() + 4, r.getCentreY() - 5.5f, 11, 11);
                if (on) { g.setColour (t.yellow.withAlpha (0.35f)); g.fillEllipse (lamp.expanded (3.0f)); }
                g.setColour (on ? t.yellow : Colour (0xff26282c)); g.fillEllipse (lamp);
                g.setColour (t.ink); g.setFont (theme::font (FontKind::regular, 13.0f));
                g.drawText (it.text, Rectangle<float> (lamp.getRight() + 5, r.getY(), r.getWidth() + 30, r.getHeight()), Justification::centredLeft, false);
                break;
            }
            case Seg:
            {
                const bool on = (int) std::round (get (it.pid)) == it.idx;
                g.setColour (on ? theme::sel : t.plate); g.fillRect (r);
                g.setColour (on ? theme::selb : t.line); g.drawRect (r, 1.0f);
                g.setColour (on ? theme::lilac : t.ink); g.setFont (theme::font (FontKind::regular, 13.0f));
                g.drawText (it.text, r.expanded (10.0f, 0.0f), Justification::centred, false);
                break;
            }
            case Select:
            {
                g.setColour (t.disp); g.fillRoundedRectangle (r, 4.0f);
                g.setColour (t.line); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
                const int i = (int) std::round (get (it.pid)) - it.off;
                g.setColour (t.ink); g.setFont (theme::font (FontKind::regular, 14.0f));
                g.drawText (it.opts[jlimit (0, it.opts.size() - 1, i)], r.reduced (6.0f, 0.0f).withTrimmedRight (14.0f), Justification::centredLeft, false);
                Path c; c.addTriangle (r.getRight() - 14, r.getCentreY() - 2, r.getRight() - 6, r.getCentreY() - 2, r.getRight() - 10, r.getCentreY() + 3);
                g.setColour (t.dim); g.fillPath (c);
                break;
            }
            case Btn: paintBtn (g, it, r); break;
            case Cv: paintCanvas (g, it, r); break;
        }
        g.setOpacity (1.0f);
        juce::ignoreUnused (index);
    }

    bool btnOn (const Item& it) const
    {
        const String a = it.action;
        if (a == "pgA") return page == 0;
        if (a == "pgB") return page == 1;
        if (a == "bBypass") return get ("m.bypass") > 0.5f;
        if (a == "Keep") return get ("ui.keep") > 0.5f || (bool) proc.getSetting ("ui.keep", false);
        if (a == "Mute") return get ("b" + String (it.band) + ".mute") > 0.5f;
        if (a == "Solo") return get ("b" + String (it.band) + ".solo") > 0.5f;
        if (a == "M") return get ("b3.mute") > 0.5f;
        if (a == "S" && it.r.y < 330) return get ("b3.solo") > 0.5f;
        if (a == "H") return get ("b" + String (it.band) + ".knee") < 0.5f;
        if (a == "S") return get ("b" + String (it.band) + ".knee") > 0.5f;
        if (a == "Scope") return (int) get ("b" + String (it.band) + ".view") == 0;
        if (a == "Gain Red.") return (int) get ("b" + String (it.band) + ".view") == 1;
        if (a == "DPC") return (int) get ("b" + String (it.band) + ".view") == 2;
        if (page == 1 && viewIndex (a) >= 0) return viewIndex (a) == (int) get ("s.view");
        return false;
    }
    static int viewIndex (const String& a)
    {
        static const char* names[] = { "FFT", "Low", "Hi", "FFT | Hi", "FFT | Low", "Low | Hi", "FFT | Low/Hi", "Low | Hi | FFT", "System" };
        for (int i = 0; i < 9; ++i) if (a == names[i]) return i;
        return -1;
    }
    void paintBtn (Graphics& g, const Item& it, Rectangle<float> r)
    {
        auto& t = T(); const String a = it.action;
        if (a == "pgA" || a == "pgB")
        {
            const bool on = btnOn (it);
            g.setColour (on ? theme::sel : t.plate); g.fillRect (r);
            g.setColour (on ? theme::lilac : t.dim); g.setFont (theme::font (FontKind::bold, 14.0f).withExtraKerningFactor (1.2f / 14.0f));
            g.drawText (String (a == "pgA" ? "COMPRESSOR" : "SIDECHAIN"), r.withTrimmedLeft (18.0f).expanded (10.0f, 0.0f), Justification::centred, false);
            const bool lamp = get (a == "pgA" ? "m.on" : "s.on") > 0.5f;
            auto dot = Rectangle<float> (r.getX() + 10, r.getCentreY() - 4.5f, 9, 9);
            g.setColour (lamp ? theme::green : Colour (0xff2a2c31)); g.fillEllipse (dot);
            return;
        }
        if (a == "nm" || it.cls == "nm") { return; }
        if (it.cls.contains ("cap") || it.cls == "mixnote") return;
        if (it.text.contains (U ("\xe2\x97\x80")) || it.text.contains (U ("\xe2\x96\xb6")))
        {
            const bool left = it.text.contains (U ("\xe2\x97\x80"));
            g.setColour (t.plate); g.fillRoundedRectangle (r, 4.0f); g.setColour (t.line); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
            Path tri; const float cx = r.getCentreX(), cy = r.getCentreY();
            if (left) tri.addTriangle (cx + 4, cy - 5, cx + 4, cy + 5, cx - 4, cy); else tri.addTriangle (cx - 4, cy - 5, cx - 4, cy + 5, cx + 4, cy);
            g.setColour (t.ink); g.fillPath (tri); return;
        }
        const bool tg = it.cls.contains ("tgb"), yel = a == "DPC";
        String txt = it.text;
        if (a == "pName") txt = presetName[page].isEmpty() ? String ("Init") : presetName[page];
        drawBtn (g, r, txt, btnOn (it), yel, tg ? 12.0f : 14.0f, tg);
    }

    // ----- canvases
    void drawVU (Graphics& g, Rectangle<float> r, bool pre)
    {
        auto& t = T();
        g.setColour (t.disp); g.fillRoundedRectangle (r, 3.0f); g.setColour (t.line); g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        const float w = 98, h = 96, mn = -60, mx = 6, ox = r.getX() + 1, oy = r.getY() + 1;
        auto Y = [&] (float d) { return oy + h - 8 - (clampf (d, mn, mx) - mn) / (mx - mn) * (h - 14); };
        ColourGradient grad (theme::pink, 0, Y (mx), theme::blue, 0, Y (mn), false);
        const float f12 = (mx - (-12)) / (mx - mn), f0 = (mx - 0) / (mx - mn);
        grad = ColourGradient (Colour (0xffFF1383), 0, Y (mx), theme::blue, 0, Y (mn), false);
        grad.clearColours();
        grad.addColour (0.0, Colour (0xffFF1383)); grad.addColour (jmin (1.0, (double) f0), Colour (0xffFF1383));
        grad.addColour (jmin (1.0, (double) f0 + 0.001), Colour (0xffA43DFF)); grad.addColour (f12, Colour (0xffA43DFF));
        grad.addColour (jmin (1.0, (double) f12 + 0.001), Colour (0xff6CC9F5)); grad.addColour (1.0, Colour (0xff6CC9F5));
        const bool dens = get ("m.vu") > 0.5f;
        g.setFont (theme::font (FontKind::mono, 9.0f));
        for (int tk : { 6, 0, -6, -12, -24, -36, -48 })
        {
            const float y = Y ((float) tk);
            g.setColour (t.dim); g.fillRect (ox + 20, y, 3.0f, 1.0f);
            g.drawText (String (tk), Rectangle<float> (ox - 14, y - 6, 32, 12), Justification::centredRight, false);
        }
        if (pre) { g.setColour (Colour (0xff01FCDC)); g.fillRect (ox + 24, Y (-6) - 1, 3.0f, 3.0f); g.fillRect (ox + 86, Y (-6) - 1, 3.0f, 3.0f); }
        for (int k = 0; k < 2; ++k)
        {
            const float pk = (pre ? proc.meters.prePk[k] : proc.meters.postPk[k]).load(), ms = (pre ? proc.meters.preMs[k] : proc.meters.postMs[k]).load();
            const float x = ox + 28 + k * 30, pd = l2d (pk), rd = 10.0f * std::log10 (ms + 1.0e-12f) + 3.0f;
            g.setGradientFill (grad);
            if (dens)
            {
                g.setColour (t.dim.withAlpha (0.30f)); g.fillRect (x, Y (pd), 22.0f, Y (mn) - Y (pd));
                g.setGradientFill (grad); g.fillRect (x + 4, Y (rd), 14.0f, Y (mn) - Y (rd));
            }
            else
            {
                g.fillRect (x, Y (rd), 22.0f, Y (mn) - Y (rd));
                g.setColour (Colours::white); g.fillRect (x, Y (pd), 22.0f, 2.0f);
            }
            bool& clip = (pre ? clipPre : clipPost)[k];
            if (pd >= 0.0f) clip = true;
            g.setColour (clip ? Colour (0xffFF1383) : Colour (0xff2a2c31)); g.fillRect (x, oy, 22.0f, 4.0f);
        }
        g.setColour (t.dim);
        g.drawText ("L", Rectangle<float> (ox + 29, oy + h - 11, 20, 12), Justification::centred, false);
        g.drawText ("R", Rectangle<float> (ox + 59, oy + h - 11, 20, 12), Justification::centred, false);
        juce::ignoreUnused (w);
    }

    std::array<float, 3> xs() const { return { get ("m.x1"), get ("m.x2"), get ("m.x3") }; }
    std::vector<float> edges() const
    {
        std::vector<float> e { 20.0f }; const int n = bands(); const auto x = xs();
        for (int k = 0; k < n - 1; ++k) e.push_back (x[(size_t) k]);
        e.push_back (20000.0f); return e;
    }
    static float fxp (float f, float x0, float w) { return x0 + std::log (f / 20.0f) / std::log (1000.0f) * w; }
    static float xfp (float x, float x0, float w) { return 20.0f * std::pow (1000.0f, (x - x0) / w); }
    static constexpr float GX0 = 34, GY0 = 6;
    float GX1 = 920 - 30, GY1 = 176 - 16;
    float YDB (float d) const { return GY0 + (8 - clampf (d, -24, 8)) / 32.0f * (GY1 - GY0); }
    float DBY (float y) const { return 8 - (y - GY0) / (GY1 - GY0) * 32.0f; }

    void updateSpectrum()
    {
        const int w = proc.ringPos.load (std::memory_order_acquire);
        auto doOne = [&] (const std::array<float, MultiPawProcessor::kRing>& ring, std::array<float, 2048>& db)
        {
            for (int i = 0; i < 4096; ++i) work[(size_t) i] = ring[(size_t) ((w + i) & (MultiPawProcessor::kRing - 1))] * win[(size_t) i];
            std::fill (work.begin() + 4096, work.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (work.data());
            for (int k = 0; k < 2048; ++k)
            {
                const float m = work[(size_t) k] / (4096.0f * 0.25f), d = 20.0f * std::log10 (m + 1.0e-9f);
                db[(size_t) k] = d > db[(size_t) k] ? d : db[(size_t) k] * 0.82f + d * 0.18f;
            }
        };
        doOne (proc.ringPre, dbPre); doOne (proc.ringPost, dbPost);
    }
    void specPath (Path& p, const std::array<float, 2048>& db, float x0, float w)
    {
        const double fs = proc.sampleRateNow; const int n = 2048; bool first = true;
        auto SPY = [&] (float d) { return GY1 - (clampf (d, -96, -6) + 96.0f) / 90.0f * (GY1 - GY0); };
        for (int x = 0; x <= (int) w; ++x)
        {
            const float f0 = xfp (x0 + (float) x, x0, w), f1 = xfp (x0 + (float) x + 1, x0, w);
            const float b0 = f0 * 4096.0f / (float) fs, b1 = f1 * 4096.0f / (float) fs; float d;
            if (b1 - b0 < 1.0f)
            {
                const float bf = clampf ((b0 + b1) * 0.5f, 1.0f, (float) n - 2); const int i = (int) bf; const float fr = bf - (float) i;
                d = db[(size_t) i] * (1 - fr) + db[(size_t) i + 1] * fr;
            }
            else
            {
                float s = 0; int c = 0;
                for (int k = jmax (1, (int) b0); k <= jmin (n - 1, (int) std::ceil (b1)); ++k) { s += std::pow (10.0f, db[(size_t) k] / 10.0f); ++c; }
                d = 10.0f * std::log10 (s / (float) jmax (1, c) + 1.0e-12f);
            }
            const float y = SPY (d);
            if (first) { p.startNewSubPath (x0 + (float) x, y); first = false; } else p.lineTo (x0 + (float) x, y);
        }
    }
    void drawGraph (Graphics& g, Rectangle<float> r)
    {
        auto& t = T();
        g.setColour (t.disp); g.fillRoundedRectangle (r, 3.0f);
        g.saveState(); g.reduceClipRegion (r.toNearestInt()); g.setOrigin (r.getPosition().toInt() + Point<int> (1, 1));
        const float pw = GX1 - GX0; const int n = bands(); auto ed = edges();
        g.setFont (theme::font (FontKind::mono, 9.0f));
        static const int DBT[] = { 8, 6, 3, 0, -3, -6, -12, -18, -24 };
        for (int d : DBT)
        {
            const float y = YDB ((float) d); g.setColour (d == 0 ? Colour (0xff4a4d55) : t.line); g.drawHorizontalLine ((int) y, GX0, GX1);
            g.setColour (t.dim); g.drawText ((d > 0 ? "+" : "") + String (d), Rectangle<float> (GX0 - 34, y - 6, 30, 12), Justification::centredRight, false);
        }
        static const float FL[] = { 25, 50, 125, 250, 500, 1000, 2000, 4000, 8000, 12000, 18000 };
        static const char* FT[] = { "25 Hz", "50 Hz", "125 Hz", "250 Hz", "500 Hz", "1 kHz", "2 kHz", "4 kHz", "8 kHz", "12 kHz", "18 kHz" };
        for (int k = 0; k < 11; ++k)
        {
            const float x = fxp (FL[k], GX0, pw); g.setColour (t.line); g.drawVerticalLine ((int) x, GY0, GY1);
            g.setColour (t.dim); g.drawText (FT[k], Rectangle<float> (clampf (x, GX0 + 14, GX1 - 10) - 25, 176 - 14, 50, 12), Justification::centred, false);
        }
        for (int d : { -80, -60, -40, -20 })
        {
            const float y = GY1 - (d + 96.0f) / 90.0f * (GY1 - GY0); g.setColour (t.dim.withAlpha (0.8f));
            g.drawText (String (d), Rectangle<float> (GX1 + 4, y - 6, 30, 12), Justification::centredLeft, false); g.fillRect (GX1, y, 3.0f, 1.0f);
        }
        g.setColour (t.dim); g.drawText ("dBFS", Rectangle<float> (GX1 + 3, GY0, 34, 10), Justification::centredLeft, false);
        for (int b = 0; b < n; ++b)
        {
            const String bs = "b" + String (b) + ".";
            const float x0 = fxp (ed[(size_t) b], GX0, pw), x1 = fxp (ed[(size_t) b + 1], GX0, pw), yi = YDB (get (bs + "inG")), yo = YDB (get (bs + "outG"));
            g.setColour (BC[b].withAlpha (0.13f)); g.fillRect (x0, GY0, x1 - x0, GY1 - GY0);
            g.setColour (BC[b].withAlpha (0.20f)); g.fillRect (x0, yi, (x1 - x0) * 0.5f, GY1 - yi);
            g.setColour (BC[b].withAlpha (0.12f)); g.fillRect (x0 + (x1 - x0) * 0.5f, yo, (x1 - x0) * 0.5f, GY1 - yo);
            bool anySolo = false; for (int q = 0; q < n; ++q) if (get ("b" + String (q) + ".solo") > 0.5f) anySolo = true;
            if (get (bs + "mute") > 0.5f || (anySolo && get (bs + "solo") < 0.5f)) { g.setColour (Colours::black.withAlpha (0.5f)); g.fillRect (x0, GY0, x1 - x0, GY1 - GY0); }
            g.setColour (BC[b]);
            g.drawLine (x0 + 2, yi, x0 + (x1 - x0) * 0.5f - 2, yi, 1.6f); g.drawLine (x0 + (x1 - x0) * 0.5f + 2, yo, x1 - 2, yo, 1.6f);
            if (x1 - x0 > 46)
            {
                g.setFont (theme::font (FontKind::bold, 10.0f));
                g.drawText ("In " + fDb (get (bs + "inG")).replace (" dB", ""), Rectangle<float> (x0 + 4, clampf (yi - 3, GY0 + 9, GY1 - 3) - 10, 70, 12), Justification::centredLeft, false);
                g.drawText ("Out " + fDb (get (bs + "outG")).replace (" dB", ""), Rectangle<float> (x1 - 74, clampf (yo - 3, GY0 + 9, GY1 - 3) - 10, 70, 12), Justification::centredRight, false);
                g.setFont (theme::font (FontKind::mono, 9.0f));
            }
            Path tri1; tri1.addTriangle (x0 + 3, yi, x0 + 3 + 5, yi - 4, x0 + 3 + 5, yi + 4); g.fillPath (tri1);
            Path tri2; tri2.addTriangle (x1 - 3, yo, x1 - 3 - 5, yo - 4, x1 - 3 - 5, yo + 4); g.fillPath (tri2);
        }
        const int fm = (int) get ("m.fft");
        if (fm > 0)
        {
            g.saveState(); g.reduceClipRegion (Rectangle<int> ((int) GX0, (int) GY0, (int) pw, (int) (GY1 - GY0)));
            if (fm == 1 || fm == 2) { Path p; specPath (p, dbPre, GX0, pw); g.setColour (Colour (0xd9c8ccd6)); g.strokePath (p, PathStrokeType (1.2f)); }
            if (fm == 1 || fm == 3) { Path p; specPath (p, dbPost, GX0, pw); g.setColour (theme::green); g.strokePath (p, PathStrokeType (1.4f)); }
            g.restoreState();
        }
        for (int k = 1; k < n; ++k)
        {
            const float x = fxp (ed[(size_t) k], GX0, pw); g.setColour (Colour (0xffe8e9ee)); g.drawLine (x, GY0, x, GY1, 1.5f);
            Path tri; tri.addTriangle (x - 5, GY0, x + 5, GY0, x, GY0 + 7); g.fillPath (tri);
            const bool right = x > GX1 - 60; g.setColour (t.title); g.setFont (theme::font (FontKind::bold, 11.0f));
            g.drawText (fHz (ed[(size_t) k]), Rectangle<float> (right ? x - 86 : x + 6, GY0 + 5, 80, 14), right ? Justification::centredRight : Justification::centredLeft, false);
        }
        g.setColour (t.line); g.drawRect (GX0, GY0, pw, GY1 - GY0, 1.0f);
        g.restoreState();
        g.setColour (t.line); g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    }

    void drawBandCanvas (Graphics& g, const Item& it, Rectangle<float> r, int sub)
    {
        auto& t = T(); const int b = it.band; if (b < 0 || b > 3) return;
        const String bs = "b" + String (b) + ".";
        g.setColour (t.disp); g.fillRoundedRectangle (r, 3.0f); g.setColour (t.line); g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        g.saveState(); g.setOrigin (r.getPosition().toInt() + Point<int> (1, 1));
        const float lvl = proc.meters.bandLvl[b].load(), gr = jmax (0.0f, proc.meters.bandGr[b].load()), lgr = proc.meters.bandLim[b].load();
        if (sub == 0)  // input meter with threshold
        {
            const float w = 271, h = 36; auto X = [&] (float d) { return 6 + (clampf (d, -60, 0) + 60) / 60.0f * (w - 12); };
            g.setFont (theme::font (FontKind::mono, 9.0f)); g.setColour (t.dim);
            const std::pair<float, const char*> tk[] = { { -60, "-inf" }, { -48, "-48" }, { -36, "-36" }, { -24, "-24" }, { -12, "-12" }, { 0, "0" } };
            for (auto& q : tk) { g.drawText (q.second, Rectangle<float> (clampf (X (q.first), 12, w - 8) - 16, 14, 32, 12), Justification::centred, false); g.fillRect (X (q.first), 12.0f, 1.0f, 3.0f); }
            const float ew = X (lvl) - X (-60);
            if (ew > 0)
            {
                ColourGradient gr2 (Colour (0xff6CC9F5), X (-60), 0, Colour (0xffFF1383), X (0), 0, false); gr2.clearColours();
                gr2.addColour (0.0, Colour (0xff6CC9F5)); gr2.addColour (0.8, Colour (0xff6CC9F5)); gr2.addColour (0.801, Colour (0xffA43DFF)); gr2.addColour (0.999, Colour (0xffA43DFF)); gr2.addColour (1.0, Colour (0xffFF1383));
                g.setGradientFill (gr2); g.fillRect (X (-60), 3.0f, ew, 8.0f);
            }
            g.setColour (t.line); g.drawRect (X (-60) - 0.5f, 2.5f, X (0) - X (-60) + 1, 9.0f, 1.0f);
            const float thr = get (bs + "thr"), tx = X (thr);
            g.setColour (Colours::white); g.fillRect (tx - 2, 0.0f, 4.0f, 14.0f);
            g.setColour (t.title); g.setFont (theme::font (FontKind::bold, 10.0f));
            g.drawText ("Threshold " + String (thr, 1) + " dB", Rectangle<float> (8, 23, 150, 12), Justification::centredLeft, false);
            if (get (bs + "lOn") > 0.5f) { g.setColour (theme::orange); g.fillRect (X (get (bs + "lOut")) - 1, 2.0f, 2.0f, 10.0f); }
        }
        else if (sub == 1)  // gain reduction
        {
            const float w = 271, h = 18; auto X = [&] (float d) { return 6 + (48 - clampf (d, 0, 48)) / 48.0f * (w - 12); };
            g.setColour (Colour (0xffCD92FE)); const float gw = X (0) - X (gr); if (gw > 0) g.fillRect (X (gr), 2.0f, gw, 6.0f);
            g.setColour (t.line); g.drawRect (X (48) - 0.5f, 1.5f, X (0) - X (48) + 1, 7.0f, 1.0f);
            g.setColour (t.dim); g.setFont (theme::font (FontKind::mono, 9.0f));
            const std::pair<float, const char*> tk[] = { { 48, "-48" }, { 36, "-36" }, { 24, "-24" }, { 12, "-12" }, { 6, "-6" }, { 0, "0" } };
            for (auto& q : tk) g.drawText (q.second, Rectangle<float> (clampf (X (q.first), 10, w - 6) - 16, h - 11, 32, 12), Justification::centred, false);
            g.setColour (t.ink); g.drawText ("GR " + String (-gr, 1) + " dB", Rectangle<float> (8, -1, 120, 11), Justification::centredLeft, false);
        }
        else if (sub == 2)  // transfer curve
        {
            const float w = 46, h = 40; auto X = [&] (float d) { return 3 + (d + 48) / 48.0f * (w - 6); }; auto Y = [&] (float d) { return h - 3 - (d + 48) / 48.0f * (h - 6); };
            g.setColour (t.line); g.drawRect (3.0f, 3.0f, w - 6, h - 6, 1.0f);
            g.setColour (Colour (0xff4a4d55)); g.drawLine (X (-48), Y (-48), X (0), Y (0), 1.0f);
            const float thr = get (bs + "thr"); const double u = get (bs + "rat"); const double Rr = u >= 0.998 ? 1e9 : 1.0 / (1.0 - u); const bool knee = get (bs + "knee") > 0.5f;
            Path p; bool first = true;
            for (float d = -48; d <= 0; d += 1)
            {
                const double over = d - thr, s = 1.0 / Rr; double gred = 0;
                if (knee) { if (2 * over < -6) gred = 0; else if (2 * std::abs (over) <= 6) { const double uu = over + 3; gred = (1 - s) * uu * uu / 12.0; } else gred = over > 0 ? over * (1 - s) : 0; }
                else gred = over > 0 ? over * (1 - s) : 0;
                const float o = clampf (d - (float) gred, -48, 0); if (first) { p.startNewSubPath (X (d), Y (o)); first = false; } else p.lineTo (X (d), Y (o));
            }
            g.setColour (BC[b]); g.strokePath (p, PathStrokeType (1.8f));
            g.setColour (t.dim); g.setFont (theme::font (FontKind::mono, 8.0f)); g.drawText ("0", Rectangle<float> (w - 12, h - 13, 10, 10), Justification::centredLeft, false);
        }
        else if (sub == 3)  // limiter GR
        {
            const float w = 32, h = 78; auto Y = [&] (float d) { return 6 + d / 12.0f * (h - 12); };
            g.setColour (theme::pink); g.fillRect (2.0f, 6.0f, 8.0f, Y (clampf (lgr, 0, 12)) - 6);
            g.setColour (t.line); g.drawRect (2.5f, 5.5f, 8.0f, h - 12, 1.0f);
            g.setColour (t.dim); g.setFont (theme::font (FontKind::mono, 9.0f));
            for (int d : { 0, 3, 6, 9, 12 }) { g.fillRect (11.0f, Y ((float) d), 3.0f, 1.0f); g.drawText (d == 0 ? "0" : "-" + String (d), Rectangle<float> (15, Y ((float) d) - 6, 20, 12), Justification::centredLeft, false); }
            juce::ignoreUnused (w);
        }
        else  // big display: scope (from level history), gain reduction history or DPC
        {
            const float w = 271, h = 90; const int view = (int) get (bs + "view"); const float zoom = get (bs + "zoom");
            if (view == 2) drawDpc (g, b);
            else
            {
                const int N = jmax (8, roundToInt (H / (1.0f + zoom * 7.0f)));
                g.setFont (theme::font (FontKind::mono, 9.0f));
                if (view == 0)
                {
                    const float cy = h / 2, A = (h - 22) / 2; auto ampY = [&] (float dB) { return clampf ((dB + 54) / 54.0f, 0, 1) * A; };
                    for (int d : { 0, -12, -24, -48 })
                    {
                        const float y = cy - (d + 54) / 54.0f * A; g.setColour (t.line); g.drawHorizontalLine ((int) y, 24, w - 3); g.drawHorizontalLine ((int) (2 * cy - y), 24, w - 3);
                        g.setColour (t.dim); g.drawText (String (d), Rectangle<float> (0, y - 6, 24, 12), Justification::centredLeft, false);
                    }
                    auto X = [&] (int k) { return 26 + (float) k / (float) (N - 1) * (w - 30); };
                    auto draw = [&] (bool compressed, Colour c)
                    {
                        Path p;
                        for (int k = 0; k < N; ++k) { const int i = (hPos + H - N + k) % H; const float v = hLvl[(size_t) b][(size_t) i] - (compressed ? hGr[(size_t) b][(size_t) i] : 0.0f); const float y = cy - ampY (v); if (k) p.lineTo (X (k), y); else p.startNewSubPath (X (k), y); }
                        for (int k = N - 1; k >= 0; --k) { const int i = (hPos + H - N + k) % H; const float v = hLvl[(size_t) b][(size_t) i] - (compressed ? hGr[(size_t) b][(size_t) i] : 0.0f); p.lineTo (X (k), cy + ampY (v)); }
                        p.closeSubPath (); g.setColour (c); g.fillPath (p);
                    };
                    draw (false, Colour (0xd9FF3D5A)); draw (true, Colour (0xd902EBBA));
                    { // grey core: the smaller of input and output level, like the mockup
                        Path p; const float og = get (bs + "outG") + get (bs + "boost");
                        auto vmin = [&] (int i) { return jmin (hLvl[(size_t) b][(size_t) i], hLvl[(size_t) b][(size_t) i] - hGr[(size_t) b][(size_t) i] + og); };
                        for (int k = 0; k < N; ++k) { const int i = (hPos + H - N + k) % H; const float y = cy - ampY (vmin (i)); if (k) p.lineTo (X (k), y); else p.startNewSubPath (X (k), y); }
                        for (int k = N - 1; k >= 0; --k) { const int i = (hPos + H - N + k) % H; p.lineTo (X (k), cy + ampY (vmin (i))); }
                        p.closeSubPath (); g.setColour (Colour (0xe6b9bcc6)); g.fillPath (p);
                    }
                    const float thr = get (bs + "thr"), ty = cy - (clampf (thr, -54, 0) + 54) / 54.0f * A;
                    g.setColour (Colours::white); const float dash[] = { 3.0f, 3.0f };
                    g.drawDashedLine (Line<float> (24, ty, w - 3, ty), dash, 2, 1.0f); g.drawDashedLine (Line<float> (24, 2 * cy - ty, w - 3, 2 * cy - ty), dash, 2, 1.0f);
                }
                else
                {
                    auto Y = [&] (float d) { return 6 + d / 24.0f * (h - 26); };
                    for (int d : { 0, 6, 12, 18, 24 }) { g.setColour (t.line); g.drawHorizontalLine ((int) Y ((float) d), 26, w - 3); g.setColour (t.dim); g.drawText (d == 0 ? "0" : "-" + String (d), Rectangle<float> (0, Y ((float) d) - 6, 24, 12), Justification::centredLeft, false); }
                    Path p; p.startNewSubPath (26, Y (0));
                    for (int k = 0; k < N; ++k) p.lineTo (26 + (float) k / (float) (N - 1) * (w - 30), Y (clampf (hGr[(size_t) b][(size_t) ((hPos + H - N + k) % H)], 0, 24)));
                    p.lineTo (w - 4, Y (0)); p.closeSubPath (); g.setColour (BC[b].withAlpha (0.35f)); g.fillPath (p); g.setColour (BC[b]); g.strokePath (p, PathStrokeType (1.6f));
                }
                g.setColour (t.dim);
                const float secs = (float) N / 60.0f;
                for (float f : { 1.0f, 0.5f, 0.0f }) g.drawText (f == 0.0f ? "now" : "-" + String (secs * f, 2) + "s", Rectangle<float> (clampf (26 + (1 - f) * (w - 30), 24, w - 16) - 20, h - 12, 40, 12), Justification::centred, false);
            }
        }
        g.restoreState();
    }

    static constexpr float DW = 221, DH = 72, DX0 = 4, DX1 = 221 - 22, DY0 = 5, DY1 = 72 - 5;
    void drawDpc (Graphics& g, int b)
    {
        // the pattern editor draws into the full big display area; controls are laid out by the mockup grid
        auto& t = T(); const String bs = "b" + String (b) + ".";
        auto n = proc.getPattern (b); std::sort (n.begin(), n.end(), [] (auto& a, auto& c) { return a.first < c.first; });
        const bool flip = get (bs + "dFlip") > 0.5f; const float pw = DX1 - DX0;
        auto Y = [&] (float y) { return DY0 + (1 - y) * (DY1 - DY0); }; auto X = [&] (float x) { return DX0 + x * pw; };
        g.setColour (t.disp); g.fillRect (0.0f, 0.0f, 271.0f, 90.0f);
        auto area = [&] (auto fn) { g.saveState(); g.setOrigin (0, 14); fn(); g.restoreState(); };
        area ([&]
        {
            static const float gsv[] = { 1.0f / 16, 1.0f / 8, 1.0f / 4 }; const float gs = gsv[jlimit (0, 2, (int) get (bs + "dGrid"))] / 4.0f;
            for (float q = 0; q <= 1.0001f; q += gs) { const bool beat = std::abs (q * 4 - std::round (q * 4)) < 1e-6f; g.setColour (beat ? Colour (0xff4a4d55) : t.line); g.drawVerticalLine ((int) X (q), DY0, DY1); }
            const float str = get (bs + "dStr") / 100.0f; g.setColour (t.dim); g.setFont (theme::font (FontKind::mono, 9.0f));
            g.drawText ("0", Rectangle<float> (DX1 + 3, DY0 - 1, 20, 10), Justification::centredLeft, false);
            g.drawText ("-" + String (roundToInt (24 * str)), Rectangle<float> (DX1 + 3, DY1 - 9, 24, 10), Justification::centredLeft, false);
            Path p; bool first = true;
            for (int k = 0; k <= 128; ++k)
            {
                const float x = (float) k / 128.0f; float y = 1;
                size_t j = 0; auto nn = n; if (nn.size() < 2) nn = { { 0, 1 }, { 1, 1 } };
                while (j + 2 < nn.size() && nn[j + 1].first < x) ++j;
                const auto a = nn[j], c = nn[jmin (j + 1, nn.size() - 1)]; const float dd = c.first - a.first, tt = dd > 1e-6f ? clampf ((x - a.first) / dd, 0, 1) : 1.0f, u = 0.5f - 0.5f * std::cos (MathConstants<float>::pi * tt);
                y = a.second + (c.second - a.second) * u; if (flip) y = 1 - y;
                if (first) { p.startNewSubPath (X (x), Y (y)); first = false; } else p.lineTo (X (x), Y (y));
            }
            g.setColour (BC[b]); g.strokePath (p, PathStrokeType (1.8f));
            for (size_t k = 0; k < n.size(); ++k)
            {
                const bool edge = k == 0 || k + 1 == n.size(); const float y = flip ? 1 - n[k].second : n[k].second;
                g.setColour (edge ? Colour (0xffFF1383) : Colour (0xff6CC9F5)); g.fillEllipse (X (n[k].first) - 3.2f, Y (y) - 3.2f, 6.4f, 6.4f);
                g.setColour (Colours::white); g.drawEllipse (X (n[k].first) - 3.2f, Y (y) - 3.2f, 6.4f, 6.4f, 0.8f);
            }
            const double bpm = 120; juce::ignoreUnused (bpm);
            if (get (bs + "dOn") < 0.5f) { g.setColour (Colours::black.withAlpha (0.45f)); g.fillRect (0.0f, 0.0f, DW, DH); g.setColour (t.ink); g.setFont (theme::font (FontKind::bold, 12.0f)); g.drawText ("DPC is off, press the red lamp", Rectangle<float> (0, 0, DW, DH), Justification::centred, false); }
        });
    }

    void paintCanvas (Graphics& g, const Item& it, Rectangle<float> r)
    {
        if (page == 0)
        {
            const int i = it.cv;
            // order on page 1: pre, graph, post, then per band: in, gr, curve, lim, big
            if (i == 1 || i == 3) { drawVU (g, r, i == 1); return; }
            if (i == 2) { drawGraph (g, r); return; }
            const int sub = (i - 4) % 5;
            if (it.band >= 0 && ! bandActive (it.band)) { g.setColour (T().disp.withAlpha (0.4f)); g.fillRoundedRectangle (r, 3.0f); return; }
            drawBandCanvas (g, it, r, sub);
        }
        else
        {
            if (it.cv == 1 || it.cv == 3) { drawVU (g, r, it.cv == 1); return; }
            drawS (g, r);
        }
    }

    // ----- page 2 big display (spectrum with the two bands, enveloped)
    void drawGraph2 (Graphics& g, Rectangle<float> r)
    {
        auto& t = T();
        g.setColour (t.disp); g.fillRoundedRectangle (r, 3.0f); g.setColour (t.line); g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        g.saveState(); g.reduceClipRegion (r.toNearestInt()); g.setOrigin (r.getPosition().toInt() + Point<int> (1, 1));
        const float w = r.getWidth() - 2, h = r.getHeight() - 2, x0 = 34, x1 = w - 30, pw = x1 - x0, y0 = 6, y1 = h - 16;
        g.setFont (theme::font (FontKind::mono, 9.0f));
        static const float FL[] = { 25, 50, 125, 250, 500, 1000, 2000, 4000, 8000, 12000, 18000 };
        static const char* FT[] = { "25 Hz", "50 Hz", "125 Hz", "250 Hz", "500 Hz", "1 kHz", "2 kHz", "4 kHz", "8 kHz", "12 kHz", "18 kHz" };
        for (int k = 0; k < 11; ++k) { const float x = fxp (FL[k], x0, pw); g.setColour (t.line); g.drawVerticalLine ((int) x, y0, y1); g.setColour (t.dim); g.drawText (FT[k], Rectangle<float> (clampf (x, x0 + 14, x1 - 10) - 25, h - 14, 50, 12), Justification::centred, false); }
        const float split = get ("s.split"), sx = fxp (split, x0, pw);
        g.setColour (theme::blue.withAlpha (0.10f)); g.fillRect (x0, y0, sx - x0, y1 - y0);
        g.setColour (theme::pink.withAlpha (0.10f)); g.fillRect (sx, y0, x1 - sx, y1 - y0);
        auto SPY = [&] (float d) { return y1 - (clampf (d, -96, -6) + 96.0f) / 90.0f * (y1 - y0); };
        auto path = [&] (const std::array<float, 2048>& db)
        {
            Path p; bool first = true; const double fs = proc.sampleRateNow;
            for (int x = 0; x <= (int) pw; ++x)
            {
                const float f0 = xfp (x0 + (float) x, x0, pw), f1 = xfp (x0 + (float) x + 1, x0, pw), b0 = f0 * 4096.0f / (float) fs, b1 = f1 * 4096.0f / (float) fs; float d;
                if (b1 - b0 < 1) { const float bf = clampf ((b0 + b1) * 0.5f, 1, 2046); const int i = (int) bf; const float fr = bf - (float) i; d = db[(size_t) i] * (1 - fr) + db[(size_t) i + 1] * fr; }
                else { float s = 0; int c = 0; for (int k = jmax (1, (int) b0); k <= jmin (2047, (int) std::ceil (b1)); ++k) { s += std::pow (10.0f, db[(size_t) k] / 10.0f); ++c; } d = 10 * std::log10 (s / (float) jmax (1, c) + 1e-12f); }
                if (first) { p.startNewSubPath (x0 + (float) x, SPY (d)); first = false; } else p.lineTo (x0 + (float) x, SPY (d));
            }
            return p;
        };
        g.saveState(); g.reduceClipRegion (Rectangle<int> ((int) x0, (int) y0, (int) pw, (int) (y1 - y0)));
        g.setColour (Colour (0xd9c8ccd6)); g.strokePath (path (dbPre), PathStrokeType (1.2f));
        g.setColour (theme::green); g.strokePath (path (dbPost), PathStrokeType (1.4f));
        g.restoreState();
        g.setColour (Colour (0xffe8e9ee)); g.drawLine (sx, y0, sx, y1, 1.5f);
        g.setColour (t.title); g.setFont (theme::font (FontKind::bold, 11.0f)); g.drawText (fHz (split), Rectangle<float> (sx + 6, y0 + 5, 80, 14), Justification::centredLeft, false);
        // envelope gains of the two bands, live
        g.setFont (theme::font (FontKind::mono, 10.0f));
        const float lg = proc.meters.scLow.load(), hg = proc.meters.scHigh.load();
        g.setColour (theme::blue); g.drawText ("Low gain " + fDb (20 * std::log10 (lg + 1e-9f)), Rectangle<float> (x0 + 8, y0 + 4, 140, 12), Justification::centredLeft, false);
        g.setColour (theme::pink); g.drawText ("High gain " + fDb (20 * std::log10 (hg + 1e-9f)), Rectangle<float> (x1 - 150, y0 + 4, 142, 12), Justification::centredRight, false);
        g.setColour (t.line); g.drawRect (x0, y0, pw, y1 - y0, 1.0f);
        g.restoreState();
    }


    // ================================================================ page 2 displays
    struct Pn { int k; float x, y, w, h; int e; };
    struct PB { float ix, iy, iw, ih; };
    static constexpr float SW = 920, SH = 284;
    std::vector<Pn> panels() const
    {
        const int v = (int) get ("s.view"); const float W = SW, H = SH, gap = 6, w2 = (W - gap) / 2, h2 = (H - gap) / 2, w3 = (W - 2 * gap) / 3;
        switch (v)
        {
            case 0: return { { 0, 0, 0, W, H, 0 } };
            case 1: return { { 1, 0, 0, W, H, 0 } };
            case 2: return { { 1, 0, 0, W, H, 1 } };
            case 3: return { { 0, 0, 0, w2, H, 0 }, { 1, w2 + gap, 0, w2, H, 1 } };
            case 4: return { { 0, 0, 0, w2, H, 0 }, { 1, w2 + gap, 0, w2, H, 0 } };
            case 5: return { { 1, 0, 0, w2, H, 0 }, { 1, w2 + gap, 0, w2, H, 1 } };
            case 6: return { { 0, 0, 0, w2, H, 0 }, { 1, w2 + gap, 0, w2, h2, 0 }, { 1, w2 + gap, h2 + gap, w2, h2, 1 } };
            case 7: return { { 1, 0, 0, w3, H, 0 }, { 1, w3 + gap, 0, w3, H, 1 }, { 0, 2 * (w3 + gap), 0, w3, H, 0 } };
            default: return { { 2, 0, 0, W, H, 0 } };
        }
    }
    static PB plotBox (const Pn& p)
    {
        if (p.k == 0) return { p.x + 38, p.y + 8, p.w - 38 - 30, p.h - 8 - 16 };
        if (p.k == 1) return { p.x + 44, p.y + 18, p.w - 44 - 10, p.h - 18 - 16 };
        return { p.x + 10, p.y + 10, p.w - 20, p.h - 20 };
    }
    double spb() const { return 60.0 / jmax (1.0, proc.bpmNow.load()); }
    double spanMs() const
    {
        const int s = (int) get ("s.span"); static const double m[] = { 0, 1, 2, 4 };
        if (s > 0) return m[s] * spb() * 1000.0;
        auto tot = [&] (int e) { const String k = e ? "s.h" : "s.l"; return (double) (get (k + "Atk") + get (k + "Hold") + get (k + "Rel")); };
        const double b = jmax (tot (0), tot (1)) / (spb() * 1000.0) * 1.08;
        return (b <= 1 ? 1 : b <= 2 ? 2 : 4) * spb() * 1000.0;
    }
    double gridMs() const { static const double g[] = { .25, .5, 1, 2 }; return g[jlimit (0, 3, (int) get ("s.grid"))] * spb() * 1000.0; }
    multipaw::Env envOf (int e) const
    {
        const String k = e ? "s.h" : "s.l"; const double fs = proc.sampleRateNow;
        return { get (k + "Str") / 100.0, get (k + "Atk") * 0.001 * fs, get (k + "Hold") * 0.001 * fs, get (k + "Rel") * 0.001 * fs, get (k + "AtkC"), get (k + "RelC") };
    }
    static float eqY (float g, float iy, float ih) { return iy + (18 - clampf (g, -18, 18)) / 36.0f * ih; }
    float eqResp (float f) const
    {
        const double fs = proc.sampleRateNow; const int ft = (int) get ("s.ftype"); double m = 1;
        auto mag = [&] (const multipaw::Biq& q)
        {
            const double w = 2 * MathConstants<double>::pi * f / fs; const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2 * w);
            return std::abs ((q.b0 + q.b1 * z1 + q.b2 * z2) / (1.0 + q.a1 * z1 + q.a2 * z2));
        };
        for (int i = 0; i < 2; ++i)
        {
            const double fq = get (i ? "s.e2f" : "s.e1f"), g = get (i ? "s.e2g" : "s.e1g"), q = get (i ? "s.e2w" : "s.e1w");
            multipaw::Biq a, b;
            if (ft == 1) { a.peak (fs, fq, -std::max (6.0, std::abs (g)) * 1.5, std::max (q, 2.0)); b.peak (fs, fq, 0, 1); }
            else if (ft == 2) { a.peak (fs, fq, g, q); b.peak (fs, fq, g, q); }
            else { a.peak (fs, fq, g, q); b.peak (fs, fq, 0, 1); }
            m *= mag (a) * mag (b);
        }
        return 20.0f * std::log10 ((float) m + 1.0e-9f);
    }
    void drawFFTPanel (Graphics& g, const Pn& p)
    {
        auto& t = T(); const auto b = plotBox (p);
        g.saveState(); g.reduceClipRegion (Rectangle<float> (p.x, p.y, p.w, p.h).toNearestInt());
        g.setColour (t.disp); g.fillRect (p.x, p.y, p.w, p.h); g.setFont (theme::font (FontKind::mono, 9.0f));
        for (int d : { 18, 12, 6, 0, -6, -12, -18 })
        {
            const float y = eqY ((float) d, b.iy, b.ih); g.setColour (d == 0 ? Colour (0xff4a4d55) : t.line); g.drawHorizontalLine ((int) y, b.ix, b.ix + b.iw);
            g.setColour (t.dim); g.drawText (String (d > 0 ? "+" : "") + String (d) + "dB", Rectangle<float> (b.ix - 40, y - 6, 37, 12), Justification::centredRight, false);
        }
        for (float f : { 50.f, 100.f, 200.f, 400.f, 800.f, 1600.f, 3200.f, 6400.f, 12800.f })
        {
            const float x = fxp (f, b.ix, b.iw); g.setColour (t.line); g.drawVerticalLine ((int) x, b.iy, b.iy + b.ih);
            g.setColour (t.dim); g.drawText (f >= 1000 ? String (f / 1000.0f).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + "k" : String ((int) f), Rectangle<float> (x - 20, p.y + p.h - 12, 40, 12), Justification::centred, false);
        }
        for (int d : { -80, -60, -40, -20 }) { const float y = b.iy + b.ih - (d + 96.0f) / 90.0f * b.ih; g.setColour (t.dim); g.drawText (String (d), Rectangle<float> (b.ix + b.iw + 3, y - 6, 30, 12), Justification::centredLeft, false); g.fillRect (b.ix + b.iw, y, 2.0f, 1.0f); }
        g.drawText ("dBFS", Rectangle<float> (b.ix + b.iw + 2, b.iy, 34, 10), Justification::centredLeft, false);
        const float sx = fxp (get ("s.split"), b.ix, b.iw);
        g.setColour (Colour (0x17FF7BB1)); g.fillRect (sx, b.iy, b.ix + b.iw - sx, b.ih);
        g.setColour (Colour (0x12FFA600)); g.fillRect (b.ix, b.iy, sx - b.ix, b.ih);
        {
            Path sp, fill; const double fs = proc.sampleRateNow; bool first = true; float lastY = b.iy + b.ih;
            auto SPY = [&] (float d) { return b.iy + b.ih - (clampf (d, -96, -6) + 96.0f) / 90.0f * b.ih; };
            for (int x = 0; x <= (int) b.iw; ++x)
            {
                const float f0 = xfp (b.ix + (float) x, b.ix, b.iw), f1 = xfp (b.ix + (float) x + 1, b.ix, b.iw), b0 = f0 * 4096.0f / (float) fs, b1 = f1 * 4096.0f / (float) fs; float d;
                if (b1 - b0 < 1) { const float bf = clampf ((b0 + b1) * 0.5f, 1, 2046); const int i = (int) bf; const float fr = bf - (float) i; d = dbPre[(size_t) i] * (1 - fr) + dbPre[(size_t) i + 1] * fr; }
                else { float s = 0; int c = 0; for (int k = jmax (1, (int) b0); k <= jmin (2047, (int) std::ceil (b1)); ++k) { s += std::pow (10.0f, dbPre[(size_t) k] / 10.0f); ++c; } d = 10 * std::log10 (s / (float) jmax (1, c) + 1e-12f); }
                lastY = SPY (d); if (first) { sp.startNewSubPath (b.ix + (float) x, lastY); first = false; } else sp.lineTo (b.ix + (float) x, lastY);
            }
            fill = sp; fill.lineTo (b.ix + b.iw, b.iy + b.ih); fill.lineTo (b.ix, b.iy + b.ih); fill.closeSubPath();
            g.setColour (Colour (0x29c8ccd6)); g.fillPath (fill); g.setColour (Colour (0xe6d7dae2)); g.strokePath (sp, PathStrokeType (1.1f));
        }
        { Path c; for (int x = 0; x <= (int) b.iw; x += 3) { const float y = eqY (eqResp (xfp (b.ix + (float) x, b.ix, b.iw)), b.iy, b.ih); if (x) c.lineTo (b.ix + (float) x, y); else c.startNewSubPath (b.ix, y); } g.setColour (Colour (0xffe8c340)); g.strokePath (c, PathStrokeType (2.0f)); }
        for (int n = 1; n <= 2; ++n)
        {
            const float x = fxp (get ("s.e" + String (n) + "f"), b.ix, b.iw), y = eqY (get ("s.e" + String (n) + "g"), b.iy, b.ih);
            g.setColour (Colour (0xff2a2f55)); g.fillEllipse (x - 6, y - 6, 12, 12); g.setColour (Colours::white); g.drawEllipse (x - 6, y - 6, 12, 12, 1.5f);
            g.setFont (theme::font (FontKind::bold, 9.0f)); g.drawText (String (n), Rectangle<float> (x - 6, y - 6, 12, 12), Justification::centred, false);
        }
        { const float dash[] = { 4.0f, 3.0f }; g.setColour (Colours::white); g.drawDashedLine (Line<float> (sx, b.iy, sx, b.iy + b.ih), dash, 2, 1.0f); Path tri; tri.addTriangle (sx - 5, b.iy, sx + 5, b.iy, sx, b.iy + 7); g.fillPath (tri); }
        const bool right = sx > b.ix + b.iw - 90; g.setFont (theme::font (FontKind::bold, 10.0f)); g.setColour (t.title);
        g.drawText ("Split " + fHz (get ("s.split")), Rectangle<float> (right ? sx - 86 : sx + 6, b.iy + 5, 80, 14), right ? Justification::centredRight : Justification::centredLeft, false);
        g.setColour (Colour (0xffFFA600)); g.drawText ("LOW", Rectangle<float> (b.ix + 4, b.iy + 1, 60, 12), Justification::centredLeft, false);
        g.setColour (Colour (0xffFF7BB1)); g.drawText ("HIGH", Rectangle<float> (b.ix + b.iw - 64, b.iy + 1, 60, 12), Justification::centredRight, false);
        g.setColour (t.line); g.drawRect (b.ix, b.iy, b.iw, b.ih, 1.0f);
        g.restoreState();
    }
    struct Hd { char t; char w; float x, y; };
    std::vector<Hd> envHandles (const Pn& p) const
    {
        const auto b = plotBox (p); const double sm = spanMs(); const int e = p.e; const String k = e ? "s.h" : "s.l";
        const double str = get (k + "Str") / 100.0, atk = get (k + "Atk"), hold = get (k + "Hold"), rel = get (k + "Rel"), ac = get (k + "AtkC"), rc = get (k + "RelC");
        auto X = [&] (double ms) { return b.ix + (float) (ms / sm) * b.iw; }; auto Y = [&] (double d) { return b.iy + (float) (-jlimit (-30.0, 0.0, d)) / 30.0f * b.ih; };
        const double dd = 20 * std::log10 (std::max (1 - str, 0.0316));
        auto cf = [] (double x, double c) { return std::pow (jlimit (0.0, 1.0, x), std::pow (2.0, -c / 33)); };
        std::vector<Hd> out { { 'a', 0, X (atk), Y (dd) }, { 'h', 0, X (atk + hold), Y (dd) }, { 'r', 0, X (atk + hold + rel), Y (0) } };
        out.push_back ({ 'c', 'r', X (atk + hold + rel / 2), Y (20 * std::log10 (std::max (1 - str * (1 - cf (0.5, rc)), 0.0316))) });
        if (atk > 0.4) out.push_back ({ 'c', 'a', X (atk / 2), Y (20 * std::log10 (std::max (1 - str * cf (0.5, ac), 0.0316))) });
        return out;
    }
    void drawEnvPanel (Graphics& g, const Pn& p)
    {
        auto& t = T(); const auto b = plotBox (p); const double sm = spanMs();
        g.saveState(); g.reduceClipRegion (Rectangle<float> (p.x, p.y, p.w, p.h).toNearestInt());
        g.setColour (t.disp); g.fillRect (p.x, p.y, p.w, p.h); g.setFont (theme::font (FontKind::mono, 9.0f));
        auto Y = [&] (double d) { return b.iy + (float) (-jlimit (-30.0, 0.0, d)) / 30.0f * b.ih; }; auto X = [&] (double tm) { return b.ix + (float) (tm / sm) * b.iw; };
        for (int d : { 0, -3, -6, -12, -18, -24 }) { g.setColour (d == 0 ? Colour (0xff4a4d55) : t.line); g.drawHorizontalLine ((int) Y (d), b.ix, b.ix + b.iw); g.setColour (t.dim); g.drawText (String (d) + "dB", Rectangle<float> (b.ix - 40, Y (d) - 6, 37, 12), Justification::centredRight, false); }
        const double gm = gridMs(); g.setColour (t.line); for (double tm = 0; tm <= sm + 1; tm += gm) g.drawVerticalLine ((int) X (tm), b.iy, b.iy + b.ih);
        const std::pair<const char*, double> bt[] = { { "1/16", .25 }, { "1/8", .5 }, { "1/4", 1 }, { "1/2", 2 } };
        for (auto& q : bt) { const double ms = q.second * spb() * 1000.0; if (ms <= sm) { g.setColour (t.title); g.drawText (q.first, Rectangle<float> (X (ms) - 20, p.y + 3, 40, 12), Justification::centred, false); g.setColour (Colour (0xff4a4d55)); g.drawVerticalLine ((int) X (ms), b.iy, b.iy + b.ih); } }
        for (float f : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            g.setColour (t.dim); const auto j = f == 0 ? Justification::centredLeft : f == 1 ? Justification::centredRight : Justification::centred;
            g.drawText (String (roundToInt (sm * f)) + (f == 1 ? " ms" : ""), Rectangle<float> (b.ix + f * b.iw - 30, p.y + p.h - 12, 60, 12), j, false);
        }
        const double fs = proc.sampleRateNow;
        auto curve = [&] (int e, Colour col, float al, bool fillIt)
        {
            const auto E = envOf (e); Path c, f; c.startNewSubPath (b.ix, Y (0));
            for (int x = 0; x <= (int) b.iw; ++x)
            {
                const double tm = (double) x / b.iw * sm, gg = multipaw::envGain (E, tm * 0.001 * fs), d = 20 * std::log10 (std::max (gg, 0.0316));
                if (x) c.lineTo (b.ix + (float) x, Y (d)); else c.startNewSubPath (b.ix, Y (d));
            }
            if (fillIt) { f = c; f.lineTo (b.ix + b.iw, Y (0)); f.closeSubPath(); g.setColour (col.withAlpha (al * 0.5f)); g.fillPath (f); }
            g.setColour (col.withAlpha (al)); g.strokePath (c, PathStrokeType (fillIt ? 2.0f : 1.2f));
        };
        const int mine = p.e; const Colour EC[2] { Colour (0xffFFA600), Colour (0xffFF7BB1) };
        curve (1 - mine, EC[1 - mine], 0.35f, false); curve (mine, EC[mine], 1.0f, true);
        for (const auto& h : envHandles (p))
        {
            g.setColour (h.t == 'c' ? Colour (0xff6CC9F5) : Colour (0xff2a2f55));
            if (h.t == 'c') g.fillEllipse (h.x - 4, h.y - 4, 8, 8); else g.fillRect (h.x - 5, h.y - 5, 10.0f, 10.0f);
            g.setColour (Colours::white); if (h.t == 'c') g.drawEllipse (h.x - 4, h.y - 4, 8, 8, 1.3f); else g.drawRect (h.x - 5, h.y - 5, 10.0f, 10.0f, 1.3f);
        }
        g.setFont (theme::font (FontKind::bold, 13.0f).withStyle (Font::italic)); g.setColour (EC[mine].withAlpha (0.55f));
        g.drawText (String (mine ? "High" : "Low") + " band envelope", Rectangle<float> (b.ix + b.iw - 206, b.iy + b.ih - 22, 200, 16), Justification::centredRight, false);
        g.setFont (theme::font (FontKind::mono, 9.0f)); g.setColour (t.dim);
        g.drawText ("Depth " + String (20 * std::log10 (std::max (1 - get (String (mine ? "s.h" : "s.l") + "Str") / 100.0f, 0.0316f)), 1) + " dB", Rectangle<float> (b.ix + 6, b.iy + b.ih - 20, 120, 12), Justification::centredLeft, false);
        g.setColour (t.line); g.drawRect (b.ix, b.iy, b.iw, b.ih, 1.0f);
        g.restoreState();
    }
    void drawSys (Graphics& g, const Pn& p)
    {
        auto& t = T(); g.setColour (t.disp); g.fillRect (p.x, p.y, p.w, p.h);
        g.setFont (theme::font (FontKind::regular, 13.0f)); g.setColour (t.title); g.drawText ("SYSTEM VIEW: trigger monitor", Rectangle<float> (16, 14, 400, 16), Justification::centredLeft, false);
        const char* tn[] = { "Audio", "MIDI", "Host Sync" }; const char* gn[] = { "1/16", "1/8", "1/4", "1/2" };
        const StringArray lines { String ("Trigger mode: ") + tn[jlimit (0, 2, (int) get ("s.trig"))], "Sens threshold: " + String (-6 - get ("s.sens") / 100.0f * 54, 1) + " dBFS",
                                  String ("Grid: ") + gn[jlimit (0, 3, (int) get ("s.grid"))] + " (Host Sync rate), offset " + String (roundToInt (get ("s.offset") * 100)) + "%",
                                  "Split: " + fHz (get ("s.split")) + " (LR4)", "Latency: none (page 2 has no lookahead)", "Tempo: " + String (proc.bpmNow.load(), 1) + " BPM" };
        g.setColour (t.ink); for (int i = 0; i < lines.size(); ++i) g.drawText (lines[i], Rectangle<float> (16, 40.0f + (float) i * 18, 300, 16), Justification::centredLeft, false);
    }
    void drawS (Graphics& g, Rectangle<float> r)
    {
        auto& t = T();
        g.setColour (t.disp); g.fillRoundedRectangle (r, 3.0f);
        g.saveState(); g.reduceClipRegion (r.toNearestInt()); g.setOrigin (r.getPosition().toInt() + Point<int> (1, 1));
        for (const auto& p : panels()) { if (p.k == 0) drawFFTPanel (g, p); else if (p.k == 1) drawEnvPanel (g, p); else drawSys (g, p); }
        g.restoreState(); g.setColour (t.line); g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    }
    // page 2 mouse
    struct SDrag { char t = 0; int n = 0; Pn p {}; Hd h {}; } sd;
    SDrag sHit (Point<float> m) const
    {
        for (const auto& p : panels())
        {
            if (m.x < p.x || m.x > p.x + p.w || m.y < p.y || m.y > p.y + p.h) continue;
            if (p.k == 0)
            {
                const auto b = plotBox (p);
                for (int n = 1; n <= 2; ++n) { const float x = fxp (get ("s.e" + String (n) + "f"), b.ix, b.iw), y = eqY (get ("s.e" + String (n) + "g"), b.iy, b.ih); if (std::hypot (m.x - x, m.y - y) < 10) { SDrag d; d.t = 'q'; d.n = n; d.p = p; return d; } }
                const float sx = fxp (get ("s.split"), b.ix, b.iw); if (std::abs (m.x - sx) < 6 && m.y > b.iy && m.y < b.iy + b.ih) { SDrag d; d.t = 's'; d.p = p; return d; }
            }
            else if (p.k == 1) for (const auto& h : envHandles (p)) if (std::hypot (m.x - h.x, m.y - h.y) < 9) { SDrag d; d.t = 'e'; d.p = p; d.h = h; return d; }
        }
        return {};
    }
    void linkSet (int e, const char* what, float v)
    {
        const String a = String (e ? "s.h" : "s.l") + what, o = String (e ? "s.l" : "s.h") + what;
        set (a, v); if (get ("s.link") > 0.5f) set (o, v);
    }
    void sDown (const Item& it, Point<float> p)
    {
        sd = sHit ({ p.x - it.r.x - 1, p.y - it.r.y - 1 });
        if (sd.t == 'q') { begin ("s.e" + String (sd.n) + "f"); begin ("s.e" + String (sd.n) + "g"); }
        else if (sd.t == 's') begin ("s.split");
        else if (sd.t == 'e') { for (const char* w : { "Atk", "Hold", "Rel", "Str", "AtkC", "RelC" }) { begin (String ("s.l") + w); begin (String ("s.h") + w); } }
    }
    void sDrag (const Item& it, Point<float> pt)
    {
        if (! sd.t) return; const Point<float> m { pt.x - it.r.x - 1, pt.y - it.r.y - 1 }; const auto b = plotBox (sd.p);
        if (sd.t == 'q')
        {
            set ("s.e" + String (sd.n) + "f", xfp (clampf (m.x, b.ix, b.ix + b.iw), b.ix, b.iw));
            const float d = 18 - (m.y - b.iy) / b.ih * 36; set ("s.e" + String (sd.n) + "g", std::abs (d) < 0.4f ? 0.0f : clampf (d, -18, 18));
        }
        else if (sd.t == 's') set ("s.split", xfp (clampf (m.x, b.ix, b.ix + b.iw), b.ix, b.iw));
        else if (sd.t == 'e')
        {
            const int e = sd.p.e; const String k = e ? "s.h" : "s.l"; const double sm = spanMs();
            double tm = (m.x - b.ix) / b.iw * sm; const double gm = gridMs(); const double sn = std::round (tm / gm) * gm;
            if (std::abs (sn - tm) / sm * b.iw < 7) tm = sn; tm = std::max (0.0, tm);
            const double dBy = -jlimit (0.0, 30.0, (m.y - b.iy) / b.ih * 30.0), gain = std::pow (10.0, dBy / 20);
            const double atk = get (k + "Atk"), hold = get (k + "Hold"), str = std::max (get (k + "Str") / 100.0f, 0.05f);
            if (sd.h.t == 'a') { linkSet (e, "Atk", (float) jlimit (0.0, 200.0, tm)); linkSet (e, "Str", (float) jlimit (0.0, 100.0, (1 - gain) * 100)); }
            else if (sd.h.t == 'h') { linkSet (e, "Hold", (float) jlimit (0.0, 2000.0, tm - atk)); linkSet (e, "Str", (float) jlimit (0.0, 100.0, (1 - gain) * 100)); }
            else if (sd.h.t == 'r') linkSet (e, "Rel", (float) jlimit (1.0, 4000.0, tm - atk - hold));
            else if (sd.h.t == 'c')
            {
                const double cfv = sd.h.w == 'r' ? jlimit (0.002, 0.998, 1 - (1 - gain) / str) : jlimit (0.002, 0.998, (1 - gain) / str);
                linkSet (e, sd.h.w == 'r' ? "RelC" : "AtkC", (float) jlimit (-100.0, 100.0, -33 * std::log2 (std::log (cfv) / std::log (0.5))));
            }
        }
    }
    void sUp()
    {
        if (sd.t == 'q') { end ("s.e" + String (sd.n) + "f"); end ("s.e" + String (sd.n) + "g"); }
        else if (sd.t == 's') end ("s.split");
        else if (sd.t == 'e') for (const char* w : { "Atk", "Hold", "Rel", "Str", "AtkC", "RelC" }) { end (String ("s.l") + w); end (String ("s.h") + w); }
        sd = {};
    }

    // ---------------------------------------------------------------- the whole window
    void paintAll (Graphics& g)
    {
        auto& t = T();
        g.fillAll (t.bg);
        g.drawImage (woodL, Rectangle<float> (0, 0, 52, (float) (layout::H - layout::RIG)));
        g.drawImage (woodR, Rectangle<float> (1228, 0, 52, (float) (layout::H - layout::RIG)));
        const auto* boxes = page == 0 ? layout::boxes0 : layout::boxes1; const int nb = page == 0 ? (int) std::size (layout::boxes0) : (int) std::size (layout::boxes1);
        for (int i = 0; i < nb; ++i)
        {
            const auto& b = boxes[i]; const String cls (b.cls);
            if (cls == "rig" || cls == "tag") continue;
            auto r = R (b.r);
            if (cls == "main") r.setHeight (r.getHeight() - layout::RIG);
            if (cls == "foot") r.translate (0, -(float) layout::RIG);
            if (cls == "ms") { // band mute/solo strips: follow the crossovers
                continue; }
            const auto bg = argb (b.bg);
            if (cls == "hdr") g.setColour (t.plate2); else if (cls == "main") g.setColour (t.plate); else g.setColour (bg);
            if (b.rad > 0) g.fillRoundedRectangle (r, b.rad); else g.fillRect (r);
            if (b.bd > 0) { g.setColour (cls == "flip" ? theme::selb : cls == "hdr" || cls == "foot" ? t.line : argb (b.bc)); if (b.rad > 0) g.drawRoundedRectangle (r.reduced (0.5f), b.rad, b.bd); else if (cls == "foot") g.drawHorizontalLine ((int) r.getY(), r.getX(), r.getRight()); else g.drawRect (r, b.bd); }
            if (cls == "hdr") { g.setColour (t.line); g.drawHorizontalLine ((int) r.getBottom() - 1, r.getX(), r.getRight()); }
        }
        // band strips: grey out inactive bands
        if (page == 0)
        {
            const float sx[4] = { 60, 352, 643, 935 };
            for (int b = bands(); b < 4; ++b) { g.setColour (Colours::black.withAlpha (0.5f)); g.fillRoundedRectangle (sx[b], 304, 286, 344, 4.0f); }
            drawMuteSolo (g);
        }
        // static texts
        const auto* tx = page == 0 ? layout::txts0 : layout::txts1; const int nt = page == 0 ? (int) std::size (layout::txts0) : (int) std::size (layout::txts1);
        for (int i = 0; i < nt; ++i)
        {
            const auto& s = tx[i]; if (String (s.cls) == "nm" || String (s.cls) == "SPAN") continue;
            if (page == 0 && s.r.y > 300 && s.r.y < 660 && bandOfX (s.r.x) >= bands()) continue;
            FontKind fk = s.mono ? FontKind::mono : s.fw >= 800 ? FontKind::brand : s.fw >= 700 ? FontKind::bold : s.fw >= 600 ? FontKind::semi : FontKind::regular;
            auto f = theme::font (fk, s.fs); if (s.ls != 0) f = f.withExtraKerningFactor (s.ls / s.fs);
            g.setFont (f); g.setColour (argb (s.col)); String str = U (s.s); if (s.upper) str = str.toUpperCase();
            g.drawText (str, Rectangle<float> (s.r.x, s.r.y - 2, s.r.w + 30, s.r.h + 4), Justification::centredLeft, false);
        }
        if (logo) logo->drawWithin (g, Rectangle<float> (62, 7, 32, 32), RectanglePlacement::centred, 1.0f);
        for (size_t i = 0; i < items.size(); ++i) paintItem (g, items[i], (int) i);
        paintDynamicText (g);
        // footer
        g.setColour (t.dim); g.setFont (theme::font (FontKind::regular, 12.0f).withExtraKerningFactor (1.0f / 12.0f));
        g.drawText (U ("Studio 87 \xc2\xb7 MultiPaw Compressor v") + JucePlugin_VersionString, Rectangle<float> (64, 696 - layout::RIG, 500, 24), Justification::centredLeft, false);
        g.drawText (U ("1280 \xc3\x97 680 px"), Rectangle<float> (1028, 696 - layout::RIG, 200, 24), Justification::centredRight, false);
        if (toastTicks > 0) { auto r = Rectangle<float> (440, 640, 400, 26); g.setColour (Colours::black); g.fillRoundedRectangle (r, 5); g.setColour (theme::selb); g.drawRoundedRectangle (r.reduced (0.5f), 5, 1); g.setColour (t.ink); g.setFont (theme::font (FontKind::regular, 14.0f)); g.drawText (toastText, r, Justification::centred, false); }
        if (tips && tipText.isNotEmpty() && tipItem >= 0)
        {
            const int tw = 296; const float lh = 18.0f;
            AttributedString as; as.setWordWrap (AttributedString::byWord); as.append (tipText, theme::font (FontKind::regular, 14.0f), t.ink);
            TextLayout tl; tl.createLayout (as, (float) tw - 20.0f);
            const float th = tl.getHeight() + 14.0f; float x = jlimit (56.0f, 1224.0f - tw, tipPos.x), y = tipPos.y + 22.0f;
            if (y + th > (float) (layout::H - layout::RIG) - 4) y = tipPos.y - th - 8;
            auto r = Rectangle<float> (x, y, (float) tw, th); g.setColour (Colours::black); g.fillRoundedRectangle (r, 5); g.setColour (theme::selb); g.drawRoundedRectangle (r.reduced (0.5f), 5, 1);
            tl.draw (g, r.reduced (10.0f, 7.0f)); juce::ignoreUnused (lh);
        }
    }
    int bandOfX (float x) const { return x < 346 ? 0 : x < 637 ? 1 : x < 929 ? 2 : 3; }
    struct MS { int band; bool solo; Rectangle<float> r; };
    std::vector<MS> msRects;
    void drawMuteSolo (Graphics& g)
    {
        // the strips under the graph follow the crossovers
        const int n = bands(); auto ed2 = edges(); const float gx = 172 + 1 + GX0, pw = GX1 - GX0; auto& t = T();
        msRects.clear();
        for (int i = 0; i < n; ++i)
        {
            const float x0 = gx + fxp (ed2[(size_t) i], 0, pw), x1 = gx + fxp (ed2[(size_t) i + 1], 0, pw);
            auto r = Rectangle<float> (x0, 275, x1 - x0, 22);
            g.setColour (BC[i].withAlpha (0.10f)); g.fillRect (r); g.setColour (BC[i].withAlpha (0.6f)); g.drawRect (r, 1.0f);
            const bool narrow = r.getWidth() < 128;
            for (int k = 0; k < 2; ++k)
            {
                const bool solo = k == 1; const String txt = narrow ? (solo ? "S" : "M") : (solo ? "Solo" : "Mute");
                const float w = narrow ? 30.0f : 52.0f, gap = narrow ? 2.0f : 8.0f;
                const float bx = r.getCentreX() - w - gap * 0.5f + (float) k * (w + gap);
                auto cell = Rectangle<float> (bx, r.getY() + 2, w, 18.0f);
                const bool on = get ("b" + String (i) + (solo ? ".solo" : ".mute")) > 0.5f;
                auto box = Rectangle<float> (cell.getX() + 3, cell.getCentreY() - 6.5f, 13.0f, 13.0f);
                g.setColour (on ? BC[i] : t.disp); g.fillRoundedRectangle (box, 2.0f);
                g.setColour (on ? BC[i] : t.dim); g.drawRoundedRectangle (box.reduced (0.5f), 2.0f, 1.0f);
                g.setColour (t.ink); g.setFont (theme::font (FontKind::regular, 12.0f));
                g.drawText (txt, Rectangle<float> (box.getRight() + 4, cell.getY(), cell.getRight() - box.getRight() - 4, cell.getHeight()), Justification::centredLeft, false);
                msRects.push_back ({ i, solo, cell });
            }
        }
    }
    void paintDynamicText (Graphics& g)
    {
        auto& t = T();
        g.setFont (theme::font (FontKind::regular, 11.0f)); g.setColour (t.dim);
        if (page == 0)
        {
            g.drawText ("Latency " + String ((double) proc.latencyNow() * 1000.0 / jmax (1.0, proc.sampleRateNow), 1) + " ms", Rectangle<float> (60, 255, 106, 12), Justification::centred, false);
            g.drawText ("Limiter GR " + String (-std::max (0.0f, proc.meters.limGr.load()) - 0.0f, 1).replace ("-0.0", "0.0") + " dB", Rectangle<float> (1116, 285, 100, 12), Justification::centred, false);
            for (int b = 0; b < bands(); ++b)
            {
                const float x = b == 0 ? 176 : b == 1 ? 467 : b == 2 ? 759 : 1050;
                g.setColour (t.disp); g.fillRoundedRectangle (x, 314, 116, 22, 3); g.setColour (t.line); g.drawRoundedRectangle (x + 0.5f, 314.5f, 115, 21, 3, 1);
                g.setColour (t.ink); g.setFont (theme::font (FontKind::regular, 14.0f)); g.drawText (bandPresetName (b), Rectangle<float> (x, 314, 116, 22), Justification::centred, false);
            }
            g.setColour (t.dim); g.setFont (theme::font (FontKind::regular, 11.0f));
            g.drawText (mixNote(), Rectangle<float> (627, 52, 150, 28), Justification::topLeft, true);
        }
    }
    String bandPresetName (int b) const { const int k = (int) get ("b" + String (b) + ".sel"); return k >= 0 && k < presets::kNumBand ? String (presets::kBand[k].name) : String ("- Select -"); }
    String mixNote() const { const float m = get ("m.mix"); return m <= 1 ? "Dry: the compressor is bypassed by the Mix knob." : m >= 99 ? "Fully compressed." : "New York tip: 50% Mix with fast, deep compression"; }


    // ---------------------------------------------------------------- presets
    bool pageKey (const String& id, int pg) const
    {
        const bool sc = id.startsWith ("s.");
        if (pg == 1 != sc) return false;
        for (const char* suf : { ".view", ".zoom", ".fft", ".vu", ".span", ".on" }) if (id.endsWith (suf)) return false;
        return true;
    }
    std::vector<int> bankOf (int pg) const { std::vector<int> v; for (int i = 0; i < presets::kNumPre; ++i) if (presets::kPre[i].page == pg) v.push_back (i); return v; }
    void applyPreset (int idx)
    {
        const auto& pr = presets::kPre[idx]; const int pg = pr.page;
        auto each = [&] (auto fn) { for (const auto& dd : mp::kParams) fn (String (dd.id)); for (const auto& dd : mp::kUi) fn (String (dd.id)); };
        each ([&] (const String& id) { if (pageKey (id, pg)) { const auto* dd = defOf (id); if (dd) setUser (id, dd->df); } });
        for (int i = 0; i < pr.n; ++i) setUser (pr.kv[i].id, pr.kv[i].v);
        setUser (pg == 0 ? "m.on" : "s.on", 1.0f);
        if (pg == 0) for (int b = 0; b < 4; ++b) proc.setPattern (b, patternNodes ((int) get ("b" + String (b) + ".patI")));
        presetName[pg] = pr.name; proc.setSetting (pg == 0 ? setting::preset1 : setting::preset2, pr.name);
        toast (String ("Loaded ") + pr.name); ed.repaint();
    }
    void stepPreset (int dir)
    {
        auto bank = bankOf (page); if (bank.empty()) return;
        int cur = -1; for (size_t i = 0; i < bank.size(); ++i) if (presetName[page] == presets::kPre[bank[i]].name) cur = (int) i;
        cur = cur < 0 ? (dir > 0 ? 0 : (int) bank.size() - 1) : (cur + dir + (int) bank.size()) % (int) bank.size();
        applyPreset (bank[(size_t) cur]);
    }
    void presetMenu (Point<int> where)
    {
        PopupMenu m; StringArray types;
        auto bank = bankOf (page);
        for (int i : bank) types.addIfNotAlreadyThere (presets::kPre[i].type);
        for (const auto& ty : types)
        {
            PopupMenu sub;
            for (int i : bank) if (String (presets::kPre[i].type) == ty) sub.addItem (i + 1, presets::kPre[i].name, true, presetName[page] == presets::kPre[i].name);
            m.addSubMenu (ty, sub);
        }
        m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (Rectangle<int> (where.x, where.y, 1, 1)), [this] (int r) { if (r > 0) applyPreset (r - 1); });
    }
    void applyBand (int b, int k)
    {
        const auto& bp = presets::kBand[k]; const String bs = "b" + String (b) + ".";
        for (int i = 0; i < bp.n; ++i) setUser (bs + bp.kv[i].id, bp.kv[i].v);
        setUser (bs + "sel", (float) k);
    }
    void stepBand (int b, int dir)
    {
        int k = (int) get ("b" + String (b) + ".sel");
        k = k < 0 ? (dir > 0 ? 0 : presets::kNumBand - 1) : (k + dir + presets::kNumBand) % presets::kNumBand; applyBand (b, k);
    }
    void bandMenu (int b, Point<int> where)
    {
        PopupMenu m; const int cur = (int) get ("b" + String (b) + ".sel");
        for (int k = 0; k < presets::kNumBand; ++k) m.addItem (k + 1, presets::kBand[k].name, true, k == cur);
        m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (Rectangle<int> (where.x, where.y, 1, 1)), [this, b] (int r) { if (r > 0) applyBand (b, r - 1); });
    }

    // ---------------------------------------------------------------- actions
    void toast (const String& s) { toastText = s; toastTicks = 70; ed.repaint(); }
    void flip (int p) { page = p; proc.setSetting (setting::page, p); build(); ed.repaint(); }
    void systemMenu (Point<int> where)
    {
        PopupMenu m; const String hdr = "System";
        m.addSectionHeader (hdr);
        m.addItem (1, "Linear phase crossovers (36 ms latency at default slope)", true, get ("m.linear") > 0.5f);
        m.addItem (2, "RMS detector", true, get ("m.rms") > 0.5f);
        m.addItem (3, "Autogain (make-up from threshold and ratio)", true, get ("m.auto") > 0.5f);
        m.addItem (4, "Ratio in steps", true, get ("m.steps") > 0.5f);
        PopupMenu la; for (int i = 0; i < 4; ++i) { const float v[] = { 1.0f, 2.5f, 5.0f, 7.5f }; la.addItem (10 + i, String (v[i], 1) + " ms", true, std::abs (get ("m.la") - v[i]) < 0.01f); }
        m.addSubMenu ("Limiter lookahead (latency)", la);
        m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (Rectangle<int> (where.x, where.y, 1, 1)), [this] (int r)
        {
            if (r == 1) setUser ("m.linear", get ("m.linear") > 0.5f ? 0.0f : 1.0f);
            else if (r == 2) setUser ("m.rms", get ("m.rms") > 0.5f ? 0.0f : 1.0f);
            else if (r == 3) setUser ("m.auto", get ("m.auto") > 0.5f ? 0.0f : 1.0f);
            else if (r == 4) setUser ("m.steps", get ("m.steps") > 0.5f ? 0.0f : 1.0f);
            else if (r >= 10 && r < 14) { const float v[] = { 1.0f, 2.5f, 5.0f, 7.5f }; setUser ("m.la", v[r - 10]); }
        });
    }
    void doAction (const Item& it, Point<int> screenPt)
    {
        const String a = it.action;
        const String bs = "b" + String (it.band) + ".";
        if (a == "pgA") flip (0);
        else if (a == "pgB") flip (1);
        else if (a == "bBypass") setUser ("m.bypass", get ("m.bypass") > 0.5f ? 0.0f : 1.0f);
        else if (a == "Keep") proc.setSetting ("ui.keep", ! (bool) proc.getSetting ("ui.keep", false));
        else if (a == "System") systemMenu (screenPt);
        else if (a == "Mute" && it.band >= 0 && it.r.y < 330) setUser (muteSoloBandId (it, "mute"), get (muteSoloBandId (it, "mute")) > 0.5f ? 0.0f : 1.0f);
        else if (a == "Solo" && it.band >= 0 && it.r.y < 330) setUser (muteSoloBandId (it, "solo"), get (muteSoloBandId (it, "solo")) > 0.5f ? 0.0f : 1.0f);
        else if (a == "M") setUser ("b3.mute", get ("b3.mute") > 0.5f ? 0.0f : 1.0f);
        else if (a == "S" && it.r.y < 330) setUser ("b3.solo", get ("b3.solo") > 0.5f ? 0.0f : 1.0f);
        else if (a == "H" && it.band >= 0) setUser (bs + "knee", 0.0f);
        else if (a == "S" && it.band >= 0) setUser (bs + "knee", 1.0f);
        else if (a == "Scope") setUser (bs + "view", 0.0f);
        else if (a == "Gain Red.") setUser (bs + "view", 1.0f);
        else if (a == "DPC") setUser (bs + "view", 2.0f);
        else if (page == 1 && viewIndex (a) >= 0) setUser ("s.view", (float) viewIndex (a));
        else if (a == "pPrev") stepPreset (-1);
        else if (a == "pNext") stepPreset (1);
        else if (a == "pName") presetMenu (screenPt);
        else if (a == "pSave") toast ("Saving presets arrives with the next build");
        else if (a == "bSettings") settingsMenu (screenPt);
        else if (a == "bGuide" || a == "bAbout") aboutBox (a);
        else if (it.band >= 0 && it.text.contains (U ("\xe2\x97\x80"))) stepBand (it.band, -1);
        else if (it.band >= 0 && it.text.contains (U ("\xe2\x96\xb6"))) stepBand (it.band, 1);
    }
    String muteSoloBandId (const Item& it, const char* what) const
    {
        // the strips under the graph follow the crossovers: pick the band by x position
        const int n = bands(); auto ed = edges(); const float gx = 172 + 1 + GX0, pw = GX1 - GX0; const float cx = it.r.x + it.r.w * 0.5f;
        for (int i = 0; i < n; ++i) { const float x0 = gx + fxp (ed[(size_t) i], 0, pw), x1 = gx + fxp (ed[(size_t) i + 1], 0, pw); if (cx >= x0 && cx <= x1) return "b" + String (i) + "." + what; }
        return "b0." + String (what);
    }
    void settingsMenu (Point<int> where)
    {
        PopupMenu m; m.addSectionHeader ("Settings");
        m.addItem (1, "Info bubbles", true, tips);
        PopupMenu lk; lk.addItem (11, "A  neon navy", true, look == 'A'); lk.addItem (12, "B  purple soda pop", true, look == 'B'); lk.addItem (13, "C  charcoal", true, look == 'C');
        m.addSubMenu ("Colour look", lk);
        m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (Rectangle<int> (where.x, where.y, 1, 1)), [this] (int r)
        {
            if (r == 1) { tips = ! tips; proc.setSetting (setting::tips, tips); }
            else if (r >= 11 && r <= 13) { look = (char) ('A' + (r - 11)); proc.setSetting (setting::look, String::charToString ((juce::juce_wchar) look)); }
            ed.repaint();
        });
    }
    void aboutBox (const String& which)
    {
        String msg;
        if (which == "bGuide") msg = "Page 1: multiband compressor. Page 2: multiband sidechain (pumping and ducking). Hover any control for an info bubble. Double-click a knob to reset it; Shift while dragging for fine steps. Keys: B bypass, P flip pages. Double-click a crossover line, level handle or EQ point to reset it. Mix at 0% is the untouched, delayed input.";
        else if (which == "bSettings") msg = "Info bubbles and colour looks: use the System menu for engine options. (Look and bubble settings dialog arrives with the next build.)";
        else msg = "MultiPaw Compressor by Studio 87. Free and open source (AGPL-3.0-or-later). No telemetry, no data collection. https://github.com/SpeakingColors/multipawcompressor";
        auto opts = MessageBoxOptions().withIconType (MessageBoxIconType::NoIcon).withTitle ("MultiPaw Compressor").withMessage (msg).withButton ("OK");
        AlertWindow::showAsync (opts, nullptr);
    }

    // ---------------------------------------------------------------- mouse
    int hitTest (Point<float> p) const
    {
        for (int i = (int) items.size() - 1; i >= 0; --i)
        {
            const auto& it = items[(size_t) i]; if (! bandActive (it.band)) continue;
            if (it.cls == "cap" || it.cls == "mixnote" || it.cls == "nm") continue;
            if (R (it.r).expanded (it.kind == Radio ? 0.0f : 0.0f).contains (p)) return i;
        }
        return -1;
    }
    Point<float> toLayout (Point<float> p) const { const float s = (float) ed.getWidth() / 1280.0f; return { p.x / s, p.y / s }; }
    void down (const MouseEvent& e)
    {
        const auto p = toLayout (e.position); dragItem = -1; dragKind = {};
        if (page == 0) for (const auto& m : msRects) if (m.r.contains (p)) { const String id = "b" + String (m.band) + (m.solo ? ".solo" : ".mute"); setUser (id, get (id) > 0.5f ? 0.0f : 1.0f); return; }
        if (page == 0) for (int b = 0; b < bands(); ++b) { const float x = b == 0 ? 176.0f : b == 1 ? 467.0f : b == 2 ? 759.0f : 1050.0f; if (Rectangle<float> (x, 314, 116, 22).contains (p)) { bandMenu (b, ed.localPointToGlobal (e.getPosition())); return; } }
        const int i = hitTest (p);
        if (i < 0) return;
        const auto& it = items[(size_t) i]; dragItem = i; dragStart = e.position; dragGesture = false;
        switch (it.kind)
        {
            case Knob: dragStartNorm = posOf (it.pid, get (it.pid)); begin (it.pid); dragGesture = true; break;
            case Toggle: case Power: setUser (it.pid, get (it.pid) > 0.5f ? 0.0f : 1.0f); break;
            case Radio: case Seg: setUser (it.pid, (float) it.idx); break;
            case Select:
            {
                PopupMenu m; const int cur = (int) std::round (get (it.pid)) - it.off;
                for (int k = 0; k < it.opts.size(); ++k) m.addItem (k + 1, it.opts[k], true, k == cur);
                const String pid = it.pid; const int off = it.off;
                m.showMenuAsync (PopupMenu::Options().withTargetComponent (&ed).withTargetScreenArea (ed.localAreaToGlobal (Rectangle<float> (R (it.r).getX() * (float) ed.getWidth() / 1280.0f, R (it.r).getBottom() * (float) ed.getWidth() / 1280.0f, 1, 1).toNearestInt())),
                                [this, pid, off] (int r) { if (r > 0) setUser (pid, (float) (r - 1 + off)); });
                break;
            }
            case Btn: doAction (it, ed.localPointToGlobal (e.getPosition())); break;
            case Cv: canvasDown (it, p, e); break;
        }
    }
    void canvasDown (const Item& it, Point<float> p, const MouseEvent&)
    {
        if (page == 1 && it.cv == 2) { sDown (it, p); return; }
        if (page == 0 && it.cv == 1) { clipPre[0] = clipPre[1] = false; return; }
        if (page == 0 && it.cv == 3) { clipPost[0] = clipPost[1] = false; return; }
        if (page == 0 && it.cv == 2)
        {
            const float mx = p.x - it.r.x - 1, my = p.y - it.r.y - 1; const int n = bands(); auto ed2 = edges(); const float pw = GX1 - GX0;
            for (int k = 1; k < n; ++k) if (std::abs (mx - fxp (ed2[(size_t) k], GX0, pw)) < 6 && my > GY0 && my < GY1) { dragKind = "x"; dragBand = k - 1; begin ("m.x" + String (k)); return; }
            for (int b = 0; b < n; ++b)
            {
                const float x0 = fxp (ed2[(size_t) b], GX0, pw), x1 = fxp (ed2[(size_t) b + 1], GX0, pw); if (mx < x0 || mx > x1) continue;
                const float yi = YDB (get ("b" + String (b) + ".inG")), yo = YDB (get ("b" + String (b) + ".outG")), wl = x0 + (x1 - x0) * 0.5f;
                if (mx < wl && std::abs (my - yi) < 9) { dragKind = "in"; dragBand = b; begin ("b" + String (b) + ".inG"); return; }
                if (mx >= wl && std::abs (my - yo) < 9) { dragKind = "out"; dragBand = b; begin ("b" + String (b) + ".outG"); return; }
            }
            return;
        }
        if (page == 0 && it.band >= 0 && (it.cv - 4) % 5 == 0) { dragKind = "thr"; dragBand = it.band; begin ("b" + String (it.band) + ".thr"); thrSet (it, p); return; }
        if (page == 0 && it.band >= 0 && (it.cv - 4) % 5 == 4 && (int) get ("b" + String (it.band) + ".view") == 2) { dpcDown (it, p); return; }
    }
    void thrSet (const Item& it, Point<float> p)
    {
        const float mx = p.x - it.r.x - 1; const float d = -60 + (mx - 6) / (271 - 12) * 60;
        set ("b" + String (it.band) + ".thr", std::round (clampf (d, -48, 0) * 2) / 2);
    }
    // DPC pattern editor: lives in the lower part of the big display
    void dpcDown (const Item& it, Point<float> p)
    {
        const int b = it.band; auto n = proc.getPattern (b); const bool flip = get ("b" + String (b) + ".dFlip") > 0.5f;
        const float mx = p.x - it.r.x - 1, my = p.y - it.r.y - 1 - 14;
        int hit = -1; float bd = 64;
        for (size_t k = 0; k < n.size(); ++k)
        {
            const float y = flip ? 1 - n[k].second : n[k].second; const float dx = DX0 + n[k].first * (DX1 - DX0) - mx, dy = DY0 + (1 - y) * (DY1 - DY0) - my, d = dx * dx + dy * dy;
            if (d < bd) { bd = d; hit = (int) k; }
        }
        if (hit < 0)
        {
            if (n.size() >= 21) return;
            float x = clampf ((mx - DX0) / (DX1 - DX0), 0, 1), y = clampf (1 - (my - DY0) / (DY1 - DY0), 0, 1); if (flip) y = 1 - y;
            n.push_back ({ x, y }); std::sort (n.begin(), n.end(), [] (auto& a, auto& c) { return a.first < c.first; });
            for (size_t k = 0; k < n.size(); ++k) if (n[k].first == x && n[k].second == y) hit = (int) k;
            proc.setPattern (b, n);
        }
        dragKind = "dpc"; dragDpcBand = b; dragDpcNode = hit;
    }
    void drag (const MouseEvent& e)
    {
        if (dragItem < 0 || dragItem >= (int) items.size()) return;
        const auto& it = items[(size_t) dragItem]; const float s = (float) ed.getWidth() / 1280.0f;
        if (it.kind == Knob)
        {
            const double dy = -(e.position.y - dragStart.y) / s;
            set (it.pid, valOf (it.pid, dragStartNorm + dy / (e.mods.isShiftDown() ? 1500.0 : 170.0)));
        }
        else if (it.kind == Cv)
        {
            const auto p = toLayout (e.position);
            if (page == 1 && it.cv == 2) { sDrag (it, p); return; }
            if (dragKind == "x")
            {
                const float mx = p.x - it.r.x - 1; float f = xfp (clampf (mx, GX0, GX1), GX0, GX1 - GX0);
                const float g = 1.18f; const auto x = xs(); const int k = dragBand;
                const float lo = k == 0 ? 20.0f : x[(size_t) k - 1] * g, hi = k == bands() - 2 ? 20000.0f / g : x[(size_t) k + 1] / g;
                set ("m.x" + String (k + 1), clampf (f, lo, jmax (lo, hi)));
            }
            else if (dragKind == "in" || dragKind == "out")
            {
                const float my = p.y - it.r.y - 1; const float d = clampf (DBY (my), -24, 8);
                set ("b" + String (dragBand) + "." + (dragKind == "in" ? "inG" : "outG"), std::abs (d) < 0.6f ? 0.0f : d);
            }
            else if (dragKind == "thr") thrSet (it, p);
            else if (dragKind == "dpc")
            {
                auto n = proc.getPattern (dragDpcBand); if (dragDpcNode < 0 || dragDpcNode >= (int) n.size()) return;
                const float mx = p.x - it.r.x - 1, my = p.y - it.r.y - 1 - 14; const bool flip = get ("b" + String (dragDpcBand) + ".dFlip") > 0.5f;
                float x = clampf ((mx - DX0) / (DX1 - DX0), 0, 1), y = clampf (1 - (my - DY0) / (DY1 - DY0), 0, 1); if (flip) y = 1 - y;
                const bool first = dragDpcNode == 0, last = dragDpcNode == (int) n.size() - 1;
                static const float gsv[] = { 1.0f / 16, 1.0f / 8, 1.0f / 4 }; const float gs = gsv[jlimit (0, 2, (int) get ("b" + String (dragDpcBand) + ".dGrid"))] / 4.0f;
                n[(size_t) dragDpcNode].second = y; if (! (first || last)) n[(size_t) dragDpcNode].first = clampf (std::round (x / gs) * gs, 0, 1);
                proc.setPattern (dragDpcBand, n);
            }
        }
    }
    void up()
    {
        sUp();
        if (dragItem >= 0 && dragItem < (int) items.size())
        {
            const auto& it = items[(size_t) dragItem];
            if (it.kind == Knob) end (it.pid);
            if (dragKind == "x") end ("m.x" + String (dragBand + 1));
            else if (dragKind == "in") end ("b" + String (dragBand) + ".inG");
            else if (dragKind == "out") end ("b" + String (dragBand) + ".outG");
            else if (dragKind == "thr") end ("b" + String (dragBand) + ".thr");
        }
        dragItem = -1; dragKind = {};
    }
    void dbl (const MouseEvent& e)
    {
        const auto p = toLayout (e.position); const int i = hitTest (p); if (i < 0) return;
        const auto& it = items[(size_t) i];
        if (it.kind == Knob) { const auto* d = defOf (it.pid); if (d) setUser (it.pid, d->df); }
        else if (it.kind == Cv && page == 1 && it.cv == 2)
        {
            const auto h = sHit ({ p.x - it.r.x - 1, p.y - it.r.y - 1 });
            if (h.t == 'q') { setUser ("s.e" + String (h.n) + "g", 0.0f); setUser ("s.e" + String (h.n) + "w", 1.0f); } else if (h.t == 's') setUser ("s.split", 1050.0f);
        }
        else if (it.kind == Cv && page == 0 && it.cv == 2)
        {
            const float mx = p.x - it.r.x - 1, my = p.y - it.r.y - 1; const int n = bands(); auto ed2 = edges(); const float pw = GX1 - GX0;
            static const float defs[] = { 110, 500, 5000 };
            for (int k = 1; k < n; ++k) if (std::abs (mx - fxp (ed2[(size_t) k], GX0, pw)) < 6) { setUser ("m.x" + String (k), defs[k - 1]); return; }
            for (int b = 0; b < n; ++b)
            {
                const float x0 = fxp (ed2[(size_t) b], GX0, pw), x1 = fxp (ed2[(size_t) b + 1], GX0, pw); if (mx < x0 || mx > x1) continue;
                const float wl = x0 + (x1 - x0) * 0.5f; const float yi = YDB (get ("b" + String (b) + ".inG")), yo = YDB (get ("b" + String (b) + ".outG"));
                if (mx < wl && std::abs (my - yi) < 9) setUser ("b" + String (b) + ".inG", 0.0f);
                if (mx >= wl && std::abs (my - yo) < 9) setUser ("b" + String (b) + ".outG", 0.0f);
            }
        }
        else if (it.kind == Cv && page == 0 && it.band >= 0 && (it.cv - 4) % 5 == 0) setUser ("b" + String (it.band) + ".thr", -18.0f);
        else if (it.kind == Cv && page == 0 && it.band >= 0 && (it.cv - 4) % 5 == 4 && (int) get ("b" + String (it.band) + ".view") == 2)
        {
            auto n = proc.getPattern (it.band); if (n.size() <= 2) return;
            const float mx = p.x - it.r.x - 1, my = p.y - it.r.y - 1 - 14; const bool flip = get ("b" + String (it.band) + ".dFlip") > 0.5f; int hit = -1; float bd = 64;
            for (size_t k = 0; k < n.size(); ++k) { const float y = flip ? 1 - n[k].second : n[k].second; const float dx = DX0 + n[k].first * (DX1 - DX0) - mx, dy = DY0 + (1 - y) * (DY1 - DY0) - my, d = dx * dx + dy * dy; if (d < bd) { bd = d; hit = (int) k; } }
            if (hit > 0 && hit < (int) n.size() - 1) { n.erase (n.begin() + hit); proc.setPattern (it.band, n); }
        }
    }
    void wheel (const MouseEvent& e, const MouseWheelDetails& w)
    {
        const int i = hitTest (toLayout (e.position)); if (i < 0) return; const auto& it = items[(size_t) i];
        if (it.kind == Cv && page == 1 && it.cv == 2)
        {
            const auto p = toLayout (e.position); const auto h = sHit ({ p.x - it.r.x - 1, p.y - it.r.y - 1 });
            if (h.t == 'q') { const String id = "s.e" + String (h.n) + "w"; setUser (id, get (id) * (w.deltaY > 0 ? 1.12f : 1 / 1.12f)); }
            return;
        }
        if (it.kind != Knob) return;
        const float dy = w.isReversed ? -w.deltaY : w.deltaY; if (std::abs (dy) < 1.0e-6f) return;
        const auto* d = defOf (it.pid); if (! d) return;
        begin (it.pid);
        set (it.pid, d->isInt ? get (it.pid) + (dy > 0 ? 1.0f : -1.0f) : valOf (it.pid, posOf (it.pid, get (it.pid)) + (dy > 0 ? 1 : -1) * (e.mods.isShiftDown() ? 0.002 : 0.02)));
        end (it.pid);
    }
    void move (const MouseEvent& e)
    {
        const auto p = toLayout (e.position); const int i = hitTest (p);
        if (i != hot) { hot = i; tipTicks = 0; tipText = {}; tipItem = -1; ed.repaint(); }
        tipPos = p;
        if (i >= 0)
        {
            const auto& it = items[(size_t) i];
            const bool vert = it.kind == Knob; ed.setMouseCursor (it.kind == Knob ? MouseCursor::UpDownResizeCursor : MouseCursor::PointingHandCursor);
            juce::ignoreUnused (vert);
        }
        else ed.setMouseCursor (MouseCursor::NormalCursor);
    }
    void tick()
    {
        // 30 Hz: histories, spectrum, meters, tooltips
        for (int b = 0; b < 4; ++b) { hLvl[(size_t) b][(size_t) hPos] = proc.meters.bandLvl[b].load(); hGr[(size_t) b][(size_t) hPos] = proc.meters.bandGr[b].load(); }
        hPos = (hPos + 1) % H;
        updateSpectrum();
        if (hot >= 0 && tipText.isEmpty() && ++tipTicks > 20) { tipText = items[(size_t) hot].tip; tipItem = hot; }
        if (toastTicks > 0) --toastTicks;
        ed.repaint();
    }
};

// ================================================================================================
MultiPawEditor::MultiPawEditor (MultiPawProcessor& p) : AudioProcessorEditor (p), d (std::make_unique<Impl> (*this, p))
{
    setOpaque (true);
    setSize (1280, layout::H - layout::RIG);
    setResizable (true, true);
    setResizeLimits (640, 340, 2560, 1360);
    getConstrainer()->setFixedAspectRatio (1280.0 / (layout::H - layout::RIG));
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}
MultiPawEditor::~MultiPawEditor() = default;
void MultiPawEditor::paint (Graphics& g)
{
    const float s = (float) getWidth() / 1280.0f;
    g.addTransform (AffineTransform::scale (s));
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    d->paintAll (g);
}
void MultiPawEditor::resized() {}
void MultiPawEditor::timerCallback() { d->tick(); }
void MultiPawEditor::mouseDown (const MouseEvent& e) { d->down (e); }
void MultiPawEditor::mouseDrag (const MouseEvent& e) { d->drag (e); }
void MultiPawEditor::mouseUp (const MouseEvent&) { d->up(); }
void MultiPawEditor::mouseMove (const MouseEvent& e) { d->move (e); }
void MultiPawEditor::mouseExit (const MouseEvent&) { d->hot = -1; d->tipText = {}; d->tipItem = -1; repaint(); }
void MultiPawEditor::mouseDoubleClick (const MouseEvent& e) { d->dbl (e); }
void MultiPawEditor::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) { d->wheel (e, w); }
bool MultiPawEditor::keyPressed (const KeyPress& k)
{
    if (k.getTextCharacter() == 'p' || k.getTextCharacter() == 'P') { d->flip (d->page == 0 ? 1 : 0); return true; }
    if (k.getTextCharacter() == 'b' || k.getTextCharacter() == 'B') { d->setUser ("m.bypass", d->get ("m.bypass") > 0.5f ? 0.0f : 1.0f); return true; }
    return false;
}
