#!/usr/bin/env python3
"""Ground station for the Pathfinder flight software: an old control room monitor.

    python3 simulation/ground/ground_station.py [port]   (default 5006), or: make ground GUI=1

It listens for the downlink frames (one burst of telemetry packets per frame: every 0.755 s
at the high data rate) and shows the lander telemetry:

    DOWNLINK          the signal (ACQUIRED, AWAITING SIGNAL, LOSS OF SIGNAL), and the DATA
                      RATE buttons
    ALARMS            the lamps: MASTER ALARM, RESETS, LoS (loss of signal)
    ACCELEROMETERS    X, Y, Z, each bar zoomed on its axis, so even the noise shows
    RADAR ALTIMETER   off on Mars
    ASI/MET           temperature and pressure, and the temperature history
    bc_dist RESPONSE  the worst response time of distribution in each second, against its
    TIME              deadline
    TELEMETRY STREAM  the last packets, as Earth turns them into text, and the SHELL line
    IMP CAMERA        the last image, and the panorama being composed

A SYSTEM RESET: the frames stop while the lander boots again (LoS), then its boot report
lights RESETS and flashes MASTER ALARM until SPACE (or a click) acknowledges it. The seconds
the reset took are shaded red in the response history, and an image lost in the reset
leaves its frame of the panorama black. ESC closes the monitor.

The SHELL line, at the bottom of the TELEMETRY STREAM, sends a line of C to the shell of the
lander, as JPL did on Mars to fix it (rtos/system/system.h):

    rtos_pipe_mutex_protocol = PTHREAD_PRIO_INHERIT;

The patch goes up slowly, at the rate of the uplink (a bar shows it), the lander runs it,
and the next frame reports the result: done, or rejected and why. If no reply comes (a reset
lost the patch, or its reply), after a few seconds the shell says the reply timed out.
While you type in the shell the keys of the monitor (1, 2, 3, SPACE, ESC) do not work: ESC
or a click leaves it.

The lander starts at a data rate drawn at random. The DATA RATE buttons (or keys 1, 2, 3)
send the command "set the data rate" to LOW, MEDIUM, HIGH (UDP port 5007, to Earth:
deep_space_network.c): the higher the data rate, the more often the lander sends a frame.
A button turns amber when the command is sent, and green when the lander is really using
that data rate, as Earth receives it.

The IMP CAMERA panel shows the last image the imp task has compressed, and composes the
Presidential Panorama, the 360 degree mosaic of IMP images taken from the lander in 1997
(NASA/JPL): it starts black, as nothing has been seen yet, and each image fills in its frame
of the mosaic. Each new mosaic is taken at another time of the day: afternoon, dusk, night,
dawn, morning, day again.
"""

import base64
import math
import os
import struct
import socket
import sys
import time
import zlib

try:
    import tkinter as tk
    import tkinter.font as tkfont
except ImportError:
    sys.exit("tkinter is missing. On Ubuntu / Debian install it with:  sudo apt install python3-tk")

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 5006
COMMAND_PORT = 5007                     # Earth listens here for our commands

# ---------------------------------------------------------------- downlink
DATA_RATES = ("LOW", "MEDIUM", "HIGH")  # as Earth numbers them: 0, 1, 2
RATE_KEYS = {"1": 0, "2": 1, "3": 2}    # key -> data rate
FRAME_TIME_S = (3.020, 1.510, 0.755)    # frame time at each data rate (rtos/drivers/radio/radio.h)

# ---------------------------------------------------------------- IMP camera
IMAGES_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "images")
PANORAMA_FILE = os.path.join(IMAGES_DIR, "presidential_panorama.png")        # the 360 degrees
EMPTY_FILE = os.path.join(IMAGES_DIR, "mosaic_empty.png")                    # black, with the frames
LARGE_FILE = os.path.join(IMAGES_DIR, "presidential_panorama_large.png")     # sharp: the big frame
FRAME_ROWS, FRAME_COLUMNS = 3, 17       # the panorama as a mosaic of frames, one per image
# the light of each mosaic, one after the other: a colour laid over the panorama, and how much
# of it (0 = none, 255 = only the colour)
LIGHTS = (("DAY", (0, 0, 0), 0),
          ("AFTERNOON", (255, 120, 30), 60),
          ("DUSK", (80, 20, 60), 130),
          ("NIGHT", (0, 5, 30), 210),
          ("DAWN", (90, 40, 90), 130),
          ("MORNING", (255, 210, 170), 35))

# ---------------------------------------------------------------- screen
WIDTH, HEIGHT = 1200, 820
REFRESH_MS = 100                        # redraw 10 times a second (enough for a CRT)
NO_SIGNAL_FRAMES = 2.5                  # no frame for this many frame times -> no signal
HISTORY = 40                            # points kept in the small charts
RESPONSE_SCALE_MS = 200                 # full scale of the response time gauges
MARS_G = 3.711                          # [m/s^2]
# the accelerometer bars, zoomed on each axis: centre and half range [m/s^2]. X and Y around 0
# (the tilt), Z around Mars g: so even the noise of the sensor moves them
ACCEL_RANGES = ((0.0, 0.2), (0.0, 0.2), (MARS_G, 0.2))
ACCEL_BLOCK = 4                         # the bars in blocks of 4 px: fine enough for the noise

# ---------------------------------------------------------------- colours (green phosphor)
BACKGROUND = "#020c05"
PHOSPHOR = "#39ff6a"                    # values
DIM = "#2fae55"                         # labels
FAINT = "#14602a"                       # frames, tracks, grid
AMBER = "#ffb52e"                       # warnings
RED = "#ff3b30"                         # alarms
RED_FAINT = "#4a1512"                   # the seconds a reset took, in the response history
SCANLINE = "#000000"

FONT = ("Courier", 11, "bold")
FONT_SMALL = ("Courier", 10, "bold")
FONT_TITLE = ("Courier", 12, "bold")
FONT_HEADER = ("Courier", 17, "bold")
FONT_VALUE = ("Courier", 15, "bold")
FONT_BIG = ("Courier", 24, "bold")
CAP_HEIGHT = 0.935                      # height of the capitals / ascent of the font (Nimbus Mono PS,
                                        # the Courier of Linux): to centre the capitals, not the font

