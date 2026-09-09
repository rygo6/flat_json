////////////////////////////////////////////////////////////////////////////////
// @author rygo6
// term.cpp
////////////////////////////////////////////////////////////////////////////////




#include <unistd.h>
#include "Terminal.hpp"
#include <time.h>
#include <stdlib.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <signal.h>
#include <termios.h>
#include <array>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

////////////////////////////////////////////////////////////////////////////////
namespace Flat::Terminal {
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Status line + terminal input
//  StatusBar targets a fixed bottom row; normal stderr scrolls above. Raw
//  stdin (set up here) lets PollKey/PollMouse poll the controlling
//  terminal for input without a window.
////////////////////////////////////////////////////////////////////////////////

static struct {
  bool statusIsTty  = false;
  int  statusRows   = 0;
  bool stdinRaw     = false;
  bool mouseOn      = false;
  struct termios savedStdin = {};
  FILE* logFile     = nullptr;
  char  logDir[512] = {};
} term;

template <size_t N>
static void WriteRaw(const char (&text)[N]) { (void)write(STDERR_FILENO, text, N - 1); }

///////////////////////////////////////////////////////
// Reset
//  Destructor: restore stdin termios, disable mouse reporting, and clear the
//  scroll region so the shell prompt isn't constrained. Runs on normal exit(0) /
//  return-from-main; skipped by _exit().
///////////////////////////////////////////////////////
void Reset() {
  if (term.logFile) { fclose(term.logFile); term.logFile = nullptr; }
  if (term.stdinRaw) { tcsetattr(STDIN_FILENO, TCSANOW, &term.savedStdin); term.stdinRaw = false; }
  if (term.mouseOn)  { fputs("\033[?1003l\033[?1006l", stderr); term.mouseOn = false; }
  if (term.statusIsTty) {
    // \e[r resets DECSTBM to full screen; move to status row and clear it so the
    // shell prompt overwrites it cleanly instead of scrolling it into history.
    fprintf(stderr, "\033[r\033[%d;1H\033[2K", term.statusRows);
  }
  fflush(stderr);
}

///////////////////////////////////////////////////////
// ResetSignalSafe
//  Restores terminal state from signal context without calling stdio.
///////////////////////////////////////////////////////
void ResetSignalSafe() {
  if (term.stdinRaw)
    tcsetattr(STDIN_FILENO, TCSANOW, &term.savedStdin);

  if (term.statusIsTty)
    WriteRaw("\033[?1003l\033[?1006l\033[r");
}

///////////////////////////////////////////////////////
// QueryCursorRow
//  Asks the terminal where the cursor is (DSR) so init can resume output at the prompt.
//  Needs raw stdin for the reply; 0 on no/garbled reply. Discards any typeahead it drains.
///////////////////////////////////////////////////////
static int QueryCursorRow() {
  if (!term.stdinRaw)
    return 0;

  fprintf(stderr, "\033[6n");
  fflush(stderr);

  char reply[64];
  int  length = 0;
  for (int waitedMs = 0; waitedMs < 100;) {
    pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
    if (poll(&pfd, 1, 10) <= 0) { waitedMs += 10; continue; }

    int bytes = (int)read(STDIN_FILENO, reply + length, sizeof(reply) - 1 - (size_t)length);
    if (bytes <= 0) { waitedMs += 10; continue; }
    length += bytes;
    reply[length] = 0;

    const char* pReport = strstr(reply, "\033[");
    while (pReport) {
      int  row = 0, column = 0;
      char terminator = 0;
      if (sscanf(pReport + 2, "%d;%d%c", &row, &column, &terminator) == 3 && terminator == 'R')
        return row;
      pReport = strstr(pReport + 1, "\033[");
    }
    if (length >= (int)sizeof(reply) - 1)
      return 0;
  }
  return 0;
}

///////////////////////////////////////////////////////
// FatalSignal
//  Restores the terminal before preserving the fatal signal and core-dump behavior.
///////////////////////////////////////////////////////
static void FatalSignal(const int signalNumber) {
  ResetSignalSafe();
  signal(signalNumber, SIG_DFL);
  raise(signalNumber);
}

///////////////////////////////////////////////////////
// TerminalStatusInit
//  Constructor: runs before main(). Sets raw stdin (for input polling), detects
//  the stderr TTY, sets DECSTBM, installs signal handlers, registers Reset via atexit.
///////////////////////////////////////////////////////
[[gnu::constructor]]
static void TerminalStatusInit() {
#if defined(FLAT_SHARED_LIB)
  // Dlopen'd into a host app: the tty and the signal handlers are the host's, and dlclose would
  // leave every handler installed here pointing at unmapped code.
  return;
#endif
  // Raw stdin: single-key / mouse polling. Keep ISIG so Ctrl-C still raises SIGINT.
  // VMIN=0/VTIME=0 -> read() returns 0 immediately when no input is pending.
  if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &term.savedStdin) == 0) {
    struct termios raw = term.savedStdin;
    raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) term.stdinRaw = true;
  }

  term.statusIsTty = isatty(fileno(stderr));
  winsize ws = {};
  if (term.statusIsTty && (ioctl(fileno(stderr), TIOCGWINSZ, &ws) != 0 || ws.ws_row < 4))
    term.statusIsTty = false;
  if (term.statusIsTty) {
    term.statusRows = ws.ws_row;
    // Reserve last row; scroll region = 1..(H-1). DECSTBM homes the cursor, so put it back where
    // the prompt left it (clamped into the region) instead of parking at the bottom — parking
    // painted a screen-height gap of blank rows between the prompt and the first log line.
    int cursorRow = QueryCursorRow();
    int parkRow   = (cursorRow > 0 && cursorRow < term.statusRows - 1) ? cursorRow : term.statusRows - 1;
    fprintf(stderr, "\033[1;%dr\033[%d;1H", term.statusRows - 1, parkRow);
    // On the region floor the row still holds the prompt's last line; scroll once off of it.
    if (cursorRow >= term.statusRows)
      fprintf(stderr, "\n");
    fflush(stderr);
  }

  constexpr std::array kFatalSignals = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGQUIT, SIGHUP, SIGINT, SIGTERM, SIGABRT, SIGTRAP };
  for (int signalNumber : kFatalSignals)
    signal(signalNumber, FatalSignal);

  atexit(Reset);
}

