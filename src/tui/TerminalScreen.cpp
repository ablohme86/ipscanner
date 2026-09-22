#include "TerminalScreen.h"
#include <sys/ioctl.h>
#include <unistd.h>
#include <csignal>
#include <cstring>
#include <clocale>

TerminalScreen *TerminalScreen::s_instance = nullptr;

static void signalHandler(int sig)
{
    TerminalScreen::emergencyRestore();
    _exit(128 + sig);
}

TerminalScreen::TerminalScreen()
{
    s_instance = this;
    std::setlocale(LC_ALL, "");
}

TerminalScreen::~TerminalScreen()
{
    restore();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

void TerminalScreen::emergencyRestore()
{
    if (s_instance) {
        s_instance->restore();
    }
}

bool TerminalScreen::init()
{
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        return false;
    }

    if (tcgetattr(STDIN_FILENO, &m_origTermios) == -1) {
        return false;
    }

    struct termios raw = m_origTermios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        return false;
    }

    m_rawActive = true;

    // Register signal handlers for clean terminal restore on abort/interrupt
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGQUIT, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);

    // Enter alternate screen buffer & hide cursor
    const char *enterAlt = "\033[?1049h\033[?25l\033[2J\033[H";
    ::write(STDOUT_FILENO, enterAlt, strlen(enterAlt));

    updateDimensions();
    return true;
}

void TerminalScreen::restore()
{
    if (m_rawActive) {
        // Show cursor & return to main screen buffer
        const char *exitAlt = "\033[0m\033[?25h\033[?1049l";
        ::write(STDOUT_FILENO, exitAlt, strlen(exitAlt));
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &m_origTermios);
        m_rawActive = false;
    }
}

void TerminalScreen::updateDimensions()
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1 && ws.ws_col > 0 && ws.ws_row > 0) {
        m_cols = ws.ws_col;
        m_rows = ws.ws_row;
    } else {
        m_cols = 80;
        m_rows = 24;
    }

    if (m_cols < 20) m_cols = 20;
    if (m_rows < 10) m_rows = 10;

    m_buffer.assign(m_cols * m_rows, TuiCell{});
}

bool TerminalScreen::checkResize()
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1 && ws.ws_col > 0 && ws.ws_row > 0) {
        int newCols = ws.ws_col;
        int newRows = ws.ws_row;
        if (newCols < 20) newCols = 20;
        if (newRows < 10) newRows = 10;

        if (newCols != m_cols || newRows != m_rows) {
            m_cols = newCols;
            m_rows = newRows;
            m_buffer.assign(m_cols * m_rows, TuiCell{});
            return true;
        }
    }
    return false;
}

void TerminalScreen::clear(const TuiColor &bg)
{
    TuiCell blank;
    blank.ch = ' ';
    blank.fg = TuiColor(200, 200, 200);
    blank.bg = bg;
    blank.bold = false;
    blank.underline = false;
    std::fill(m_buffer.begin(), m_buffer.end(), blank);
}

void TerminalScreen::setCell(int col, int row, uint32_t ch, const TuiColor &fg, const TuiColor &bg, bool bold, bool underline)
{
    if (col < 0 || col >= m_cols || row < 0 || row >= m_rows) return;
    int idx = row * m_cols + col;
    m_buffer[idx].ch = ch;
    m_buffer[idx].fg = fg;
    m_buffer[idx].bg = bg;
    m_buffer[idx].bold = bold;
    m_buffer[idx].underline = underline;
}

void TerminalScreen::drawString(int col, int row, const QString &text, const TuiColor &fg, const TuiColor &bg, bool bold, int maxLen)
{
    if (row < 0 || row >= m_rows || col >= m_cols) return;

    int curCol = col;
    int written = 0;

    for (int i = 0; i < text.length(); ++i) {
        if (curCol < 0) {
            curCol++;
            continue;
        }
        if (curCol >= m_cols) break;
        if (maxLen >= 0 && written >= maxLen) break;

        QChar qc = text.at(i);
        uint32_t cp = qc.unicode();
        if (qc.isHighSurrogate() && i + 1 < text.length()) {
            QChar next = text.at(i + 1);
            if (next.isLowSurrogate()) {
                cp = QChar::surrogateToUcs4(qc, next);
                i++;
            }
        }

        setCell(curCol, row, cp, fg, bg, bold);
        curCol++;
        written++;
    }
}

