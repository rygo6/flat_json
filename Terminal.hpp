////////////////////////////////////////////////////////////////////////////////
// @author rygo6
// term.hpp
////////////////////////////////////////////////////////////////////////////////

#pragma once

////////////////////////////////////////////////////////////////////////////////
// ANSI escape codes
////////////////////////////////////////////////////////////////////////////////

#define ANSI_RESET            "\033[0m"

// Attributes
#define ANSI_BOLD             "\033[1m"
#define ANSI_DIM              "\033[2m"
#define ANSI_ITALIC           "\033[3m"
#define ANSI_UNDERLINE        "\033[4m"
#define ANSI_BLINK            "\033[5m"
#define ANSI_REVERSE          "\033[7m"
#define ANSI_HIDDEN           "\033[8m"
#define ANSI_STRIKETHROUGH    "\033[9m"

// Foreground — standard 8
#define ANSI_FG_BLACK         "\033[30m"
#define ANSI_FG_RED           "\033[31m"
#define ANSI_FG_GREEN         "\033[32m"
#define ANSI_FG_YELLOW        "\033[33m"
#define ANSI_FG_BLUE          "\033[34m"
#define ANSI_FG_MAGENTA       "\033[35m"
#define ANSI_FG_CYAN          "\033[36m"
#define ANSI_FG_WHITE         "\033[37m"
#define ANSI_FG_DEFAULT       "\033[39m"

// Foreground — bright 8
#define ANSI_FG_BRIGHT_BLACK   "\033[90m"
#define ANSI_FG_BRIGHT_RED     "\033[91m"
#define ANSI_FG_BRIGHT_GREEN   "\033[92m"
#define ANSI_FG_BRIGHT_YELLOW  "\033[93m"
#define ANSI_FG_BRIGHT_BLUE    "\033[94m"
#define ANSI_FG_BRIGHT_MAGENTA "\033[95m"
#define ANSI_FG_BRIGHT_CYAN    "\033[96m"
#define ANSI_FG_BRIGHT_WHITE   "\033[97m"

// Background — standard 8
#define ANSI_BG_BLACK         "\033[40m"
#define ANSI_BG_RED           "\033[41m"
#define ANSI_BG_GREEN         "\033[42m"
#define ANSI_BG_YELLOW        "\033[43m"
#define ANSI_BG_BLUE          "\033[44m"
#define ANSI_BG_MAGENTA       "\033[45m"
#define ANSI_BG_CYAN          "\033[46m"
#define ANSI_BG_WHITE         "\033[47m"
#define ANSI_BG_DEFAULT       "\033[49m"

// Background — bright 8
#define ANSI_BG_BRIGHT_BLACK   "\033[100m"
#define ANSI_BG_BRIGHT_RED     "\033[101m"
#define ANSI_BG_BRIGHT_GREEN   "\033[102m"
#define ANSI_BG_BRIGHT_YELLOW  "\033[103m"
#define ANSI_BG_BRIGHT_BLUE    "\033[104m"
#define ANSI_BG_BRIGHT_MAGENTA "\033[105m"
#define ANSI_BG_BRIGHT_CYAN    "\033[106m"
#define ANSI_BG_BRIGHT_WHITE   "\033[107m"

// 256-color (n = 0..255) and 24-bit truecolor
#define ANSI_FG_256(n)        "\033[38;5;" #n "m"
#define ANSI_BG_256(n)        "\033[48;5;" #n "m"
#define ANSI_FG_RGB(r,g,b)    "\033[38;2;" #r ";" #g ";" #b "m"
#define ANSI_BG_RGB(r,g,b)    "\033[48;2;" #r ";" #g ";" #b "m"

// Cursor + screen control
#define ANSI_SAVE_CURSOR      "\0337"        // DECSC: save cursor + attributes
#define ANSI_RESTORE_CURSOR   "\0338"        // DECRC: restore cursor + attributes
#define ANSI_CLEAR_LINE       "\033[2K"      // clear entire current line
#define ANSI_CLEAR_SCREEN     "\033[2J"      // clear entire screen
#define ANSI_SCROLL_RESET     "\033[r"       // reset DECSTBM to full screen