///////////////////////////////////////////////////////
// Input drain + parse
//  One read of pending stdin bytes, split into a key queue + a mouse queue. Mouse
//  reports are xterm SGR (1006): ESC '[' '<' b ';' x ';' y ('M'|'m'). Everything
//  else is a key byte. Partial sequences at the buffer tail are kept for next call.
///////////////////////////////////////////////////////
static unsigned char  s_raw[256];
static int            s_rawLen = 0;
static int            s_keyQ[128];
static int            s_keyHead = 0, s_keyTail = 0;
static MouseEvent s_mouseQ[128];
static int            s_mHead = 0, s_mTail = 0;

static constexpr int KEYQ_N   = (int)(sizeof(s_keyQ)   / sizeof(s_keyQ[0]));
static constexpr int MOUSEQ_N = (int)(sizeof(s_mouseQ) / sizeof(s_mouseQ[0]));

static void PushKey(int c)               { int n = (s_keyTail + 1) % KEYQ_N;   if (n != s_keyHead) { s_keyQ[s_keyTail] = c; s_keyTail = n; } }
static void PushMouse(const MouseEvent& e) { int n = (s_mTail + 1) % MOUSEQ_N; if (n != s_mHead)  { s_mouseQ[s_mTail] = e; s_mTail = n; } }

static void Drain() {
  if (!term.stdinRaw) return;

  if (s_rawLen < (int)sizeof(s_raw)) {
    ssize_t n = read(STDIN_FILENO, s_raw + s_rawLen, sizeof(s_raw) - s_rawLen);
    if (n > 0) s_rawLen += (int)n;
  }

  int i = 0;
  while (i < s_rawLen) {
    unsigned char c = s_raw[i];

    if (c == 0x1b) {
      bool haveHdr = (i + 2 < s_rawLen);
      if (!haveHdr && s_rawLen < (int)sizeof(s_raw)) break;   // maybe a sequence; wait for more bytes

      if (haveHdr && s_raw[i + 1] == '[' && s_raw[i + 2] == '<') {
        // SGR mouse: parse b ; x ; y (M|m).
        int j = i + 3, vals[3] = { 0, 0, 0 }, vi = 0; char tc = 0; bool done = false, bad = false;
        while (j < s_rawLen) {
          char d = (char)s_raw[j++];
          if      (d >= '0' && d <= '9') vals[vi] = vals[vi] * 10 + (d - '0');
          else if (d == ';')             { if (vi < 2) vi++; }
          else if (d == 'M' || d == 'm') { tc = d; done = true; break; }
          else                           { bad = true; break; }
        }
        if (!done && !bad) {
          if (s_rawLen < (int)sizeof(s_raw)) break;   // terminator not here yet; keep bytes for next drain
          // buffer full with no terminator (pathological) — give up, emit ESC as a key below.
        }
        if (done) {
          int b = vals[0];
          MouseEvent e;
          e.x = vals[1]; e.y = vals[2];
          e.button = b & 0x3;
          e.motion = (b & 0x20) != 0;
          e.pressed = (tc == 'M');
          PushMouse(e);
          i = j;
          continue;
        }
        // bad: not a real mouse seq — fall through and emit ESC as a key byte.
      }
      // ESC that isn't a mouse header (or buffer full): emit it as a key byte.
    }

    PushKey(c);
    i++;
  }

  if (i > 0) { memmove(s_raw, s_raw + i, s_rawLen - i); s_rawLen -= i; }
}

int PollKey() {
  Drain();
  if (s_keyHead == s_keyTail) return -1;
  int c = s_keyQ[s_keyHead];
  s_keyHead = (s_keyHead + 1) % KEYQ_N;
  return c;
}

