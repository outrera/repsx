REPSX is a special fork of PCSX emulator, geared towards sprite ripping and rom-hacking, also modified PCSX-r to easy compilation and hacking. It uses only SDL and compiles into a monolithic executable.

To compile and run game simply type "make && ./repsx cd1.bin cd2.bin …" If you have gcc and SDL setup, everything should go well.

To setup key bindings, just hit ESC go into Input menu

Default Bindings:
W = Start
Q = Select

R = Up
F = Down
D = Left
G = Right
T = L1
E = L2

NumPad8 = Triangle
NumPad6 = Circle
NumPad5 = Cross
NumPad4 = Square
NumPad7 = R1
NumPad9 = R2

P = Pause

Shift + F1 to F7 = save state
F1 to F7 = load saved state
F8 = undoes last load

A = starts/stops making screenshots under ./snaps/
V = view RAM: clicking SHIFT+LMB here sets write breakpoint, clicking SHIFT+RMB - breaks on read. Press B to remove all breakpoints;
B = view RAM in hex
N = view VRAM: clicking SHIFT+LMB here sets write breakpoint, clicking SHIFT+RMB - breaks on read.
C = dumps RAM and VRAM inside under ./snaps. You may need this to debug or rip sprites.

TODO: Just clicking inside emulator window should select texture/sprite and save it under ./snaps.

Have a nice day!