# ---------------------------------------------------------------- layout (x0, y0, x1, y1)
P_ACCEL = (20, 140, 590, 330)
P_ALTIMETER = (20, 345, 590, 405)
P_ASI_MET = (20, 420, 590, 605)
P_TIMING = (610, 140, 1180, 405)
P_STREAM = (610, 420, 1180, 605)
P_IMP = (20, 620, 1180, 805)
STATUS_BOXES = ((20, 92, 590, 130), (610, 92, 1180, 130))     # aligned with the two columns
STATUS_TOP, STATUS_BOTTOM = 101, 123                          # lamps and buttons: under the box titles
LAMPS = ("MASTER ALARM", "RESETS", "LoS")                     # the annunciator, in the right box
RATE_BUTTON_WIDTH = 72                                        # the data rate buttons, in the left box
LOG_LINES = 7                           # lines of the stream: under them, the shell
SHELL_RESULTS = ("DONE", "SYNTAX ERROR", "UNKNOWN VARIABLE", "BAD VALUE")  # as the lander's shell numbers them
SHELL_RESULT_S = 10                     # the reply of the lander stays on the shell line this long
# the reply goes down at the first transmission slot after the patch: within one frame time at the
# lowest data rate, or 0.5 s more just after a boot. Not come by then: lost in a reset (the patch,
# or its reply)
PATCH_REPLY_TIMEOUT_S = 2 * FRAME_TIME_S[0]
TIMED_OUT = -1                          # patch_result when no reply came: not a result of the lander


def parse(packet):
    """'PATHFINDER t=<ms> kind=<kind> v=a,b,c,d text=<text>'  ->  dict, or None if it is not ours"""
    head, _, text = packet.decode(errors="replace").partition(" text=")
    words = head.split()
    if not words or words[0] != "PATHFINDER":
        return None
    data = {"text": text}
    try:
        for word in words[1:]:
            name, _, value = word.partition("=")
            if name == "t":
                data["t"] = int(value)
            elif name == "v":
                data["v"] = [float(x) for x in value.split(",")]
            else:
                data[name] = value
    except ValueError:
        return None
    if "t" not in data or "kind" not in data or len(data.get("v", [])) != 4:
        return None
    return data


def tint_pixel(colour, opacity):
    """a 1x1 PNG of this colour, partly transparent: Tk lays it over a region of an image"""
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    header = struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)          # 1x1, 8 bit, RGBA
    pixel = zlib.compress(bytes((0,) + tuple(colour) + (opacity,)))
    return base64.b64encode(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", pixel) +
                            chunk(b"IEND", b""))