bool PollMouse(MouseEvent* out) {
  Drain();
  if (s_mHead == s_mTail) return false;
  if (out) *out = s_mouseQ[s_mHead];
  s_mHead = (s_mHead + 1) % MOUSEQ_N;
  return true;
}

void EnableMouse() {
  if (!term.stdinRaw || term.mouseOn) return;   // need raw stdin to read the reports back
  term.mouseOn = true;
  fputs("\033[?1003h\033[?1006h", stderr);   // 1003 = any-motion tracking, 1006 = SGR coords
  fflush(stderr);
}

void DisableMouse() {
  if (!term.mouseOn) return;
  term.mouseOn = false;
  fputs("\033[?1003l\033[?1006l", stderr);
  fflush(stderr);
}

///////////////////////////////////////////////////////
// StatusBar
//  Writes the formatted message to the fixed status row.
///////////////////////////////////////////////////////
void StatusBar(const char* fmt, ...) {
  flockfile(stderr);  // serialize with concurrent stderr writers

  if (!term.statusIsTty) {
    // Non-TTY: degrade to a regular line; prefix retained as a tag.
    fputs("[STATS] ", stderr);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    funlockfile(stderr);
    return;
  }

  // Re-query rows for SIGWINCH-free resize handling.
  winsize ws = {};
  if (ioctl(fileno(stderr), TIOCGWINSZ, &ws) == 0 && ws.ws_row >= 4 && ws.ws_row != term.statusRows) {
    term.statusRows = ws.ws_row;
    fprintf(stderr, "\033[1;%dr", term.statusRows - 1);  // resync scroll region
  }

  // Save cursor, jump to status row, clear line, write prefix + message, restore cursor.
  fprintf(stderr, "\0337\033[%d;1H\033[2K", term.statusRows);
  fputs(ANSI_BG_RGB(40, 80, 160) ANSI_FG_BRIGHT_WHITE ANSI_BOLD " [STATS] " ANSI_RESET " ", stderr);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputs("\0338", stderr);
  fflush(stderr);

  funlockfile(stderr);
}

///////////////////////////////////////////////////////
// LogOpenFileAt
//  Opens and rotates the log file at a platform-selected directory.
///////////////////////////////////////////////////////
void LogOpenFileAt(const char* dir, const char* appName)
{
  mkdir(dir, 0755);
  snprintf(term.logDir, sizeof(term.logDir), "%s", dir);

  char path[600], prevPath[640];
  snprintf(path, sizeof(path), "%s/%s.log", dir, appName);
  snprintf(prevPath, sizeof(prevPath), "%s/%s-prev.log", dir, appName);
  rename(path, prevPath);

  term.logFile = fopen(path, "w");
  if (term.logFile) fprintf(stderr, "log file: %s\n", path);
  else              fprintf(stderr, "log file open failed: %s\n", path);
}

const char* LogDir() { return term.logDir; }

///////////////////////////////////////////////////////
// StripAnsi
//  Removes terminal color sequences before mirroring a record to disk.
///////////////////////////////////////////////////////
static int StripAnsi(const char* source, int length, int capacity, char* pOutput) {
  int read = 0;
  int written = 0;
  while (read < length && written < capacity) {
    if ((unsigned char)source[read] == 0x1b && read + 1 < length && source[read + 1] == '[') {
      read += 2;
      while (read < length) {
        unsigned char c = (unsigned char)source[read++];
        if (c >= 0x40 && c <= 0x7e)
          break;
      }

      continue;
    }

    pOutput[written++] = source[read++];
  }

  return written;
}

///////////////////////////////////////////////////////
// Log
//  Writes a colored log record to stderr and a plain-text copy to the optional file mirror.
///////////////////////////////////////////////////////
void Log(const char* file, int filePad, int line, const char* tag, const char* fmt, ...) {
  char buf[2048];   // sized so multi-line framed warnings fit in a single call
  int n = snprintf(buf, sizeof(buf),
    ANSI_FG_RGB(0,0,64) "%s" ANSI_FG_RGB(0,0,0) ":" ANSI_FG_RGB(0,64,0) "%-*d" "%s",
    file, filePad, line, tag);
  va_list args;
  va_start(args, fmt);
  n += vsnprintf(buf + n, sizeof(buf) - n, fmt, args);
  va_end(args);
  n = n < (int)sizeof(buf) ? n : (int)sizeof(buf);
  fwrite(buf, 1, n, stderr);

  if (term.logFile) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);

    char plain[2048];
    int plainLength = StripAnsi(buf, n, sizeof(plain), plain);
    fprintf(term.logFile, "%02d:%02d:%02d.%03d %.*s", tm.tm_hour, tm.tm_min, tm.tm_sec, (int)(ts.tv_nsec / 1000000), plainLength, plain);
    fflush(term.logFile);
  }
}

////////////////////////////////////////////////////////////////////////////////
}  // namespace Flat::Terminal
////////////////////////////////////////////////////////////////////////////////
