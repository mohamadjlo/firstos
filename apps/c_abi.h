#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handles for the C kernel
typedef void* ExplorerHandle;
typedef void* MediaPlayerHandle;

// Explorer C-ABI
ExplorerHandle Explorer_Create(void);
void Explorer_Destroy(ExplorerHandle handle);
void Explorer_SetSearchFilter(ExplorerHandle handle, const char* query);

// Media Player C-ABI
MediaPlayerHandle MediaPlayer_Create(void);
void MediaPlayer_Destroy(MediaPlayerHandle handle);
void MediaPlayer_LoadFile(MediaPlayerHandle handle, const char* path);

#ifdef __cplusplus
}
#endif