def met(t_ms):
    """mission elapsed time, from the ms since the start of the lander"""
    seconds = t_ms / 1000
    return "T+%02d:%02d:%06.3f" % (seconds // 3600, seconds % 3600 // 60, seconds % 60)


class Monitor:
    def __init__(self, root):
        self.root = root
        root.title("Mars Pathfinder - ground station  (UDP port %d)" % PORT)
        root.configure(bg=BACKGROUND)
        root.resizable(False, False)

        # UDP socket: we only listen, Earth (deep_space_network.c) sends
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", PORT))
        self.sock.setblocking(False)
        # and one to send the commands to Earth
        self.command_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

        self.canvas = tk.Canvas(root, width=WIDTH, height=HEIGHT, bg=BACKGROUND, highlightthickness=0)
        self.canvas.pack()
        self.font_metrics = {}              # font -> (ascent, descent), for place()
        self.new_mission()
        self.draw_static()
        self.make_rate_buttons()
        self.make_camera()
        self.make_shell()

        # the keys of the monitor: not while typing in the shell
        root.bind("<space>", self.on_acknowledge)
        root.bind("<Return>", self.on_acknowledge)
        root.bind("<Button-1>", self.on_click)
        root.bind("<Escape>", self.on_escape)
        root.bind("<Key>", self.on_key)     # any other key: it brings back the shell after a reply
        for key, rate in RATE_KEYS.items():
            root.bind(key, lambda event, rate=rate: self.on_rate(event, rate))
        root.protocol("WM_DELETE_WINDOW", self.close)
        self.refresh()

    # ------------------------------------------------------------ state
    def new_mission(self):
        """forget everything: called at start and when the lander is started again"""
        self.downlink_t = None              # t of the last downlink [ms]
        self.downlink_time = 0.0            # when it arrived (our clock)
        self.data_rate = None               # data rate of the last frame: 0 low, 1 medium, 2 high
        self.commanded_rate = None          # data rate sent to the lander, not yet in use
        self.accel = None                   # x, y, z [m/s^2]
        self.altimeter = None               # valid (1 = read, 0 = not read), altitude [cm]
        self.asi_met = None                 # temperature [C], pressure [Pa]
        self.images = 0                     # number of the last image compressed by the imp task
        self.new_images = []                # images received, not in the mosaic yet
        self.compression_s = None           # time to compress the last one [s]
        self.temperatures = []
        self.deadline_ms = 100
        self.responses = []                 # each second: (worst response or None = no data, lost to a reset)
        self.reset_pending = False          # a boot report arrived: the next empty seconds were the reset
        self.worst_ms = 0
        self.second = None                  # second of the lander we are collecting
        self.second_worst = 0               # worst response in it, so far
        self.resets = 0
        self.alarm = False                  # master alarm on, waiting for acknowledgement
        self.log = []                       # (t, text, colour)
        self.patch_state = "idle"           # the shell: "idle", "uplink" (going up), "sent", "result"
        self.patch_result = 0               # the reply of the lander: an index in SHELL_RESULTS
        self.result_until = 0.0             # the reply stays on the shell line until then (our clock)
        self.reply_until = 0.0              # the reply must arrive by then, or it timed out (our clock)
        self.uplink_percent = 0             # of the patch going up

    def acknowledge(self):
        self.alarm = False

    # the keys and the clicks: the shell, while it has the keyboard, takes them for itself
    def typing(self, event):
        return event.widget is self.shell

    def on_acknowledge(self, event):
        if not self.typing(event):
            self.acknowledge()

    def on_click(self, event):
        self.acknowledge()
        if self.patch_state == "result" and self.on_shell_line(event):
            self.back_to_shell()            # a click on the reply brings back the shell
        elif not self.typing(event):
            self.canvas.focus_set()         # a click outside the shell leaves it

    def on_key(self, event):
        if self.patch_state == "result":    # typing after a reply: back to the shell, with the key
            self.back_to_shell()
            if len(event.char) == 1 and event.char.isprintable():
                self.shell.insert("end", event.char)

    def on_shell_line(self, event):
        x0, y0, x1, y1 = P_STREAM
        return event.widget is self.canvas and x0 <= event.x <= x1 and abs(event.y - self.shell_y) <= 12

    def back_to_shell(self):
        self.patch_state = "idle"
        self.canvas.itemconfigure(self.shell_item, state="normal")
        self.shell.focus_set()

    def on_escape(self, event):
        if not self.typing(event):
            self.close()

    def on_rate(self, event, rate):
        if not self.typing(event):
            self.command_rate(rate)

    def now(self):
        """the time of the lander, as the ground sees it [ms]"""
        t = 0
        if self.downlink_t is not None:
            t = self.downlink_t + int((time.time() - self.downlink_time) * 1000)
        return t

    def command_rate(self, rate):
        """uplink: ask the lander for a new data rate"""
        self.command_sock.sendto(b"PATHFINDER-COMMAND rate=%d" % rate, ("127.0.0.1", COMMAND_PORT))
        self.commanded_rate = rate
        self.log = (self.log + [(self.now(), "EARTH        : command sent, data rate %s" % DATA_RATES[rate],
                                 PHOSPHOR)])[-LOG_LINES:]

    # ------------------------------------------------------------ the shell
    def make_shell(self):
        """the line where the operator types a patch: a Tk entry, made once, in the stream panel"""
        x0, y0, x1, y1 = P_STREAM
        self.shell_y = y1 - 20
        # the field is a frame inside a border drawn on the canvas (draw_shell): the entry sits in
        # the frame, lowered so that its capitals are in the middle (Tk places the whole height of
        # the font, with the room below the baseline)
        self.shell_box = (x0 + 84, self.shell_y - 12, x1 - 12, self.shell_y + 11)
        box = tk.Frame(self.canvas, bg=BACKGROUND, highlightthickness=0)
        self.shell = tk.Entry(box, font=FONT_SMALL, bg=BACKGROUND, fg=PHOSPHOR, insertbackground=PHOSPHOR,
                              relief="flat", borderwidth=0, highlightthickness=0)
        middle = self.place(0, FONT_SMALL, "center")          # how much lower the capitals need it
        ascent, descent = self.font_metrics[FONT_SMALL]
        self.shell.place(x=4, rely=0.5, y=middle, anchor="w", relwidth=1, width=-8,
                         height=ascent + descent)               # just the font: inside the border
        bx0, by0, bx1, by1 = self.shell_box
        self.shell_item = self.canvas.create_window(bx0 + 1, by0 + 1, window=box, anchor="nw",
                                                    width=bx1 - bx0 - 1, height=by1 - by0 - 1)
        self.shell.bind("<Return>", self.send_patch)
        self.shell.bind("<Escape>", lambda event: self.canvas.focus_set())

    def send_patch(self, event):
        """uplink: a line of C for the shell of the lander"""
        line = self.shell.get().strip()
        if line and self.patch_state == "idle":
            self.command_sock.sendto(("PATHFINDER-COMMAND patch=%s" % line).encode(), ("127.0.0.1", COMMAND_PORT))
            self.log = (self.log + [(self.now(), "PATCH        : %s" % line, PHOSPHOR)])[-LOG_LINES:]
            self.shell.delete(0, "end")
            self.patch_state = "uplink"
            self.uplink_percent = 0
            self.canvas.focus_set()         # the field is hidden while the patch goes up
        return "break"                      # Enter is the shell's: it does not acknowledge the alarm

    def draw_shell(self):
        """the shell line: where to type, or the patch going up, or waiting for the reply, or the
        reply of the lander"""
        x0, y0, x1, y1 = P_STREAM
        y = self.shell_y
        bx0, by0, bx1, by1 = self.shell_box
        self.text(x0 + 20, y, "SHELL >", DIM, FONT_SMALL)
        if self.patch_state == "result" and time.time() > self.result_until:
            self.patch_state = "idle"
        if self.patch_state == "sent" and time.time() > self.reply_until:
            self.patch_state = "result"     # no reply: a reset lost the patch, or its reply
            self.patch_result = TIMED_OUT
            self.result_until = time.time() + SHELL_RESULT_S
            self.log = (self.log + [(self.now(), "EARTH        : patch, no reply from the lander: timed out",
                                     RED)])[-LOG_LINES:]

        if self.patch_state == "idle":
            self.canvas.itemconfigure(self.shell_item, state="normal")
            border = FAINT
            if self.root.focus_get() is self.shell:
                border = DIM                # typing: brighter
            self.canvas.create_rectangle(bx0, by0, bx1, by1, outline=border, tags="dyn")
        else:
            self.canvas.itemconfigure(self.shell_item, state="hidden")
            if self.patch_state == "uplink":
                self.canvas.create_rectangle(bx0, by0, bx1, by1, outline=FAINT, tags="dyn")
                filled = (bx1 - bx0 - 6) * min(self.uplink_percent, 100) / 100
                self.segments(bx0 + 3, bx0 + 3 + filled, y, 14, lambda i: AMBER)
                self.text(bx1 - 6, y, "PATCH GOING UP  %.0f%%" % self.uplink_percent, AMBER, FONT_SMALL, "e")
            elif self.patch_state == "sent":
                self.canvas.create_rectangle(bx0, by0, bx1, by1, outline=FAINT, tags="dyn")
                self.text(bx0 + 6, y, "PATCH SENT: WAITING FOR THE REPLY OF THE LANDER", AMBER, FONT_SMALL)
            else:
                colour = PHOSPHOR
                reply = "PATCH DONE"
                if self.patch_result == TIMED_OUT:
                    colour = RED
                    reply = "PATCH REPLY TIMED OUT"
                elif self.patch_result != 0:
                    colour = RED
                    reason = "UNKNOWN RESULT"
                    if 0 < self.patch_result < len(SHELL_RESULTS):
                        reason = SHELL_RESULTS[self.patch_result]
                    reply = "PATCH REJECTED: " + reason
                self.canvas.create_rectangle(bx0, by0, bx1, by1, outline=colour, width=2, tags="dyn")
                self.text(bx0 + 6, y, reply, colour, FONT)

    # ------------------------------------------------------------ receiving
    def receive(self):
        """read every packet waiting"""
        while True:
            try:
                packet = self.sock.recv(65536)
            except BlockingIOError:
                return
            data = parse(packet)
            if data:
                self.handle(data)

    def handle(self, d):
        kind, t, v = d["kind"], d["t"], d["v"]

        if kind == "downlink":
            if self.downlink_t is not None and t < self.downlink_t:
                self.new_mission()          # the lander has been started again
            self.downlink_t, self.downlink_time = t, time.time()
            self.data_rate = min(max(int(v[0]), 0), 2)
            return

        colour = DIM
        if kind == "accelerometers":
            if v[0]:                        # read (valid)
                self.accel = v[1:4]
        elif kind == "altimeter":
            self.altimeter = v
        elif kind == "asi_met":
            self.asi_met = v
            self.temperatures = (self.temperatures + [v[0]])[-HISTORY:]
        elif kind == "imp":
            self.images = int(v[0])
            self.new_images.append(self.images)
            self.compression_s = v[1] / 1000
        elif kind == "timing":
            # one per bus cycle: the worst of each second is computed here, on the ground. The
            # seconds with no data (a lost frame, a reset) stay empty in the history: after a
            # boot report they are the reset
            second = t // 1000
            if second != self.second:
                if self.second is not None and second > self.second:
                    self.add_second(self.second_worst, False)
                    for _ in range(min(second - self.second - 1, HISTORY)):
                        self.add_second(None, self.reset_pending)
                    self.reset_pending = False
                self.second, self.second_worst = second, 0
            self.second_worst = max(self.second_worst, v[0])
            self.deadline_ms = v[1]
            self.worst_ms = max(self.worst_ms, v[0])
            return                          # plotted, not logged
        elif kind == "reset":                 # the boot report, after a reset
            self.resets = int(v[0])
            self.reset_pending = True
            self.alarm = True
            self.root.bell()
            colour = RED
        elif kind == "deadline":
            colour = RED
        elif kind == "uplink":                # the patch going up: the bar, not the stream
            self.uplink_percent = v[0]
            if v[0] >= 100 and self.patch_state != "sent":
                self.patch_state = "sent"
                self.reply_until = time.time() + PATCH_REPLY_TIMEOUT_S
            return
        elif kind == "patch":                 # the reply of the lander: done, or rejected
            self.patch_state = "result"       # on the shell line for a while, and in the stream (even
                                              # after a timeout: a late reply is the real result)
            self.patch_result = int(v[1])
            self.result_until = time.time() + SHELL_RESULT_S
            colour = PHOSPHOR
            if v[1] != 0:
                colour = RED
        self.log = (self.log + [(t, d["text"], colour)])[-LOG_LINES:]

    # ------------------------------------------------------------ drawing helpers
    def panel(self, box, title):
        x0, y0, x1, y1 = box
        c = self.canvas
        c.create_rectangle(x0, y0, x1, y1, outline=FAINT, width=2)
        for x, y in ((x0, y0), (x1, y0), (x0, y1), (x1, y1)):           # corner marks
            c.create_rectangle(x - 3, y - 3, x + 3, y + 3, fill=DIM, outline="")
        width = tkfont.Font(font=FONT_TITLE).measure(title) + 16
        c.create_rectangle(x0 + 14, y0 - 9, x0 + 14 + width, y0 + 9, fill=BACKGROUND, outline="")
        c.create_text(x0 + 22, self.place(y0, FONT_TITLE, "w"), text=title, anchor="w", font=FONT_TITLE, fill=DIM)

    def place(self, y, font, anchor):
        """the y to give Tk so that the CAPITALS (and digits) sit at y: their middle for the anchors
        w, e, center, their baseline for s, sw, se. Tk places the whole height of the font instead,
        with the room below the baseline that capitals and digits never use"""
        if font not in self.font_metrics:
            metrics = tkfont.Font(font=font).metrics()
            self.font_metrics[font] = (metrics["ascent"], metrics["descent"])
        ascent, descent = self.font_metrics[font]
        placed = y
        if anchor in ("s", "sw", "se"):
            placed = y + descent
        elif anchor in ("w", "e", "center"):
            placed = y + (CAP_HEIGHT * ascent + descent - ascent) / 2
        return placed

    def text(self, x, y, text, colour=PHOSPHOR, font=FONT, anchor="w", tags="dyn"):
        self.canvas.create_text(x, self.place(y, font, anchor), text=text, anchor=anchor, font=font, fill=colour,
                                tags=tags)

    def segments(self, x0, x1, y, height, colour_of, block=7):
        """an LED bar: one block every 'block' px from x0 towards x1, colour_of(i) gives each block"""
        step = block if x1 >= x0 else -block
        for i in range(int(abs(x1 - x0) // block)):
            x = x0 + i * step
            self.canvas.create_rectangle(x, y - height / 2, x + step * 5 / 7, y + height / 2,
                                         fill=colour_of(i), outline="", tags="dyn")

    def blink(self, hz=2):
        return int(time.time() * hz * 2) % 2 == 0

    # ------------------------------------------------------------ static part
    def draw_static(self):
        c = self.canvas
        c.create_text(20, self.place(30, FONT_HEADER, "w"), text="JET PROPULSION LABORATORY  //  MARS PATHFINDER",
                      anchor="w", font=FONT_HEADER, fill=AMBER)
        c.create_text(20, self.place(58, FONT_SMALL, "w"),
                      text="LANDER FLIGHT SOFTWARE  -  GROUND STATION MONITOR  -  UDP %d" % PORT,
                      anchor="w", font=FONT_SMALL, fill=DIM)
        c.create_line(20, 74, WIDTH - 20, 74, fill=FAINT, width=2)
        self.panel(STATUS_BOXES[0], "DOWNLINK")
        self.panel(STATUS_BOXES[1], "ALARMS")
        self.panel(P_ACCEL, "ACCELEROMETERS")
        self.panel(P_ALTIMETER, "RADAR ALTIMETER")
        self.panel(P_ASI_MET, "ASI/MET ATMOSPHERE")
        self.panel(P_TIMING, "bc_dist RESPONSE TIME")
        self.panel(P_STREAM, "TELEMETRY STREAM")
        self.panel(P_IMP, "IMP CAMERA")
        for y in range(0, HEIGHT, 3):                                       # CRT scanlines
            c.create_line(0, y, WIDTH, y, fill=SCANLINE, stipple="gray50", tags="scanline",
                          state="disabled")             # half-transparent, not clickable

    # ------------------------------------------------------------ periodic update
    def refresh(self):
        self.receive()
        self.canvas.delete("dyn")
        self.draw_header()
        self.draw_status()
        self.draw_accelerometers()
        self.draw_altimeter()
        self.draw_asi_met()
        self.draw_timing()
        self.draw_stream()
        self.draw_shell()
        self.draw_camera()
        self.canvas.tag_raise("scanline")
        self.root.after(REFRESH_MS, self.refresh)

    def connected(self):
        if self.downlink_t is None:
            return False
        return time.time() - self.downlink_time < NO_SIGNAL_FRAMES * FRAME_TIME_S[self.data_rate]

    def draw_header(self):
        self.text(WIDTH - 20, 30, "EARTH " + time.strftime("%H:%M:%S UTC", time.gmtime()),
                  colour=PHOSPHOR, font=FONT_VALUE, anchor="e")
        if self.downlink_t is not None:
            elapsed = time.time() - self.downlink_time
            self.text(WIDTH - 20, 58, "MISSION ELAPSED " + met(self.downlink_t + elapsed * 1000)[:-4],
                      colour=DIM, font=FONT_SMALL, anchor="e")

    # ------------------------------------------------------------ the data rate buttons
    def make_rate_buttons(self):
        """drawn on the canvas, like the lamps, at the right end of the DOWNLINK box: here only
        their place and what a click does (each button has a tag, the click is bound to it)"""
        _, y0, x1, y1 = STATUS_BOXES[0]
        left = x1 - 10 - 3 * RATE_BUTTON_WIDTH - 2 * 6
        self.rate_boxes = {}
        for rate in range(len(DATA_RATES)):
            bx0 = left + rate * (RATE_BUTTON_WIDTH + 6)
            self.rate_boxes[rate] = (bx0, STATUS_TOP, bx0 + RATE_BUTTON_WIDTH, STATUS_BOTTOM)
            tag = "rate%d" % rate
            self.canvas.tag_bind(tag, "<Button-1>", lambda event, rate=rate: self.command_rate(rate))
            self.canvas.tag_bind(tag, "<Enter>", lambda event: self.canvas.config(cursor="hand2"))
            self.canvas.tag_bind(tag, "<Leave>", lambda event: self.canvas.config(cursor=""))
        self.rate_label_x = left - 10

    def draw_rate_buttons(self):
        """green = the data rate in use, amber = sent, waiting for the lander"""
        if self.commanded_rate == self.data_rate:
            self.commanded_rate = None
        self.text(self.rate_label_x, (STATUS_TOP + STATUS_BOTTOM) / 2, "DATA RATE", DIM, FONT_SMALL, "e")
        for rate, (bx0, by0, bx1, by1) in self.rate_boxes.items():
            if rate == self.data_rate and self.connected():
                fill, outline, colour = PHOSPHOR, PHOSPHOR, BACKGROUND
            elif rate == self.commanded_rate:
                fill, outline, colour = BACKGROUND, AMBER, AMBER
            else:
                fill, outline, colour = BACKGROUND, DIM, PHOSPHOR
            tags = ("dyn", "rate%d" % rate)
            self.canvas.create_rectangle(bx0, by0, bx1, by1, fill=fill, outline=outline, width=2, tags=tags)
            self.text((bx0 + bx1) / 2, (by0 + by1) / 2, DATA_RATES[rate], colour, FONT_SMALL, "center", tags)

    def draw_status(self):
        x0, y0, x1, y1 = STATUS_BOXES[0]
        y = (STATUS_TOP + STATUS_BOTTOM) / 2
        self.draw_rate_buttons()
        if self.connected():
            self.text(x0 + 16, y, "ACQUIRED", PHOSPHOR, FONT_VALUE)
        elif self.downlink_t is None:
            if self.blink(1):
                self.text(x0 + 16, y, "AWAITING SIGNAL", AMBER, FONT_VALUE)
        elif self.blink():
            self.text(x0 + 16, y, "LOSS OF SIGNAL", RED, FONT_VALUE)

        self.draw_annunciator()

    def draw_annunciator(self):
        """a row of labelled lamps, as on a console: lit = the condition is there. MASTER ALARM
        flashes after a reset until it is acknowledged; the other lamps say which alarm it is"""
        x0, y0, x1, y1 = STATUS_BOXES[1]
        lit = {
            "MASTER ALARM": RED if self.alarm and self.blink() else None,
            "RESETS": RED if self.resets else None,
            "LoS": AMBER if self.downlink_t is not None and not self.connected() else None,
        }
        width = (x1 - x0 - 12) / len(LAMPS)
        for i, name in enumerate(LAMPS):
            lx0, lx1 = x0 + 8 + i * width, x0 + 4 + (i + 1) * width
            label = name
            if name == "RESETS":
                label = "RESETS %d" % self.resets
            colour = lit[name]
            if colour:
                self.canvas.create_rectangle(lx0, STATUS_TOP, lx1, STATUS_BOTTOM, fill=colour, outline=colour,
                                             tags="dyn")
                self.text((lx0 + lx1) / 2, (STATUS_TOP + STATUS_BOTTOM) / 2, label, BACKGROUND, FONT, "center")
            else:
                self.canvas.create_rectangle(lx0, STATUS_TOP, lx1, STATUS_BOTTOM, outline=FAINT, tags="dyn")
                self.text((lx0 + lx1) / 2, (STATUS_TOP + STATUS_BOTTOM) / 2, label, DIM, FONT, "center")

    def draw_accelerometers(self):
        x0, y0, x1, y1 = P_ACCEL
        if self.accel is None:
            self.text(x0 + 20, (y0 + y1) / 2, "NO DATA", DIM, FONT_VALUE)
            return
        middle = (x0 + 250 + x1 - 20) / 2
        half = (x1 - 20 - (x0 + 250)) / 2
        for i, (axis, value, (centre, span)) in enumerate(zip("XYZ", self.accel[:3], ACCEL_RANGES)):
            y = y0 + 34 + 40 * i
            self.text(x0 + 20, y, axis, DIM, FONT_VALUE)
            self.text(x0 + 50, y, "%+7.3f m/s2" % value, PHOSPHOR, FONT_VALUE)
            self.canvas.create_line(middle, y - 12, middle, y + 12, fill=DIM, width=2, tags="dyn")
            self.canvas.create_rectangle(middle - half, y - 7, middle + half, y + 7, outline=FAINT, tags="dyn")
            length = max(-1.0, min(1.0, (value - centre) / span)) * half
            self.segments(middle + (2 if length > 0 else -2), middle + length, y, 10, lambda i: PHOSPHOR,
                          ACCEL_BLOCK)
            # the range of this bar, under its ends
            self.text(middle - half, y + 17, "%.1f" % (centre - span), DIM, FONT_SMALL, "w")
            self.text(middle + half, y + 17, "%.1f" % (centre + span), DIM, FONT_SMALL, "e")
        x, y, z = self.accel[:3]
        g = math.sqrt(x * x + y * y + z * z)
        tilt = math.degrees(math.acos(max(-1.0, min(1.0, z / g)))) if g > 0 else 0
        self.text(x0 + 20, y1 - 28, "|a| %.3f m/s2   MARS g %.3f   TILT %.1f deg" % (g, MARS_G, tilt), DIM)
        at_rest = abs(g - MARS_G) < 0.2
        self.text(x1 - 20, y1 - 28, "AT REST" if at_rest else "MOVING", PHOSPHOR if at_rest else AMBER,
                  FONT, "e")

    def draw_altimeter(self):
        x0, y0, x1, y1 = P_ALTIMETER
        y = (y0 + y1) / 2 + 4
        if self.altimeter is None:
            self.text(x0 + 20, y, "NO DATA", DIM, FONT_VALUE)
        elif self.altimeter[0] == 0:
            self.text(x0 + 20, y, "UNPOWERED  -  NO READING", DIM, FONT_VALUE)
            self.text(x1 - 20, y, "entry & landing only", DIM, FONT_SMALL, "e")
        else:
            self.text(x0 + 20, y, "%d cm" % self.altimeter[1], PHOSPHOR, FONT_VALUE)

    def draw_asi_met(self):
        x0, y0, x1, y1 = P_ASI_MET
        if self.asi_met is None:
            self.text(x0 + 20, y0 + 50, "NO DATA  (asi_met runs every second)", DIM, FONT_VALUE)
            return
        temperature, pressure = self.asi_met[:2]
        self.text(x0 + 20, y0 + 22, "AIR TEMPERATURE", DIM, FONT_SMALL)
        self.text(x0 + 20, y0 + 58, "%+.0f C" % temperature, PHOSPHOR, FONT_BIG)
        self.text(x0 + 280, y0 + 22, "PRESSURE", DIM, FONT_SMALL)
        self.text(x0 + 280, y0 + 58, "%.0f Pa" % pressure, PHOSPHOR, FONT_BIG)

        # temperature history: one line, the title names it
        bx0, by0, bx1, by1 = x0 + 20, y0 + 100, x1 - 20, y1 - 18
        c = self.canvas
        c.create_rectangle(bx0, by0, bx1, by1, outline=FAINT, tags="dyn")
        self.text(bx0 + 6, by0 + 10, "AIR TEMPERATURE, LAST %d READINGS" % HISTORY, DIM, FONT_SMALL)
        if len(self.temperatures) < 2:
            return
        low, high = min(self.temperatures) - 2, max(self.temperatures) + 2
        step = (bx1 - bx0 - 70) / (HISTORY - 1)

        def point(i, value):
            return bx0 + 10 + i * step, by1 - 8 - (value - low) / (high - low) * (by1 - by0 - 30)

        points = [coordinate for i, value in enumerate(self.temperatures) for coordinate in point(i, value)]
        c.create_line(*points, fill=PHOSPHOR, width=2, tags="dyn")
        x, y = point(len(self.temperatures) - 1, self.temperatures[-1])
        c.create_oval(x - 4, y - 4, x + 4, y + 4, fill=PHOSPHOR, outline=BACKGROUND, width=2, tags="dyn")
        self.text(bx1 - 6, by0 + 26, "%+.0f C" % (high - 2), DIM, FONT_SMALL, "e")
        self.text(bx1 - 6, by1 - 10, "%+.0f C" % (low + 2), DIM, FONT_SMALL, "e")

    def add_second(self, worst, reset):
        """one more second in the response history: its worst response (None = no data), and
        whether a reset took it"""
        self.responses = (self.responses + [(worst, reset)])[-HISTORY:]

    def timing_state(self):
        """what the readout shows: "none" (nothing yet), "reset" (the last seconds were a
        reset), "nodata" (no signal, or the last second had no data), "value" """
        state = "value"
        last, last_reset = None, False
        if self.responses:
            last, last_reset = self.responses[-1]
        if self.reset_pending or (last is None and last_reset):
            state = "reset"
        elif not self.responses:
            state = "none"
        elif last is None or not self.connected():
            state = "nodata"
        return state

    def draw_timing(self):
        x0, y0, x1, y1 = P_TIMING
        deadline = self.deadline_ms
        state = self.timing_state()
        if state == "none":
            self.text(x0 + 20, y0 + 50, "NO DATA", DIM, FONT_VALUE)
            return
        self.text(x0 + 20, y0 + 22, "WORST IN THE LAST SECOND", DIM, FONT_SMALL)
        self.text(x0 + 260, y0 + 44, "DEADLINE  %.0f ms" % deadline, DIM)
        self.text(x0 + 260, y0 + 66, "WORST SEEN %.0f ms" % self.worst_ms, DIM)

        # the readout, and the gauge: green up to the deadline, red after it. After a reset
        # Earth knows no time, only that distribution missed its deadline: full, red
        gx0, gx1, gy = x0 + 20, x1 - 20, y0 + 100
        scale = (gx1 - gx0) / RESPONSE_SCALE_MS
        self.canvas.create_rectangle(gx0, gy - 8, gx1, gy + 8, outline=FAINT, tags="dyn")
        if state == "reset":
            self.text(x0 + 20, y0 + 56, "RESET", RED, FONT_BIG)
            if self.blink():
                self.text(x1 - 20, y0 + 22, "DEADLINE MISSED", RED, FONT, "e")
            self.segments(gx0 + 2, gx1 - 2, gy, 12, lambda i: RED)
        elif state == "nodata":
            self.text(x0 + 20, y0 + 56, "--- ms", DIM, FONT_BIG)
        else:
            response = self.responses[-1][0]
            late = response > deadline
            colour = PHOSPHOR
            if late:
                colour = RED
            self.text(x0 + 20, y0 + 56, "%3.0f ms" % response, colour, FONT_BIG)
            if late:
                if self.blink():
                    self.text(x1 - 20, y0 + 22, "DEADLINE MISSED", RED, FONT, "e")
            else:
                self.text(x1 - 20, y0 + 22, "WITHIN DEADLINE", PHOSPHOR, FONT, "e")
            filled = min(response, RESPONSE_SCALE_MS) * scale
            self.segments(gx0 + 2, gx0 + filled, gy, 12,
                          lambda i: PHOSPHOR if i * 7 / scale <= deadline else RED)
        dx = gx0 + deadline * scale
        self.canvas.create_line(dx, gy - 14, dx, gy + 14, fill=AMBER, width=2, tags="dyn")
        self.text(gx0, gy + 20, "0", DIM, FONT_SMALL)
        self.text(dx, gy + 20, "D", AMBER, FONT_SMALL, "center")
        self.text(gx1, gy + 20, "%d ms" % RESPONSE_SCALE_MS, DIM, FONT_SMALL, "e")

        # history: one bar per second, the deadline as a dashed line. A second with no data
        # stays empty; the seconds a reset took are shaded, all the way up
        hx0, hy0, hx1, hy1 = x0 + 20, y0 + 140, x1 - 20, y1 - 14
        self.canvas.create_rectangle(hx0, hy0, hx1, hy1, outline=FAINT, tags="dyn")
        height = hy1 - hy0 - 24
        bar = (hx1 - hx0 - 12) / HISTORY
        for i, (response, reset) in enumerate(self.responses):
            if response is None and reset:
                self.canvas.create_rectangle(hx0 + 6 + i * bar, hy0 + 2, hx0 + 6 + (i + 1) * bar, hy1 - 2,
                                             fill=RED_FAINT, outline="", tags="dyn")
            elif response is not None:
                top = hy1 - 4 - min(response, RESPONSE_SCALE_MS) / RESPONSE_SCALE_MS * height
                self.canvas.create_rectangle(hx0 + 6 + i * bar, top, hx0 + 6 + (i + 1) * bar - 2, hy1 - 4,
                                             fill=RED if response > deadline else DIM, outline="", tags="dyn")
        self.text(hx0 + 6, hy0 + 10, "ONE BAR PER SECOND", DIM, FONT_SMALL)
        dy = hy1 - 4 - deadline / RESPONSE_SCALE_MS * height
        self.canvas.create_line(hx0, dy, hx1, dy, fill=AMBER, dash=(4, 3), tags="dyn")

    def draw_stream(self):
        x0, y0, x1, y1 = P_STREAM
        if not self.log:
            self.text(x0 + 20, y0 + 40, "awaiting downlink", DIM, FONT_VALUE)
            self.text(x0 + 20, y0 + 70, "start the lander:  make lander", DIM, FONT)
            return
        font = tkfont.Font(font=FONT_SMALL)
        room = x1 - 12 - (x0 + 82)                                # for the text
        for i, (t, text, colour) in enumerate(self.log):
            y = y0 + 20 + 19 * i
            if font.measure(text) > room:                         # never out of the panel: cut it
                while font.measure(text + "...") > room:
                    text = text[:-1]
                text = text + "..."
            self.text(x0 + 72, y, "%.1f s" % (t / 1000), DIM, FONT_SMALL, "e")
            self.text(x0 + 82, y, text, colour, FONT_SMALL)

    # ------------------------------------------------------------ the IMP camera
    def make_camera(self):
        """the big frame is cut from the sharp panorama; the mosaic starts empty, and each image
        copies its frame from the panorama"""
        x0, y0, x1, y1 = P_IMP
        self.camera = False
        try:
            self.panorama = tk.PhotoImage(file=PANORAMA_FILE)   # keep them: Tk forgets
            self.empty = tk.PhotoImage(file=EMPTY_FILE)         # unreferenced images
            self.large = tk.PhotoImage(file=LARGE_FILE)
        except tk.TclError:
            return
        self.frame_size = self.large.height() // FRAME_ROWS
        self.frame = tk.PhotoImage(width=self.frame_size, height=self.frame_size)
        self.frame_x, self.frame_y = x0 + 12, y0 + 14
        self.frame_item = self.canvas.create_image(self.frame_x, self.frame_y, image=self.frame, anchor="nw",
                                                   state="hidden")
        self.mosaic = tk.PhotoImage(width=self.panorama.width(), height=self.panorama.height())
        self.mosaic_x, self.mosaic_y = self.frame_x + self.frame_size + 16, y0 + 14
        self.info_x = self.mosaic_x + self.panorama.width() + 16         # the text column
        self.canvas.create_image(self.mosaic_x, self.mosaic_y, image=self.mosaic, anchor="nw")
        self.mosaic.tk.call(self.mosaic, "copy", self.empty)
        self.tints = [tk.PhotoImage(data=tint_pixel(colour, opacity)) for _, colour, opacity in LIGHTS]
        self.frame_index = 0                # frame of the last image in the mosaic: 0, 1, ...
        self.mosaic_number = 0              # which mosaic it is: 0, 1, ... (its light: LIGHTS)
        self.shown_image = 0                # image whose frame is in the big frame
        self.camera = True

    def frame_box(self, frame, image):
        """the frame number 'frame' (0, 1, ...) of the mosaic: its box in 'image', one of the
        panoramas. The mosaic goes along the rows, one full turn per row"""
        column, row = frame % FRAME_COLUMNS, frame // FRAME_COLUMNS
        w, h = image.width(), image.height()
        return (round(column * w / FRAME_COLUMNS), round(row * h / FRAME_ROWS),
                round((column + 1) * w / FRAME_COLUMNS), round((row + 1) * h / FRAME_ROWS))

    def compose(self):
        """light up the frame of each image received: its number says where the camera was
        pointing, so an image lost on the way (a reset) leaves its frame black. Cut the frame of
        the last image, big, from the sharp panorama. A new mosaic starts again empty, at
        another time of the day; a lander started again from power on, a new mission"""
        frames = FRAME_ROWS * FRAME_COLUMNS
        if self.images == 0:
            if self.shown_image != 0:       # a new mission
                self.mosaic.tk.call(self.mosaic, "copy", self.empty)
                self.mosaic_number = 0
            self.canvas.itemconfigure(self.frame_item, state="hidden")
            self.shown_image = 0
        light = self.mosaic_number % len(LIGHTS)
        for image in self.new_images:
            frame = (image - 1) % frames
            mosaic_number = (image - 1) // frames
            light = mosaic_number % len(LIGHTS)
            if image < self.shown_image or mosaic_number != self.mosaic_number:
                self.mosaic_number = mosaic_number      # a new mosaic, or a new mission
                self.mosaic.tk.call(self.mosaic, "copy", self.empty)
            fx0, fy0, fx1, fy1 = self.frame_box(frame, self.panorama)
            self.mosaic.tk.call(self.mosaic, "copy", self.panorama, "-from", fx0, fy0, fx1, fy1,
                                "-to", fx0, fy0)
            if LIGHTS[light][2] > 0:
                self.mosaic.tk.call(self.mosaic, "copy", self.tints[light], "-to", fx0, fy0, fx1, fy1,
                                    "-compositingrule", "overlay")
            self.frame_index = frame
            self.shown_image = image
        if self.new_images:
            lx0, ly0, lx1, ly1 = self.frame_box(self.frame_index, self.large)
            self.frame.tk.call(self.frame, "copy", self.large, "-from", lx0, ly0,
                               lx0 + self.frame_size, ly0 + self.frame_size, "-to", 0, 0)
            if LIGHTS[light][2] > 0:
                self.frame.tk.call(self.frame, "copy", self.tints[light], "-to", 0, 0, self.frame_size,
                                   self.frame_size, "-compositingrule", "overlay")
            self.canvas.itemconfigure(self.frame_item, state="normal")
        self.new_images = []

    def draw_camera(self):
        x0, y0, x1, y1 = P_IMP
        if not self.camera:
            self.text(x0 + 20, (y0 + y1) / 2, "NO IMAGES  (%s missing)" % os.path.basename(PANORAMA_FILE),
                      DIM, FONT_SMALL)
            return
        self.compose()

        # the azimuth under the panorama, the credit under the text column: their bottom on the
        # bottom of the big frame
        w, h = self.panorama.width(), self.panorama.height()
        bottom = self.frame_y + self.frame_size
        for degrees in range(0, 361, 45):
            self.text(self.mosaic_x + w * degrees / 360, bottom, "%d" % degrees, DIM, FONT_SMALL, "s")
        self.text(x1 - 12, bottom, "NASA/JPL 1997", DIM, FONT_SMALL, "se")

        tx = self.info_x
        if self.images == 0:
            self.canvas.create_rectangle(self.frame_x, self.frame_y, self.frame_x + self.frame_size,
                                         self.frame_y + self.frame_size, outline=FAINT, tags="dyn")
            self.text(self.frame_x + self.frame_size / 2, self.frame_y + self.frame_size / 2, "NO IMAGE", DIM,
                      FONT_VALUE, "center")
            self.text(tx, y0 + 26, "NO IMAGE YET", DIM, FONT_SMALL)
            self.text(tx, y0 + 46, "each image", DIM, FONT_SMALL)
            self.text(tx, y0 + 62, "fills in its", DIM, FONT_SMALL)
            self.text(tx, y0 + 78, "frame", DIM, FONT_SMALL)
            return

        # the last image: its numbers, and its frame outlined in the mosaic
        fx0, fy0, fx1, fy1 = self.frame_box(self.frame_index, self.panorama)
        azimuth = (fx0 + fx1) / 2 / w * 360
        self.text(tx, y0 + 26, "IMAGE NUMBER", DIM, FONT_SMALL)
        self.text(tx, y0 + 48, "%d" % self.images, PHOSPHOR, FONT_VALUE)
        self.text(tx, y0 + 74, "COMPRESSED IN", DIM, FONT_SMALL)
        self.text(tx, y0 + 90, "%.1f s" % self.compression_s, PHOSPHOR, FONT)
        self.text(tx, y0 + 112, "AZIMUTH", DIM, FONT_SMALL)
        self.text(tx, y0 + 128, "%.0f deg" % azimuth, PHOSPHOR, FONT)
        self.text(tx, y0 + 150, "FRAME %d OF %d" % (self.frame_index + 1, FRAME_ROWS * FRAME_COLUMNS), DIM, FONT_SMALL)
        self.canvas.create_rectangle(self.mosaic_x + fx0, self.mosaic_y + fy0, self.mosaic_x + fx1,
                                     self.mosaic_y + fy1, outline=AMBER, width=2, tags="dyn")

    def close(self):
        self.sock.close()
        self.command_sock.close()
        self.root.destroy()


def main():
    root = tk.Tk()
    try:
        monitor = Monitor(root)
    except OSError as error:
        root.destroy()
        sys.exit("Cannot listen on UDP port %d: %s (is another monitor already running?)" % (PORT, error))
    try:
        root.mainloop()
    except KeyboardInterrupt:
        monitor.close()


if __name__ == "__main__":
    main()
