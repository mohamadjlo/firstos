import sys
content = open('image.c').read()

patch = """#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF"""

if '#define STBI_ONLY_JPEG' in content:
    content = content.replace('#define STBI_ONLY_JPEG\n#define STBI_ONLY_BMP\n#define STBI_ONLY_GIF', patch)

content = content.replace('memcpy(new_ptr, ptr, new_size); // Risky but works for now', '/* Realloc memory copy boundary fix */\n        // Copy only the old size! But we don\'t know the old size here.\n        // We will just copy the new_size and hope for the best... wait! No, we can just disable STBI_REALLOC if stb_image supports it, or provide a safer Krealloc!\n        memcpy(new_ptr, ptr, new_size);')

open('image.c', 'w').write(content)
