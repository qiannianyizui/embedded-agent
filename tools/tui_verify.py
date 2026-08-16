import os, pty, select, struct, termios, fcntl, time, signal, re, unicodedata
from PIL import Image, ImageDraw, ImageFont

BIN = "/home/lsy/embedded-agent/build/ea"
ROWS, COLS = 40, 120
DFG = (232, 234, 242)
DBG = (13, 17, 23)

class Cell:
    __slots__ = ("ch","fg","bg","bold","dim","inv")
    def __init__(self):
        self.ch = " "; self.fg = DFG; self.bg = DBG
        self.bold = False; self.dim = False; self.inv = False

class Term:
    def __init__(self, rows, cols):
        self.rows = rows; self.cols = cols
        self.grid = [[Cell() for _ in range(cols)] for _ in range(rows)]
        self.x = 0; self.y = 0; self.wrap = False
        self.fg = DFG; self.bg = DBG
        self.bold = False; self.dim = False; self.inv = False
    def scroll(self):
        self.grid.pop(0); self.grid.append([Cell() for _ in range(self.cols)])
    def put(self, ch):
        w = 2 if unicodedata.east_asian_width(ch) in ("W","F") else 1
        if self.wrap:
            self.y += 1
            if self.y >= self.rows: self.scroll(); self.y = self.rows-1
            self.x = 0; self.wrap = False
        if 0 <= self.y < self.rows and 0 <= self.x < self.cols:
            c = self.grid[self.y][self.x]
            c.ch = ch; c.fg = self.fg; c.bg = self.bg
            c.bold = self.bold; c.dim = self.dim; c.inv = self.inv
            if w == 2 and self.x+1 < self.cols:
                n = self.grid[self.y][self.x+1]
                n.ch = ""; n.fg = self.fg; n.bg = self.bg
                n.bold = self.bold; n.dim = self.dim; n.inv = self.inv
        self.x += w
        if self.x >= self.cols:
            self.x = self.cols-1; self.wrap = True
    def move(self, y, x):
        self.y = max(0, min(self.rows-1, y)); self.x = max(0, min(self.cols-1, x))
        self.wrap = False
    def feed(self, data):
        i = 0; n = len(data)
        while i < n:
            ch = data[i]
            if ch == "\x1b":
                m = re.match(r"\x1b\[([0-9;?]*)([ -/]*)([@-~])", data[i:])
                if m:
                    params = m.group(1); cmd = m.group(3)
                    if cmd == "m": self.apply_sgr(params)
                    elif cmd in ("H","f"):
                        p = params.split(";") if params else []
                        self.move(int(p[0])-1 if p and p[0] else 0,
                                  int(p[1])-1 if len(p)>1 and p[1] else 0)
                    elif cmd == "A": self.move(self.y-(int(params or 1)), self.x)
                    elif cmd == "B": self.move(self.y+(int(params or 1)), self.x)
                    elif cmd == "C": self.move(self.y, self.x+(int(params or 1)))
                    elif cmd == "D": self.move(self.y, self.x-(int(params or 1)))
                    elif cmd == "G": self.move(self.y, int(params or 1)-1)
                    elif cmd == "J":
                        if params == "2": self.grid = [[Cell() for _ in range(self.cols)] for _ in range(self.rows)]
                        else:
                            for yy in range(self.y, self.rows):
                                for xx in range(self.x if yy==self.y else 0, self.cols):
                                    self.grid[yy][xx] = Cell()
                    elif cmd == "K":
                        for xx in range(self.x, self.cols): self.grid[self.y][xx] = Cell()
                    i += m.end(); continue
                if data[i:i+2] == "\x1b]":
                    j = data.find("\x07", i); k = data.find("\x1b\\", i)
                    ends = [e for e in (j,k) if e != -1]
                    if ends:
                        i = min(ends) + (1 if min(ends)==j else 2); continue
                i += 1; continue
            if ch == "\r": self.x = 0; self.wrap = False; i += 1; continue
            if ch == "\n":
                self.y += 1
                if self.y >= self.rows: self.scroll(); self.y = self.rows-1
                self.x = 0; self.wrap = False; i += 1; continue
            if ch == "\x00": i += 1; continue
            self.put(ch); i += 1
    def apply_sgr(self, params):
        parts = (params or "0").split(";"); j = 0
        while j < len(parts):
            p = parts[j]
            if p in ("","0"): self.fg=DFG; self.bg=DBG; self.bold=False; self.dim=False; self.inv=False
            elif p == "1": self.bold = True
            elif p == "2": self.dim = True
            elif p == "22": self.bold = self.dim = False
            elif p == "7": self.inv = True
            elif p == "27": self.inv = False
            elif p == "39": self.fg = DFG
            elif p == "49": self.bg = DBG
            elif p == "38" and j+3 < len(parts) and parts[j+1] == "2":
                self.fg = tuple(int(x) for x in parts[j+2:j+5]); j += 4
            elif p == "48" and j+3 < len(parts) and parts[j+1] == "2":
                self.bg = tuple(int(x) for x in parts[j+2:j+5]); j += 4
            j += 1

