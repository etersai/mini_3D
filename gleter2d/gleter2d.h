#ifndef GLETER_2D_H
#define GLETER_2D_H

#include <glad/glad.h>
#include <linmath/linmath.h>
#define STBTT_STATIC  // To avoid conflict with Nuklear for my project.
#include <stb/stb_truetype.h>

/////////////////////////////////////
// DEPENDENCIES                    
// glad, linmath.h, stb_truetype.h 

//////////////////////////
// FEATURES
// Load ttf fonts.
// Render text using those fonts.
// Render rotated text.
// Hot reload build in shaders.
// Save textures as png.
// Render quads.
// Simple opengl Texture wrapper.

// TODO wglGetCurrentContext/Linux version to check if OpenGL started.
// TODO add line drawing.
// TODO add circle drawing.
// TODO add ebo to font rendering quads.

//////////
// GLE2D

// Some predefined colors.
#define GLE2D_COLOR_BLACK (vec4){0.0f, 0.0f, 0.0f, 1.0f}
#define GLE2D_COLOR_WHITE (vec4){1.0f, 1.0f, 1.0f, 1.0f}
#define GLE2D_COLOR_RED (vec4){1.0f, 0.0f, 0.0f, 1.0f}
#define GLE2D_COLOR_GREEN (vec4){0.0f, 1.0f, 0.0f, 1.0f}
#define GLE2D_COLOR_BLUE (vec4){0.0f, 0.0f, 1.0f, 1.0f}

//////////
// Enums
typedef enum {
    GLE2D_SHADER_SOLID = 0,
    GLE2D_SHADER_TEXTURED,
    GLE2D_SHADER_FONT,
    GLE2D_SHADER_COUNT
} GLE2D_ShaderType; 

// Run this after openGL is initialized.
int gle2d_init(void);
// Updates the orthographic projection matrix.
// Should be called whenever the viewport size changes.
void gle2d_update_rendering_area(int viewport_width, int viewport_height);
// at the end obviously.
void gle2d_shutdown(void);

// Textures and Fonts are both managed by the caller.

/////////////
// TEXTURES

typedef struct {
    GLuint id;
    int width;
    int height;
} GLE2D_Texture;

GLE2D_Texture gle2d_texture_load(const char* path);
void gle2d_texture_bind(GLE2D_Texture texture);
void gle2d_texture_delete(GLE2D_Texture texture);

///////////
// Shapes

void gle2d_update_time_uniform(float time);
// pass 0 to texture if you want to draw raw color quad.
void gle2d_shapes_draw_quad(float x, float y, float w, float h, float rotation, vec4 color, GLuint texture);
void gle2d_shapes_draw_circle(float x, float y, float radius, vec4 color);
void gle2d_shapes_draw_glpoint(float x, float y, float size, vec4 color);

//////////
// FONT 
typedef struct {
    stbtt_packedchar *packed_char_array;
    unsigned char* ttf_data;
    GLuint atlas;
    unsigned int num_chars;
    float px_size;
    float px_scale;
    int ascent;
    int descent;
    int linegap;
} GLE2D_Font;

int gle2d_font_create(GLE2D_Font* font, const char* path, float px_size);
void gle2d_font_render_text(const GLE2D_Font* font, vec4 color, const char *text, float x, float y);
void gle2d_font_render_text_rotation(const GLE2D_Font* font, const char* text, float x, float y, float rotation, vec4 color);
void gle2d_font_destroy(GLE2D_Font* font);

/////////
// MISC

// TODO: Add save to folder, assure folder is created, Add linux/windows compability.
// NOTES: Unbinds currently bound texture.
//        Requires texture format to be GL_RED (single channel, 8-bit).
int gle2d_misc_texture_save_to_disk_as_png(GLuint texture, const char* name);
int gle2d_misc_shader_hot_reload(GLE2D_ShaderType type, const char* vertex_file_path, const char* fragment_file_path);

#endif
