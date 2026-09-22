#pragma once

#include <QString>
#include <cstdint>
#include <cstdio>
#include <vector>

struct TuiColor {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    constexpr TuiColor() : r(0), g(0), b(0) {}
    constexpr TuiColor(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue) {}

    static TuiColor fromHex(const char *hex) {
        if (!hex || hex[0] != '#' || strlen(hex) < 7) return TuiColor(0, 0, 0);
        unsigned int ir = 0, ig = 0, ib = 0;
        sscanf(hex + 1, "%02x%02x%02x", &ir, &ig, &ib);
        return TuiColor(static_cast<uint8_t>(ir), static_cast<uint8_t>(ig), static_cast<uint8_t>(ib));
    }

    bool operator==(const TuiColor &other) const {
        return r == other.r && g == other.g && b == other.b;
    }

    bool operator!=(const TuiColor &other) const {
        return !(*this == other);
    }
};

enum class TuiPaletteId {
    TokyoNight = 0,
    Dracula,
    Gruvbox,
    RetroCyan,
    RetroGreen,
    RetroAmber,
    CleanDark,
    CleanLight,
    Count
};

struct TuiPalette {
    TuiPaletteId id = TuiPaletteId::TokyoNight;
    QString name;
    TuiColor bg;
    TuiColor panelBg;
    TuiColor headerBg;
    TuiColor border;
    TuiColor borderFocus;
    TuiColor text;
    TuiColor textDim;
    TuiColor textBold;
    TuiColor accent;
    TuiColor online;
    TuiColor offline;
    TuiColor warning;
    TuiColor error;
    TuiColor highlight;
    TuiColor selectionBg;
    TuiColor selectionFg;
    TuiColor progressFill;
    TuiColor progressEmpty;