void TerminalScreen::drawBox(int x, int y, int w, int h, const QString &title, const TuiColor &borderColor, const TuiColor &titleColor, const TuiColor &bg, bool rounded)
{
    if (w <= 1 || h <= 1) return;

    uint32_t tl = rounded ? 0x256D : 0x250C; // ╭ or ┌
    uint32_t tr = rounded ? 0x256E : 0x2510; // ╮ or ┐
    uint32_t bl = rounded ? 0x2570 : 0x2514; // ╰ or └
    uint32_t br = rounded ? 0x256F : 0x2518; // ╯ or ┘
    uint32_t hz = 0x2500; // ─
    uint32_t vt = 0x2502; // │

    // Corners
    setCell(x, y, tl, borderColor, bg);
    setCell(x + w - 1, y, tr, borderColor, bg);
    setCell(x, y + h - 1, bl, borderColor, bg);
    setCell(x + w - 1, y + h - 1, br, borderColor, bg);

    // Top & bottom edges
    for (int col = x + 1; col < x + w - 1; ++col) {
        setCell(col, y, hz, borderColor, bg);
        setCell(col, y + h - 1, hz, borderColor, bg);
    }

    // Left & right edges
    for (int row = y + 1; row < y + h - 1; ++row) {
        setCell(x, row, vt, borderColor, bg);
        setCell(x + w - 1, row, vt, borderColor, bg);
    }

    // Inner area fill
    for (int row = y + 1; row < y + h - 1; ++row) {
        for (int col = x + 1; col < x + w - 1; ++col) {
            setCell(col, row, ' ', borderColor, bg);
        }
    }

    // Title
    if (!title.isEmpty() && w > 6) {
        QString displayTitle = " " + title + " ";
        int maxTitleLen = w - 4;
        if (displayTitle.length() > maxTitleLen) {
            displayTitle = displayTitle.left(maxTitleLen);
        }
        drawString(x + 2, y, displayTitle, titleColor, bg, true);
    }
}

void TerminalScreen::drawHLine(int x, int y, int len, const TuiColor &color, const TuiColor &bg, uint32_t ch)
{
    if (ch == 0) ch = 0x2500; // ─
    for (int i = 0; i < len; ++i) {
        setCell(x + i, y, ch, color, bg);
    }
}

void TerminalScreen::drawVLine(int x, int y, int len, const TuiColor &color, const TuiColor &bg, uint32_t ch)
{
    if (ch == 0) ch = 0x2502; // │
    for (int i = 0; i < len; ++i) {
        setCell(x, y + i, ch, color, bg);
    }
}

void TerminalScreen::drawProgressBar(int x, int y, int width, double fraction, const TuiColor &fillColor, const TuiColor &emptyColor, const TuiColor &bg)
{
    if (width <= 0) return;
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;

    int filledChars = static_cast<int>(fraction * width);
    for (int i = 0; i < width; ++i) {
        if (i < filledChars) {
            setCell(x + i, y, 0x2588, fillColor, bg); // █
        } else {
            setCell(x + i, y, 0x2591, emptyColor, bg); // ░
        }
    }
}

void TerminalScreen::fill(int x, int y, int w, int h, uint32_t ch, const TuiColor &fg, const TuiColor &bg)
{
    for (int r = y; r < y + h; ++r) {
        for (int c = x; c < x + w; ++c) {
            setCell(c, r, ch, fg, bg);
        }
    }
}

void TerminalScreen::showCursor(int col, int row)
{
    m_cursorVisible = true;
    m_cursorCol = col;
    m_cursorRow = row;
}