// Parameterized format-string fragments
#define ANSI_CUP_ROW_FMT      "\033[%d;1H"   // move cursor to (row, col=1)
#define ANSI_SCROLL_TOP_FMT   "\033[1;%dr"   // DECSTBM: scroll region = lines 1..N

////////////////////////////////////////////////////////////////////////////////
namespace Flat::Terminal {
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////
// StatusBar
//  Writes to a fixed bottom-row status area while normal stderr output scrolls above it.
///////////////////////////////////////////////////////
[[gnu::format(printf, 1, 2)]]
void StatusBar(const char* fmt, ...);

///////////////////////////////////////////////////////
// Log
//  Writes a log line to stderr (file:line + tag prefix + body) in one fwrite.
///////////////////////////////////////////////////////
[[gnu::format(printf, 5, 6)]]
void Log(const char* file, int filePad, int line, const char* tag, const char* fmt, ...);

///////////////////////////////////////////////////////
// LogOpenFile
//  Mirrors timestamped log lines with rotation in the platform log directory.
///////////////////////////////////////////////////////
void LogOpenFile(const char* appName);

///////////////////////////////////////////////////////
// LogDir
//  Directory the log file was opened in; empty string before LogOpenFile.
///////////////////////////////////////////////////////
const char* LogDir();

///////////////////////////////////////////////////////
// Terminal input capture
//  Raw stdin (no canonical line buffering, no echo) is enabled automatically at
//  startup when stdin is a TTY, and restored on exit / SIGINT / SIGTERM. Ctrl-C
//  still works (ISIG kept). Lets a headless client poll the controlling terminal
//  for keys (and, opt-in, mouse) — e.g. to drive input without a window.
///////////////////////////////////////////////////////

// Non-blocking single-byte key read. Returns the byte (0..255), or -1 if none is
// pending. Mouse escape sequences are consumed internally (not returned as keys)
// and surfaced via PollMouse instead.
int PollKey();

// One mouse report parsed from the terminal (xterm SGR 1006 + any-motion 1003).
struct MouseEvent {
  int  x       = 0;      // 1-based column (terminal cell)
  int  y       = 0;      // 1-based row
  int  button  = 0;      // 0=left, 1=middle, 2=right, 3=none (SGR code low 2 bits)
  bool pressed = false;  // 'M' = press / motion-with-button held; 'm' = release
  bool motion  = false;  // event carried the motion flag (SGR code & 32)
};

// Opt-in mouse reporting (OFF by default — any-motion tracking streams a lot of
// events to stdin, so a client asks for it explicitly). EnableMouse turns on
// xterm 1003 (any motion) + 1006 (SGR coords); PollMouse pops the next event
// (false if none). Auto-disabled on exit / SIGINT / SIGTERM. Needs raw stdin (a TTY).
void Reset();
void ResetSignalSafe();
void EnableMouse();
void DisableMouse();
bool PollMouse(MouseEvent* out);

////////////////////////////////////////////////////////////////////////////////
}  // namespace Flat::Terminal
////////////////////////////////////////////////////////////////////////////////

#define TERM_LOG(tag, format, ...) Flat::Terminal::Log(__FILE__, (int)(20 - sizeof(__FILE__)), __LINE__, tag, format, ##__VA_ARGS__)

#define INFO(name, color, format, ...) TERM_LOG(ANSI_FG_RGB color                         "[" #name "]" ANSI_RESET " ",       format, ##__VA_ARGS__)
#define WARN(name, format, ...)        TERM_LOG(ANSI_FG_RGB(0,0,0) ANSI_BG_RGB(255,255,0) "[" #name "] WARN:" ANSI_RESET " ", format, ##__VA_ARGS__)
#define ERR(name, format, ...)         TERM_LOG(ANSI_FG_RGB(0,0,0) ANSI_BG_RGB(255,0,0)   "[" #name "] ERR:" ANSI_RESET " ",  format, ##__VA_ARGS__)

#ifdef ENABLE_VERBOSE_INFO
#define VERBOSE(name, color, format, ...) INFO(name, color, format, ##__VA_ARGS__)
#else
#define VERBOSE(name, color, format, ...) ((void)0)
#endif
