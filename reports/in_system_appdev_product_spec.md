# FirstOS App Studio v1: Product Specification

**Product**: FirstOS App Studio v1  
**Category**: Integrated Developer Environment & Application Lifecycle Manager  
**Version**: 1.0  
**Date**: October 12, 2026  

---

## 1. Product Overview

**App Studio v1** is the integrated application development environment native to FirstOS. It transforms FirstOS from an execution-only operating system into a self-hosting developer platform where developers can create projects from templates, edit code, configure permissions, build binaries, execute apps in isolated sandboxes, and inspect real-time diagnostics.

---

## 2. Directory Hierarchy & Project Structure

Every FirstOS application project is stored within `/Projects/<AppName>/`:

```
/Projects/<AppName>/
├── manifest.ini          # Application metadata, entry point, capabilities
├── src/
│   └── main.c            # Primary application source code
├── assets/
│   └── icon.ico          # Application icon
├── build/
│   └── app.elf           # Compiled userspace binary artifact
└── README.txt            # Project documentation and build notes
```

---

## 3. Project Manifest Specification (`manifest.ini`)

The project manifest uses a standard key-value format:

```ini
[app]
id=com.firstos.myfirstapp
name=My First App
version=1.0.0
author=FirstOS Developer
category=Utility
entry=/Projects/MyFirstApp/build/app.elf
icon=/Projects/MyFirstApp/assets/icon.ico

[window]
width=640
height=480
resizable=1
title=My First App Window

[permissions]
fs_read=1
fs_write=1
network=0
window=1
shm=0
```

---

## 4. App Studio UI Layout

The App Studio window is divided into 4 primary functional zones:

```
+-----------------------------------------------------------------------------------+
|  [New]  [Open]  [Save]  |  [Build]  [Run]  [Stop]  |  Template: [Hello App v]     | Top Action Bar
+---------------------+-------------------------------------------------------------+
| PROJECT TREE        | CODE EDITOR                                                 |
|                     |                                                             |
| > MyFirstApp/       |   #include <firstos/syscall.h>                              |
|   - manifest.ini    |                                                             |
|   > src/            |   int main(int argc, char **argv) {                         |
|     - main.c        |       firstos_write(1, "Hello World\n", 12);               |
|   > build/          |       return 0;                                             |
|     - app.elf       |   }                                                         |
|                     |                                                             |
|                     |                                                             |
+---------------------+-------------------------------------------------------------+
| OUTPUT & DIAGNOSTICS PANE                                                         |
| [build] Compiled /Projects/MyFirstApp/src/main.c -> /Projects/MyFirstApp/build/app.elf (OK)|
| [runner] Process spawned PID=0x03. State: RUNNING                                 |
| [stdout] Hello World                                                              |
| [runner] Process exited with code 0                                               |
+-----------------------------------------------------------------------------------+
| Ready | File: /Projects/MyFirstApp/src/main.c | Line: 4 Col: 10 | Status: Clean   | Status Bar
+-----------------------------------------------------------------------------------+
```

---

## 5. Core Workflows

1. **New Project**:
   - The user selects "New Project" and specifies a project name (`HelloWorld`, `Calculator`, `ImageViewer`).
   - The user chooses a project template (e.g. `Hello Window App`, `Text Viewer App`, `Console Demo`).
   - App Studio creates the project directory tree and populates `manifest.ini`, `src/main.c`, and `README.txt`.

2. **Editing & Code Modification**:
   - Multi-file browsing via the Project Tree.
   - Text editing with cursor navigation, selection, syntax keywords, and dirty status tracking (`[Save]`).

3. **In-System Build**:
   - The user clicks `[Build]`.
   - The In-System Build Service validates source files and produces a 64-bit ELF binary into `build/app.elf`.
   - Output pane displays compiler warnings, errors, and artifact size.

4. **Run, Supervise & Terminate**:
   - The user clicks `[Run]`.
   - The Run Service launches the compiled ELF in Ring 3 userspace with restricted capabilities.
   - Standard output/error are redirected and rendered live in the Output Pane.
   - The user can click `[Stop]` to terminate the active process gracefully.

5. **Reboot Persistence**:
   - All files are synchronized directly to `fs.img`.
   - Reopening App Studio loads the last active project state automatically.