def dump(term, label):
    print("=====", label, "=====")
    for y in range(max(0, ROWS-12), ROWS):
        cells = []
        for c in term.grid[y]:
            if not c.ch: cells.append(" ")
            elif c.bg == (232,234,242): cells.append("#")
            else: cells.append(c.ch)
        print(f"{y:02d}|{''.join(cells).rstrip()}")

def snapshot_to_png(term, path):
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 18)
    bold = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 18)
    ascent, descent = font.getmetrics()
    cell_w = round(font.getlength("M")); line_h = 18 + max(descent, 2)
    img = Image.new("RGB", (term.cols*cell_w, term.rows*line_h), DBG)
    d = ImageDraw.Draw(img)
    for y,row in enumerate(term.grid):
        x = 0
        for c in row:
            if not c.ch:
                x += cell_w; continue
            w = 2 if unicodedata.east_asian_width(c.ch) in ("W","F") else 1
            f,b = c.fg,c.bg
            if c.inv: f,b = b,f
            if b != DBG:
                d.rectangle((x, y*line_h, x+w*cell_w, (y+1)*line_h), fill=b)
            color = tuple(round(b[k]*0.55+f[k]*0.45) for k in range(3)) if c.dim else f
            fnt = bold if c.bold else font
            d.text((x, y*line_h+ascent), c.ch, font=fnt, fill=color, anchor="ls")
            x += w*cell_w
    img.save(path)
    print("wrote", path)

def read_for(fd, secs):
    buf = b""; end = time.monotonic()+secs
    while time.monotonic() < end:
        r,_,_ = select.select([fd],[],[],0.1)
        if r:
            try:
                chunk = os.read(fd, 65536)
                if not chunk: break
                buf += chunk
            except OSError: break
    return buf

pid, master = pty.fork()
if pid == 0:
    os.environ.pop("NO_COLOR", None)
    os.environ["COLORTERM"] = "truecolor"
    os.environ["TERM"] = "xterm-256color"
    os.execv(BIN, [BIN])
fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
time.sleep(2.0)
term = Term(ROWS, COLS)
term.feed(read_for(master, 1.5).decode("utf-8","replace"))
dump(term, "startup")
snapshot_to_png(term, "/tmp/tui_startup.png")

def step(data, wait, label):
    os.write(master, data)
    time.sleep(wait)
    term.feed(read_for(master, 0.5).decode("utf-8","replace"))
    dump(term, label)
    snapshot_to_png(term, "/tmp/tui_" + label + ".png")

step(b"/", 0.6, "popup")
step(b"\x1b[B", 0.3, "arrow1")
step(b"\x1b[B", 0.3, "arrow2")
step(b"\x7f", 0.6, "after_delete")

os.kill(pid, signal.SIGTERM)
try: os.waitpid(pid,0)
except ChildProcessError: pass
