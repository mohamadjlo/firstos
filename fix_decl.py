import re

with open('gui.c', 'r') as f:
    content = f.read()

# Remove the ones from patch_open
content = content.replace("static char audio_player_current_file[64] = \"\";\nstatic int audio_player_is_playing = 0;\n", "")

# Add them to the top
content = content.replace("static int desktop_context_is_explorer;", "static int desktop_context_is_explorer;\nstatic char audio_player_current_file[64] = \"\";\nstatic int audio_player_is_playing = 0;")

with open('gui.c', 'w') as f:
    f.write(content)