void TerminalScreen::hideCursor()
{
    m_cursorVisible = false;
}

void TerminalScreen::appendUtf8(std::string &out, uint32_t cp)
{
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

void TerminalScreen::flush()
{
    std::string out;
    out.reserve(m_cols * m_rows * 4 + 1024);

    // Move cursor to home & hide cursor
    out.append("\033[H");
    if (!m_cursorVisible) {
        out.append("\033[?25l");
    }

    TuiColor curFg(255, 255, 255);
    TuiColor curBg(0, 0, 0);
    bool curBold = false;
    bool curUnderline = false;
    bool first = true;

    char numBuf[64];

    for (int r = 0; r < m_rows; ++r) {
        for (int c = 0; c < m_cols; ++c) {
            const TuiCell &cell = m_buffer[r * m_cols + c];

            if (first || cell.fg != curFg || cell.bg != curBg || cell.bold != curBold || cell.underline != curUnderline) {
                curFg = cell.fg;
                curBg = cell.bg;
                curBold = cell.bold;
                curUnderline = cell.underline;
                first = false;

                out.append("\033[0");
                if (curBold) out.append(";1");
                if (curUnderline) out.append(";4");

                snprintf(numBuf, sizeof(numBuf), ";38;2;%u;%u;%u;48;2;%u;%u;%um",
                         curFg.r, curFg.g, curFg.b,
                         curBg.r, curBg.g, curBg.b);
                out.append(numBuf);
            }

            appendUtf8(out, cell.ch);
        }
        if (r + 1 < m_rows) {
            out.append("\r\n");
        }
    }

    if (m_cursorVisible) {
        snprintf(numBuf, sizeof(numBuf), "\033[%d;%dH\033[?25h", m_cursorRow + 1, m_cursorCol + 1);
        out.append(numBuf);
    } else {
        out.append("\033[?25l");
    }

    ::write(STDOUT_FILENO, out.data(), out.size());
}

std::vector<TuiKeyEvent> TerminalScreen::readKeys()
{
    std::vector<TuiKeyEvent> events;
    uint8_t buf[256];
    ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
    if (n <= 0) return events;

    ssize_t i = 0;
    while (i < n) {
        uint8_t b = buf[i];

        if (b == 0x1B) { // Escape
            if (i + 1 >= n) {
                // Standalone Escape
                TuiKeyEvent ev;
                ev.key = TuiKey::Escape;
                events.push_back(ev);
                i++;
                continue;
            }

            // Check next bytes
            uint8_t b2 = buf[i + 1];
            if (b2 == '[' || b2 == 'O') {
                if (i + 2 >= n) {
                    // Incomplete sequence, treat as escape
                    TuiKeyEvent ev;
                    ev.key = TuiKey::Escape;
                    events.push_back(ev);
                    i++;
                    continue;
                }

                uint8_t b3 = buf[i + 2];
                if (b2 == '[') {
                    if (b3 == 'A') { events.push_back({TuiKey::Up}); i += 3; continue; }
                    if (b3 == 'B') { events.push_back({TuiKey::Down}); i += 3; continue; }
                    if (b3 == 'C') { events.push_back({TuiKey::Right}); i += 3; continue; }
                    if (b3 == 'D') { events.push_back({TuiKey::Left}); i += 3; continue; }
                    if (b3 == 'H') { events.push_back({TuiKey::Home}); i += 3; continue; }
                    if (b3 == 'F') { events.push_back({TuiKey::End}); i += 3; continue; }

                    // Tilde sequences: \033[1~, \033[3~, \033[5~, etc.
                    if (i + 3 < n && buf[i + 3] == '~') {
                        if (b3 == '1' || b3 == '7') { events.push_back({TuiKey::Home}); i += 4; continue; }
                        if (b3 == '4' || b3 == '8') { events.push_back({TuiKey::End}); i += 4; continue; }
                        if (b3 == '3') { events.push_back({TuiKey::Delete}); i += 4; continue; }
                        if (b3 == '5') { events.push_back({TuiKey::PageUp}); i += 4; continue; }
                        if (b3 == '6') { events.push_back({TuiKey::PageDown}); i += 4; continue; }
                    }

                    // Multi-digit tilde sequences, e.g. F5 is \033[15~
                    if (i + 4 < n && buf[i + 4] == '~') {
                        if (b3 == '1' && buf[i + 3] == '5') { events.push_back({TuiKey::F5}); i += 5; continue; }
                        i += 5;
                        continue;
                    }

                    // Unknown bracket sequence, skip 3
                    i += 3;
                    continue;
                } else if (b2 == 'O') {
                    if (b3 == 'A') { events.push_back({TuiKey::Up}); i += 3; continue; }
                    if (b3 == 'B') { events.push_back({TuiKey::Down}); i += 3; continue; }
                    if (b3 == 'C') { events.push_back({TuiKey::Right}); i += 3; continue; }
                    if (b3 == 'D') { events.push_back({TuiKey::Left}); i += 3; continue; }
                    if (b3 == 'H') { events.push_back({TuiKey::Home}); i += 3; continue; }
                    if (b3 == 'F') { events.push_back({TuiKey::End}); i += 3; continue; }
                    if (b3 == 'P') { events.push_back({TuiKey::F1}); i += 3; continue; }
                    if (b3 == 'Q') { events.push_back({TuiKey::F2}); i += 3; continue; }
                    if (b3 == 'R') { events.push_back({TuiKey::F3}); i += 3; continue; }
                    if (b3 == 'S') { events.push_back({TuiKey::F4}); i += 3; continue; }
                    i += 3;
                    continue;
                }
            }

            // Alt + key
            TuiKeyEvent ev;
            ev.key = TuiKey::Char;
            ev.ch = b2;
            ev.alt = true;
            events.push_back(ev);
            i += 2;
            continue;
        }

        // Control characters
        if (b == 0x03) { events.push_back({TuiKey::CtrlC, 0, true}); i++; continue; }
        if (b == 0x04) { events.push_back({TuiKey::CtrlD, 0, true}); i++; continue; }
        if (b == 0x15) { events.push_back({TuiKey::CtrlU, 0, true}); i++; continue; }
        if (b == 0x0C) { events.push_back({TuiKey::CtrlL, 0, true}); i++; continue; }
        if (b == 0x12) { events.push_back({TuiKey::CtrlR, 0, true}); i++; continue; }

        if (b == '\r' || b == '\n') { events.push_back({TuiKey::Enter}); i++; continue; }
        if (b == 0x7F || b == 0x08) { events.push_back({TuiKey::Backspace}); i++; continue; }
        if (b == '\t') { events.push_back({TuiKey::Tab}); i++; continue; }

        // Normal UTF-8 decoding
        uint32_t cp = 0;
        int len = 1;
        if ((b & 0x80) == 0) {
            cp = b;
            len = 1;
        } else if ((b & 0xE0) == 0xC0 && i + 1 < n) {
            cp = ((b & 0x1F) << 6) | (buf[i + 1] & 0x3F);
            len = 2;
        } else if ((b & 0xF0) == 0xE0 && i + 2 < n) {
            cp = ((b & 0x0F) << 12) | ((buf[i + 1] & 0x3F) << 6) | (buf[i + 2] & 0x3F);
            len = 3;
        } else if ((b & 0xF8) == 0xF0 && i + 3 < n) {
            cp = ((b & 0x07) << 18) | ((buf[i + 1] & 0x3F) << 12) | ((buf[i + 2] & 0x3F) << 6) | (buf[i + 3] & 0x3F);
            len = 4;
        } else {
            cp = b;
            len = 1;
        }

        TuiKeyEvent ev;
        ev.key = TuiKey::Char;
        ev.ch = cp;
        events.push_back(ev);
        i += len;
    }

    return events;
}
