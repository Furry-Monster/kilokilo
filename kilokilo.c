/*  includes  */

// if compile error,includes these:
// #define _DEFAULT_SOURCE
// #define _BSD_SOURCE
// #define _GNU_SOURCE

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/*  defines */

#define KILOKILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
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

typedef struct erow {
  int size;
  char *chars;
} erow;

struct editorConfig {
  int cx, cy;
  int rowoff;
  int coloff;
  int screenrow;
  int screencol;
  int numrows;
  erow *row;
  struct termios orig_termios;
};

struct editorConfig E;

/*  terminal  */

void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4); // [2J will clear the terminal window

  write(STDOUT_FILENO, "\x1b[H", 3);
  // [H is another way of saying [1;1H ,
  // which will set cursor to the top left

  perror(s);
  exit(1);
}

void disableRowMode() {
  // this function will reset the terminal
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

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

void editorAppendRow(char *s, size_t len) {
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  int at = E.numrows;
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';
  E.numrows++;
}

/*  file i/o  */

void editorOpen(char *filename) {
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
    editorAppendRow(line, linelen);
  }
  free(line);
  fclose(fp);
}

/*  append buffer  */

struct abuf {
  char *b;
  int len;
};

// default buffer initalizer
#define ABUF_INIT                                                              \
  { NULL, 0 }

void abAppend(struct abuf *ab, const char *s, int len) {
  char *newBuf = realloc(ab->b, ab->len + len);

  if (newBuf == NULL)
    return;
  memcpy(&newBuf[ab->len], s, len);
  ab->b = newBuf;
  ab->len += len;
}

void abFree(struct abuf *ab) { free(ab->b); }

/*  input&output */

void editorScroll() {
  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrow) {
    E.rowoff = E.cy - E.screenrow + 1;
  }
}

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
      int len = E.row[filerow].size;
      if (len > E.screencol)
        len = E.screencol;
      abAppend(ab, E.row[filerow].chars, len);
    }

    abAppend(
        ab, "\x1b[K",
        3); // Clear when draw new line,instead of repainting the whole window
    if (y < E.screenrow - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
}

void editorRefreshScreen() {
  editorScroll();

  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6); // hide cursor
  // abAppend(&ab, "\x1b[2j", 4);            // don't need this line to clear
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy - E.rowoff + 1, E.cx + 1);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6); // show cursor

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

void editorMoveCursor(int key) {
  switch (key) {
  case ARROW_LEFT:
    if (E.cx != 0)
      E.cx--;
    break;
  case ARROW_RIGHT:
    if (E.cx != E.screencol - 1)
      E.cx++;
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
}

void editorProcessKeypress() {
  int c = editorReadKey();

  switch (c) {
  case CTRL_KEY('q'):
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;

  case HOME_KEY:
    E.cx = 0;
    break;
  case END_KEY:
    E.cx = E.screencol - 1;
    break;

  case PAGE_DOWN:
  case PAGE_UP: {
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
  }
}

/*  init & main  */

void initEditor() {
  E.cx = 0;
  E.cy = 0;
  E.coloff = 0;
  E.rowoff = 0;
  E.numrows = 0;
  E.row = NULL;

  if (getWindowSize(&E.screenrow, &E.screencol) == -1)
    die("getWindowSize");
}

int main(int argc, char *argv[]) {
  enableRowMode();
  initEditor();
  if (argc >= 2)
    editorOpen(argv[1]);

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return EXIT_SUCCESS;
}