    static TuiPalette get(TuiPaletteId id) {
        TuiPalette p;
        p.id = id;
        switch (id) {
        case TuiPaletteId::TokyoNight:
            p.name = "BTOP TOKYO NIGHT";
            p.bg = TuiColor(22, 22, 30);           // #16161e
            p.panelBg = TuiColor(26, 27, 38);      // #1a1b26
            p.headerBg = TuiColor(36, 40, 59);     // #24283b
            p.border = TuiColor(86, 95, 137);      // #565f89
            p.borderFocus = TuiColor(125, 207, 255); // #7dcfff (cyan)
            p.text = TuiColor(192, 202, 245);      // #c0caf5
            p.textDim = TuiColor(86, 95, 137);     // #565f89
            p.textBold = TuiColor(255, 255, 255);
            p.accent = TuiColor(125, 207, 255);    // #7dcfff
            p.online = TuiColor(158, 206, 106);    // #9ece6a (green)
            p.offline = TuiColor(86, 95, 137);     // #565f89 (gray)
            p.warning = TuiColor(224, 175, 104);   // #e0af68 (yellow)
            p.error = TuiColor(247, 118, 142);     // #f7768e (red)
            p.highlight = TuiColor(187, 154, 247); // #bb9af7 (purple)
            p.selectionBg = TuiColor(40, 52, 90);  // #28345a
            p.selectionFg = TuiColor(255, 255, 255);
            p.progressFill = TuiColor(125, 207, 255);
            p.progressEmpty = TuiColor(41, 46, 66);
            break;

        case TuiPaletteId::Dracula:
            p.name = "BTOP DRACULA";
            p.bg = TuiColor(33, 34, 44);           // #21222c
            p.panelBg = TuiColor(40, 42, 54);      // #282a36
            p.headerBg = TuiColor(52, 55, 70);
            p.border = TuiColor(98, 114, 164);     // #6272a4
            p.borderFocus = TuiColor(189, 147, 249); // #bd93f9 (purple)
            p.text = TuiColor(248, 248, 242);      // #f8f8f2
            p.textDim = TuiColor(98, 114, 164);
            p.textBold = TuiColor(255, 255, 255);
            p.accent = TuiColor(189, 147, 249);
            p.online = TuiColor(80, 250, 123);     // #50fa7b
            p.offline = TuiColor(98, 114, 164);
            p.warning = TuiColor(241, 250, 140);   // #f1fa8c
            p.error = TuiColor(255, 85, 85);       // #ff5555
            p.highlight = TuiColor(139, 233, 253); // #8be9fd (cyan)
            p.selectionBg = TuiColor(68, 71, 90);  // #44475a
            p.selectionFg = TuiColor(255, 255, 255);
            p.progressFill = TuiColor(80, 250, 123);
            p.progressEmpty = TuiColor(55, 58, 74);
            break;

        case TuiPaletteId::Gruvbox:
            p.name = "BTOP GRUVBOX";
            p.bg = TuiColor(29, 32, 33);           // #1d2021
            p.panelBg = TuiColor(40, 40, 40);      // #282828
            p.headerBg = TuiColor(60, 56, 54);     // #3c3836
            p.border = TuiColor(124, 111, 100);    // #7c6f64
            p.borderFocus = TuiColor(254, 128, 25); // #fe8019 (orange)
            p.text = TuiColor(235, 219, 178);      // #ebdbb2
            p.textDim = TuiColor(146, 131, 116);   // #928374
            p.textBold = TuiColor(251, 241, 199);
            p.accent = TuiColor(254, 128, 25);     // #fe8019
            p.online = TuiColor(184, 187, 38);     // #b8bb26 (green)
            p.offline = TuiColor(124, 111, 100);
            p.warning = TuiColor(250, 189, 47);    // #fabd2f (yellow)
            p.error = TuiColor(251, 73, 52);       // #fb4934 (red)
            p.highlight = TuiColor(142, 192, 124); // #8ec07c (aqua)
            p.selectionBg = TuiColor(80, 73, 69);  // #504945
            p.selectionFg = TuiColor(251, 241, 199);
            p.progressFill = TuiColor(254, 128, 25);
            p.progressEmpty = TuiColor(50, 48, 47);
            break;

        case TuiPaletteId::RetroGreen:
            p.name = "MATRIX PHOSPHOR GREEN";
            p.bg = TuiColor(6, 10, 6);             // #060a06
            p.panelBg = TuiColor(9, 15, 9);        // #090f09
            p.headerBg = TuiColor(12, 22, 12);
            p.border = TuiColor(0, 100, 35);       // dark green
            p.borderFocus = TuiColor(0, 255, 102); // neon green #00ff66
            p.text = TuiColor(0, 255, 102);
            p.textDim = TuiColor(0, 136, 43);      // #00882b
            p.textBold = TuiColor(100, 255, 160);
            p.accent = TuiColor(0, 255, 102);
            p.online = TuiColor(0, 255, 102);
            p.offline = TuiColor(0, 80, 25);
            p.warning = TuiColor(150, 255, 50);
            p.error = TuiColor(255, 80, 80);
            p.highlight = TuiColor(50, 255, 136);
            p.selectionBg = TuiColor(0, 51, 17);
            p.selectionFg = TuiColor(150, 255, 180);
            p.progressFill = TuiColor(0, 255, 102);
            p.progressEmpty = TuiColor(0, 40, 15);
            break;

        case TuiPaletteId::RetroCyan:
            p.name = "CYBER CYAN CRT";
            p.bg = TuiColor(5, 13, 20);            // #050d14
            p.panelBg = TuiColor(8, 20, 32);       // #081420
            p.headerBg = TuiColor(12, 30, 48);
            p.border = TuiColor(0, 110, 140);
            p.borderFocus = TuiColor(0, 240, 255); // #00f0ff
            p.text = TuiColor(0, 240, 255);
            p.textDim = TuiColor(0, 120, 145);
            p.textBold = TuiColor(200, 250, 255);
            p.accent = TuiColor(0, 240, 255);
            p.online = TuiColor(0, 255, 180);
            p.offline = TuiColor(0, 70, 95);
            p.warning = TuiColor(255, 215, 0);
            p.error = TuiColor(255, 80, 110);
            p.highlight = TuiColor(199, 146, 234);
            p.selectionBg = TuiColor(10, 41, 66);
            p.selectionFg = TuiColor(220, 250, 255);
            p.progressFill = TuiColor(0, 240, 255);
            p.progressEmpty = TuiColor(10, 30, 45);
            break;

        case TuiPaletteId::RetroAmber:
            p.name = "AMBER CRT TERMINAL";
            p.bg = TuiColor(16, 10, 2);            // #100a02
            p.panelBg = TuiColor(23, 14, 3);       // #170e03
            p.headerBg = TuiColor(35, 20, 4);
            p.border = TuiColor(120, 75, 0);
            p.borderFocus = TuiColor(255, 176, 0); // #ffb000
            p.text = TuiColor(255, 176, 0);
            p.textDim = TuiColor(138, 90, 0);
            p.textBold = TuiColor(255, 215, 60);
            p.accent = TuiColor(255, 176, 0);
            p.online = TuiColor(255, 208, 0);
            p.offline = TuiColor(80, 50, 0);
            p.warning = TuiColor(255, 230, 80);
            p.error = TuiColor(255, 90, 40);
            p.highlight = TuiColor(255, 220, 100);
            p.selectionBg = TuiColor(56, 32, 0);
            p.selectionFg = TuiColor(255, 230, 120);
            p.progressFill = TuiColor(255, 176, 0);
            p.progressEmpty = TuiColor(40, 25, 5);
            break;

        case TuiPaletteId::CleanDark:
            p.name = "CLEAN DARK SLATE";
            p.bg = TuiColor(30, 30, 46);           // #1e1e2e
            p.panelBg = TuiColor(24, 24, 37);      // #181825
            p.headerBg = TuiColor(40, 40, 60);
            p.border = TuiColor(88, 91, 112);      // #585b70
            p.borderFocus = TuiColor(137, 180, 250); // #89b4fa
            p.text = TuiColor(205, 214, 244);      // #cdd6f4
            p.textDim = TuiColor(108, 112, 134);   // #6c7086
            p.textBold = TuiColor(255, 255, 255);
            p.accent = TuiColor(137, 180, 250);
            p.online = TuiColor(166, 227, 161);    // #a6e3a1
            p.offline = TuiColor(88, 91, 112);
            p.warning = TuiColor(249, 226, 175);   // #f9e2af
            p.error = TuiColor(243, 139, 168);     // #f38ba8
            p.highlight = TuiColor(203, 166, 247); // #cba6f7
            p.selectionBg = TuiColor(49, 50, 68);
            p.selectionFg = TuiColor(255, 255, 255);
            p.progressFill = TuiColor(137, 180, 250);
            p.progressEmpty = TuiColor(45, 45, 60);
            break;

        case TuiPaletteId::CleanLight:
            p.name = "CLEAN LIGHT";
            p.bg = TuiColor(239, 241, 245);        // #eff1f5
            p.panelBg = TuiColor(230, 233, 239);   // #e6e9ef
            p.headerBg = TuiColor(215, 218, 225);
            p.border = TuiColor(156, 160, 176);    // #9ca0b0
            p.borderFocus = TuiColor(30, 102, 245); // #1e66f5
            p.text = TuiColor(76, 79, 105);        // #4c4f69
            p.textDim = TuiColor(140, 143, 161);   // #8c8fa1
            p.textBold = TuiColor(20, 20, 30);
            p.accent = TuiColor(30, 102, 245);
            p.online = TuiColor(64, 160, 43);      // #40a02b
            p.offline = TuiColor(156, 160, 176);
            p.warning = TuiColor(223, 142, 29);    // #df8e1d
            p.error = TuiColor(210, 15, 57);       // #d20f39
            p.highlight = TuiColor(136, 57, 239);  // #8839ef
            p.selectionBg = TuiColor(204, 208, 218);
            p.selectionFg = TuiColor(20, 20, 30);
            p.progressFill = TuiColor(30, 102, 245);
            p.progressEmpty = TuiColor(210, 214, 220);
            break;

        default:
            break;
        }
        return p;
    }

    static TuiPaletteId parseName(const QString &name) {
        QString s = name.trimmed().toLower();
        if (s == "dracula") return TuiPaletteId::Dracula;
        if (s == "gruvbox") return TuiPaletteId::Gruvbox;
        if (s == "green" || s == "matrix" || s == "retro_green") return TuiPaletteId::RetroGreen;
        if (s == "cyan" || s == "cyber" || s == "retro_cyan") return TuiPaletteId::RetroCyan;
        if (s == "amber" || s == "crt" || s == "retro_amber") return TuiPaletteId::RetroAmber;
        if (s == "dark") return TuiPaletteId::CleanDark;
        if (s == "light") return TuiPaletteId::CleanLight;
        return TuiPaletteId::TokyoNight;
    }
};
