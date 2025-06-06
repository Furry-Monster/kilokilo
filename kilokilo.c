/*  includes  */

// if compile error,includes these:
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*  defines */

#define KILOKILO_VERSION "0.0.1"
#define KILOKILO_TAB_STOP 8
#define KILOKILO_QUIT_TIMES 3

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN,
};

/*  data  */

/**
 * @brief
 *
 */
typedef struct erow {
  int size;
  int rsize;
  char *chars;
  char *render;
} erow;

/**
 * @brief
 *
 */
struct editorConfig {
  int cx, cy;
  int rx;
  int rowoff;
  int coloff;
  int screenrow;
  int screencol;
  int numrows;
  erow *row;
  int dirty;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  struct termios orig_termios;
};

struct editorConfig E;

/*  prototypes  */
void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt);

/*  terminal  */

/**
 * @brief
 *
 * @param s
 */
void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4); // [2J will clear the terminal window

  write(STDOUT_FILENO, "\x1b[H", 3);
  // [H is another way of saying [1;1H ,
  // which will set cursor to the top left

  perror(s);
  exit(1);
}

/**
 * @brief
 *
 */
void disableRowMode() {
  // this function will reset the terminal
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

/**
 * @brief
 *
 */
void enableRowMode() {
  // function to set terminal for editor
  // we'll change the way we input
  // and the way terminal show characters here...
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");
  atexit(disableRowMode);
  struct termios raw = E.orig_termios;
  // block some signal below, for example, CTRL+C is blocked IXON
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON); // set input mode
  raw.c_oflag &= ~(OPOST);                                  // set output mode
  raw.c_cflag &= ~(CS8);                                    // set control mode
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);          // set misc mode
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

/**
 * @brief
 *
 * @return int
 */
int editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }

  if (c == '\x1b') {
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
          case '1':
            return HOME_KEY;
          case '3':
            return DEL_KEY;
          case '4':
            return END_KEY;
          case '5':
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          case '7':
            return HOME_KEY;
          case '8':
            return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
      case 'H':
        return HOME_KEY;
      case 'F':
        return END_KEY;
      }
    }

    return '\x1b';
  } else {
    return c;
  }
}

/**
 * @brief Get the Cursor Position object
 *
 * @param row
 * @param col
 * @return int
 */
int getCursorPosition(int *row, int *col) {
  char buf[32];
  unsigned int i = 0;

  // using [6n to get the location of cursor , in format of "<esc>[24;80R"
  // (24row,80col)
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;

  while (1 < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
    i++;
  }
  buf[i] = '\0'; // remember the EOF

  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;
  if (sscanf(&buf[2], "%d;%d", row, col) != 2) // read row and col from buffer
    return -1;

  return 0;
}

/**
 * @brief Get the Window Size object
 *
 * @param row
 * @param col
 * @return int
 */
int getWindowSize(int *row, int *col) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    // If we can't read info with syscall,then check size like this:
    // 1. set cursor to right bottom(use <esc>[999C<esc>[999B to do this)
    // 2. get coord of cursor, where the cursor locate should be the window size
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;
    return getCursorPosition(row, col);
  } else {
    *col = ws.ws_col;
    *row = ws.ws_row;
    return 0;
  }
}

/*  row operation  */

/**
 * @brief
 *
 * @param row
 * @param cx
 * @return int
 */
int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (KILOKILO_TAB_STOP - 1) - (rx % KILOKILO_TAB_STOP);
    rx++;
  }
  return rx;
}

int editorRowRxToCx(erow *row, int rx) {
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t')
      cur_rx += (KILOKILO_TAB_STOP - 1) - (cur_rx % KILOKILO_TAB_STOP);
    cur_rx++;

    if (cur_rx > rx)
      return cx;
  }
  return cx;
}

/**
 * @brief
 *
 * @param row
 */
void editorUpdateRow(erow *row) {
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t')
      tabs++;

  free(row->render);
  row->render = malloc(row->size + (KILOKILO_TAB_STOP - 1) * tabs + 1);

  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % KILOKILO_TAB_STOP != 0)
        row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
}

/**
 * @brief
 *
 * @param s
 * @param len
 */
void editorInsertRow(int at, char *s, size_t len) {
  if (at < 0 || at > E.numrows)
    return;

  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  editorUpdateRow(&E.row[at]);

  E.numrows++;
  E.dirty++;
}

/**
 * @brief
 *
 * @param row
 */
void editorFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
}

/**
 * @brief
 *
 * @param at
 */
void editorDelRow(int at) {
  if (at < 0 || at > E.numrows)
    return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
  E.numrows--;
  E.dirty++;
}

/**
 * @brief
 *
 * @param row
 * @param at
 * @param c
 */
void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size)
    at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
  E.dirty++;
}

