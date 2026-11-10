def hue_to_rgb(h):
    x = (h % 60) * 255 // 60
    if h < 60: return (255, x, 0)
    elif h < 120: return (255-x, 255, 0)
    elif h < 180: return (0, 255, x)
    elif h < 240: return (0, 255-x, 255)
    elif h < 300: return (x, 0, 255)
    else: return (255, 0, 255-x)

for x in range(130):
    h = (x * 360) // 130
    print(hue_to_rgb(h))
