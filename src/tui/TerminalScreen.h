#pragma once

#include <QString>
#include <vector>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <cstdint>
#include "TuiTheme.h"

enum class TuiKey {
    None,
    Char,
    Up, Down, Left, Right,
    PageUp, PageDown, Home, End,
    Enter, Backspace, Delete, Tab, Escape,
    CtrlC, CtrlD, CtrlU, CtrlL, CtrlR,
    F1, F2, F3, F4, F5
};

struct TuiKeyEvent {
    TuiKey key = TuiKey::None;
    uint32_t ch = 0;
    bool ctrl = false;
    bool alt = false;
};

struct TuiCell {
    uint32_t ch = ' ';
    TuiColor fg;
    TuiColor bg;
    bool bold = false;
    bool underline = false;
};

class TerminalScreen {
public:
    TerminalScreen();
    ~TerminalScreen();

    bool init();
    void restore();

    int cols() const { return m_cols; }
    int rows() const { return m_rows; }

    bool checkResize();

    void clear(const TuiColor &bg);
    void setCell(int col, int row, uint32_t ch, const TuiColor &fg, const TuiColor &bg, bool bold = false, bool underline = false);
    void drawString(int col, int row, const QString &text, const TuiColor &fg, const TuiColor &bg, bool bold = false, int maxLen = -1);
    void drawBox(int x, int y, int w, int h, const QString &title, const TuiColor &borderColor, const TuiColor &titleColor, const TuiColor &bg, bool rounded = true);
    void drawHLine(int x, int y, int len, const TuiColor &color, const TuiColor &bg, uint32_t ch = 0);
    void drawVLine(int x, int y, int len, const TuiColor &color, const TuiColor &bg, uint32_t ch = 0);
    void drawProgressBar(int x, int y, int width, double fraction, const TuiColor &fillColor, const TuiColor &emptyColor, const TuiColor &bg);
    void fill(int x, int y, int w, int h, uint32_t ch, const TuiColor &fg, const TuiColor &bg);

    void showCursor(int col, int row);
    void hideCursor();

    void flush();

    std::vector<TuiKeyEvent> readKeys();

    static void emergencyRestore();

private:
    void updateDimensions();
    void appendUtf8(std::string &out, uint32_t cp);

private:
    int m_cols = 80;
    int m_rows = 24;
    std::vector<TuiCell> m_buffer;
    bool m_rawActive = false;
    struct termios m_origTermios;
    bool m_cursorVisible = false;
    int m_cursorCol = 0;
    int m_cursorRow = 0;

    static TerminalScreen *s_instance;
};