/**
 * @brief
 *
 * @param row
 * @param s
 * @param len
 */
void editorRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}

/**
 * @brief
 *
 * @param row
 * @param at
 */
void editorRowDelChar(erow *row, int at) {
  if (at < 0 || at > row->size)
    return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  E.dirty++;
}

/*  editor operation  */

/**
 * @brief
 *
 * @param c
 */
void editorInsertChar(char c) {
  if (E.cy == E.numrows)
    editorInsertRow(E.numrows, "", 0);

  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
}

/**
 * @brief
 *
 */
void editorInserNewLine() {
  if (E.cx == 0) {
    editorInsertRow(E.cy, "", 0);
  } else {
    erow *row = &E.row[E.cy];
    editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  E.cy++;
  E.cx = 0;
}

/**
 * @brief
 *
 */
void editorDelChar() {
  if (E.cy == E.numrows)
    return;
  if (E.cx == 0 && E.cy == 0)
    return;

  erow *row = &E.row[E.cy];
  if (E.cx > 0) {
    editorRowDelChar(row, E.cx - 1);
    E.cx--;
  } else {
    E.cx = E.row[E.cy - 1].size;
    editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
    editorDelRow(E.cy);
    E.cy--;
  }
}

/*  file i/o  */

/**
 * @brief
 *
 * @param buflen
 * @return char*
 */
char *editorRowsToString(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < E.numrows; j++)
    totlen += E.row[j].size + 1;
  *buflen = totlen;
  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }
  return buf;
}

/**
 * @brief
 *
 * @param filename
 */
void editorOpen(char *filename) {
  free(E.filename);
  E.filename = strdup(filename);

  FILE *fp = fopen(filename, "r");
  if (!fp)
    die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;
    editorInsertRow(E.numrows, line, linelen);
  }
  free(line);
  fclose(fp);
  E.dirty = 0;
}

/**
 * @brief
 *
 */
void editorSave() {
  if (E.filename == NULL) {
    E.filename = editorPrompt("Save as: %s");
    if (E.filename == NULL) {
      editorSetStatusMessage("Save aborted");
      return;
    }
  }

  int len;
  char *buf = editorRowsToString(&len);

  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        E.dirty = 0;
        editorSetStatusMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }

  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/* find */

/**
 * @brief
 *
 */
void editorFind() {
  char *query = editorPrompt("Search: %s (ESC to cancel)");
  if (query == NULL)
    return;
  int i;
  for (i = 0; i < E.numrows; i++) {
    erow *row = &E.row[i];
    char *match = strstr(row->render, query);
    if (match) {
      E.cy = i;
      E.cx = editorRowRxToCx(row, match - row->render);
      E.rowoff = E.numrows;
      break;
    }
  }
  free(query);
}

/*  append buffer  */

/**
 * @brief
 *
 */
struct abuf {
  char *b;
  int len;
};

// default buffer initalizer
#define ABUF_INIT                                                              \
  { NULL, 0 }

/**
 * @brief
 *
 * @param ab
 * @param s
 * @param len
 */
void abAppend(struct abuf *ab, const char *s, int len) {
  char *newBuf = realloc(ab->b, ab->len + len);

  if (newBuf == NULL)
    return;
  memcpy(&newBuf[ab->len], s, len);
  ab->b = newBuf;
  ab->len += len;
}

/**
 * @brief
 *
 * @param ab
 */
void abFree(struct abuf *ab) { free(ab->b); }

/*  output  */

/**
 * @brief
 *
 */
void editorScroll() {
  E.rx = E.cx;
  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }

  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrow) {
    E.rowoff = E.cy - E.screenrow + 1;
  }
  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.screencol) {
    E.coloff = E.rx - E.screencol + 1;
  }
}

/**
 * @brief
 *
 * @param ab
 */
void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrow; y++) {
    int filerow = y + E.rowoff;
    if (filerow >= E.numrows) {
      if (E.numrows == 0 && y == E.screenrow / 3) {
        // print welcome message here
        char welcome[80];
        int welcomelen =
            snprintf(welcome, sizeof(welcome), "KiloKilo editor -- version %s",
                     KILOKILO_VERSION);
        if (welcomelen > E.screencol)
          welcomelen = E.screencol; // Truncate if too long
        int padding = (E.screencol - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--)
          abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      } else {
        // print ~ character
        abAppend(ab, "~", 1);
      }
    } else {
      // print text from file.
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0)
        len = 0;
      if (len > E.screencol)
        len = E.screencol;
      abAppend(ab, &E.row[filerow].render[E.coloff], len);
    }

    abAppend(
        ab, "\x1b[K",
        3); // Clear when draw new line,instead of repainting the whole window
    abAppend(ab, "\r\n", 2);
  }
}

/**
 * @brief
 *
 * @param ab
 */
