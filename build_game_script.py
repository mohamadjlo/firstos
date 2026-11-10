code = """class init:
  function Main:
    set x to 10
    set dir to 5
    while x < 300:
      clear ui
      add rect at (x, 100) size (40, 40)
      set x to x + dir
      if x > 250:
        set dir to -5
      if x < 10:
        set dir to 5
      delay 10
    end
    print "Game loop finished!"
  end
end
"""
# wait, `size (40, 40)` is NOT supported by firstlang!
# `add rect at (x, y)` adds a fixed size 60x60 in my implementation!
