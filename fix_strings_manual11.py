with open('gui.c', 'r') as f:
    text = f.read()

import re

search = r"""if \(ide_saving_ui == 1\) \{\n                // Generate auto-detect import code"""

replace = """if (ide_saving_ui == 1) {
            text_to_save = ui_buf;
            len_to_save = 0; while(ui_buf[len_to_save]) len_to_save++;
        }
        if (created || fs_write_at(full_path, text_to_save, len_to_save)) {
            if (ide_save_dialog_open && !ide_saving_ui) {
                extern int ide_active_tab;
                extern int ide_saved;
                ide_saved = 1;
                int i=0; while(full_path[i]) { ide_tabs[ide_active_tab].filename[i] = full_path[i]; i++; }
                ide_tabs[ide_active_tab].filename[i] = 0;
            }
            if (ide_saving_ui == 2) ide_saving_ui = 0;
            if (ide_saving_ui == 1) {
                // Generate auto-detect import code"""

text = re.sub(search, replace, text, flags=re.MULTILINE)

with open('gui.c', 'w') as f:
    f.write(text)
