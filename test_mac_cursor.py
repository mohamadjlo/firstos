mac_cursor = [
    "X",
    "XX",
    "X.X",
    "X..X",
    "X...X",
    "X....X",
    "X.....X",
    "X......X",
    "X.......X",
    "X........X",
    "X.........X",
    "X..........X",
    "X......XXXXX",
    "X...X..X",
    "X..X X..X",
    "X.X  X..X",
    "XX    X..X",
    "X     X..X",
    "       XX"
]
print("Height:", len(mac_cursor))
for r in mac_cursor:
    print(r.ljust(12))

