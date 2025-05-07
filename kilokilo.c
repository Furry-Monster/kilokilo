/*  includes  */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*  defines */

#define KILOKILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

/*  data  */

struct editorConfig {
  int screenrow;
  int screencol;
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

char editorReadKey() {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }
  return c;
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

void editorDrawRows(struct abuf *ab) {
  int y;
  for (y = 0; y < E.screenrow; y++) {
    if (y == E.screenrow / 3) {
      // print welcome message here
      char welcome[80];
      int welcomelen =
        snprintf(welcome, sizeof(welcome), "KiloKilo editor -- version %s",
                 KILOKILO_VERSION);
      if (welcomelen > E.screencol)
        welcomelen = E.screencol; // Truncate if too long
      int padding = (E.screencol-welcomelen)/2;
      if(padding){
        abAppend(ab, "~", 1);
        padding--;
      }
      while(padding--)
        abAppend(ab, " ", 1);
      abAppend(ab, welcome, welcomelen);
    } else {
      abAppend(ab, "~", 1);
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
  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6); // hide cursor
  // abAppend(&ab, "\x1b[2j", 4);            // don't need this line to clear
  // window
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  abAppend(&ab, "\x1b[H", 3);
  abAppend(&ab, "\x1b[?25h", 6); // show cursor

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

void editorProcessKeypress() {
  char c = editorReadKey();

  switch (c) {
    case CTRL_KEY('q'):
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;
  }
}

/*  init & main  */

void initEditor() {
  if (getWindowSize(&E.screenrow, &E.screencol) == -1)
    die("getWindowSize");
}

int main(int argc, char *argv[]) {
  enableRowMode();
  initEditor();

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return EXIT_SUCCESS;
}