void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                     E.filename ? E.filename : "[Untitled File]", E.numrows,
                     E.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);
  if (len > E.screencol)
    len = E.screencol;
  abAppend(ab, status, len);
  while (len < E.screencol) {
    if (E.screencol - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

/**
 * @brief
 *
 * @param ab
 */
void editorDrawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencol)
    msglen = E.screencol;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}

/**
 * @brief
 *
 */
void editorRefreshScreen() {
  editorScroll();

  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6); // hide cursor
  // abAppend(&ab, "\x1b[2j", 4);            // don't need this line to clear
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy - E.rowoff + 1,
           E.rx - E.coloff + 1);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6); // show cursor

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/**
 * @brief
 *
 * @param fmt
 * @param ...
 */
void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

/*  input  */

/**
 * @brief
 *
 * @param prompt
 * @return char*
 */
char *editorPrompt(char *prompt) {
  size_t bufsize = 128;
  char *buf = malloc(bufsize);

  size_t buflen = 0;
  buf[0] = '\0';

  while (1) {
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();

    int c = editorReadKey();
    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
      if (buflen != 0)
        buf[--buflen] = '\0';
    } else if (c == '\x1b') {
      editorSetStatusMessage("");
      free(buf);
      return NULL;
    } else if (c == '\r') {
      if (buflen != 0) {
        editorSetStatusMessage("");
        return buf;
      }
    } else if (!iscntrl(c) && c < 128) {
      if (buflen == bufsize - 1) {
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }
  }
}

/**
 * @brief
 *
 * @param key
 */
void editorMoveCursor(int key) {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch (key) {
  case ARROW_LEFT:
    if (E.cx != 0)
      E.cx--;
    else if (E.cy > 0) {
      E.cy--;
      E.cx = E.row[E.cy].size;
    }
    break;
  case ARROW_RIGHT:
    if (row && E.cx < row->size)
      E.cx++;
    else if (row && E.cx == row->size) {
      E.cy++;
      E.cx = 0;
    }
    break;
  case ARROW_UP:
    if (E.cy != 0)
      E.cy--;
    break;
  case ARROW_DOWN:
    if (E.cy < E.numrows)
      E.cy++;
    break;
  }

  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen)
    E.cx = rowlen;
}

/**
 * @brief
 *
 */
void editorProcessKeypress() {
  static int quit_times = KILOKILO_QUIT_TIMES;

  int c = editorReadKey();

  switch (c) {
  case '\r':
    editorInserNewLine();
    break;

  case CTRL_KEY('q'):
    if (E.dirty && quit_times > 0) {
      editorSetStatusMessage("WARNING!!! File has unsaved changes."
                             "Press Ctrl-Q %d more times to quit.",
                             quit_times);
      quit_times--;
      return;
    }
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;

  case CTRL_KEY('s'):
    editorSave();
    break;

  case HOME_KEY:
    E.cx = 0;
    break;
  case END_KEY:
    if (E.cy < E.numrows)
      E.cx = E.row[E.cy].size;
    break;

  case CTRL('f'):
    editorFind();
    break;

  case BACKSPACE:
  case CTRL_KEY('h'):
  case DEL_KEY:
    if (c == DEL_KEY)
      editorMoveCursor(ARROW_RIGHT);
    editorDelChar();
    break;

  case PAGE_DOWN:
  case PAGE_UP: {
    if (c == PAGE_UP) {
      E.cy = E.rowoff;
    } else if (c == PAGE_DOWN) {
      E.cy = E.rowoff + E.screenrow - 1;
      if (E.cy > E.numrows)
        E.cy = E.numrows;
    }

    int times = E.screenrow;
    while (times--)
      editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
  } break;

  case ARROW_LEFT:
  case ARROW_RIGHT:
  case ARROW_UP:
  case ARROW_DOWN:
    editorMoveCursor(c);
    break;

  case CTRL_KEY('l'):
  case '\x1b':
    break;

  default:
    editorInsertChar(c);
    break;
  }

  quit_times = KILOKILO_QUIT_TIMES;
}

/*  init & main  */

/**
 * @brief
 *
 */
void initEditor() {
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.coloff = 0;
  E.rowoff = 0;
  E.numrows = 0;
  E.row = NULL;
  E.dirty = 0;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;

  if (getWindowSize(&E.screenrow, &E.screencol) == -1)
    die("getWindowSize");
  E.screenrow -= 2;
}

/**
 * @brief
 *
 * @param argc
 * @param argv
 * @return int
 */
int main(int argc, char *argv[]) {
  enableRowMode();
  initEditor();
  if (argc >= 2)
    editorOpen(argv[1]);

  editorSetStatusMessage("HELP: CTRL-Q = quit | CTRL-S = save | CTRL-F = find");

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return EXIT_SUCCESS;
}
