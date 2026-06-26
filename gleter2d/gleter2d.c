#include "gleter2d.h"

#include <stdio.h>
#include <float.h>
#include <string.h>
#include <assert.h>

#include <glad/glad.h>
#include <linmath/linmath.h>
#define STBTT_STATIC // To avoid conflict with Nuklear for my project.
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#define GLE2D_DEFAULT_SHADER_VERSION "#version 460 core\n"

#define GLE2D_FONT_ATLAS_SIZE 1024
#define GLE2D_FONT_MAX_TEXT_LEN 1024    
#define GLE2D_FONT_FIRST_CHAR 32
#define GLE2D_FONT_LAST_CHAR 95

//////////////////////////////////////////////////////
// Do only what needs to be done, build up your app //
// from smaller, simple, concise functions.         //
//////////////////////////////////////////////////////

typedef struct {
    mat4x4 projection_matrix;
    mat4x4 model_matrix;

    GLuint uniform_buffer;

    GLuint program_shapes_solid;
    GLint  solid_loc_u_color;
    GLint  solid_loc_u_time;
    GLint  solid_loc_u_res;
    GLint  solid_loc_u_model;

    GLuint program_shapes_textured;
    GLint  textured_loc_u_color;
    GLint  textured_loc_u_model; 
    GLint  textured_loc_u_texture;

    GLuint program_font;
    GLint  font_loc_u_color; 
    GLint  font_loc_u_text;

    GLuint shapes_quad_vao;
    GLuint shapes_quad_vbo;
    GLuint shapes_quad_ebo;

    float*        font_batch_buffer;
    GLE2D_Texture font_dynamic_texutre;
    GLuint        font_fbo;
    GLuint        font_quad_vao;
    GLuint        font_quad_vbo;

    float         time;
    int           cached_viewport_width;
    int           cached_viewport_height;
} GLE2D_Context;

static GLE2D_Context context;

void util_print_mat4x4(const mat4x4 M)
{
    int i, j;
    fprintf(stdout, "-------\n");
	for(i=0; i<4; ++i) {
        for(j=0; j<4; ++j) {
            fprintf(stdout, "%.6f ", M[j][i]);
        }
        putchar('\n');
    }
    fprintf(stdout, "-------\n");
    fflush(stdout);
}

// GLE2D internal //
static unsigned char* gle2d_internal_font_load_into_memory(const char *path);
static void           gle2d_internal_font_calculate_text_bounding_box(const GLE2D_Font* font, const char* text, vec2 out);
static GLE2D_Texture  gle2d_internal_texture_create_empty_texture_for_font_rendering(int width, int height);
static GLuint         gle2d_internal_create_shader_from_data(const char* vertex, const char* fragment);
static void           gle2d_internal_update_rendering_area_and_projection(int viewport_width, int viewport_height);
static int            gle2d_internal_shader_compile_error(GLuint shader);
static int            gle2d_internal_shader_program_link_error(GLuint shader);
static int            gle2d_internal_init_shader_programs(void);
static unsigned char* gle2d_internal_load_file_as_string_null_terminate(const char* path);
//----------------//

// Good to know //
// GLint prev_viewport[4];
// glGetIntegerv(GL_VIEWPORT, prev_viewport);
//--------------//

int gle2d_init(void)
{
    if (gle2d_internal_init_shader_programs()) {
        return 1;
    }

    float quad[] = {
        -0.5f, -0.5f,
        -0.5f, 0.5f,
        0.5f, 0.5f, 
        0.5f, -0.5f,
    };

    unsigned int quad_indices[] = {
        0, 3, 1,
        2, 1, 3,
    };

    // now create the buffers.
    glGenVertexArrays(1, &context.shapes_quad_vao);
    glGenBuffers(1, &context.shapes_quad_vbo);
    glGenBuffers(1, &context.shapes_quad_ebo);
    glBindVertexArray(context.shapes_quad_vao); // start recording.
    glBindBuffer(GL_ARRAY_BUFFER, context.shapes_quad_vbo); // send the data.
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, context.shapes_quad_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quad_indices), quad_indices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, 0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0); //cleanup.
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    int max_buffer_size = GLE2D_FONT_MAX_TEXT_LEN * 6 * 4 * sizeof(float);
    // First glGen...
    glGenVertexArrays(1, &context.font_quad_vao);
    glGenBuffers(1, &context.font_quad_vbo);
    glBindVertexArray(context.font_quad_vao); // Start recording...
    glBindBuffer(GL_ARRAY_BUFFER, context.font_quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, max_buffer_size, NULL, GL_DYNAMIC_DRAW); 
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0); // "Cleanup"...
    glBindVertexArray(0); // EBO must be unboud after VBO
                          // unbind beacouse it is "recording".

    context.font_batch_buffer = malloc(max_buffer_size);
    if (!context.font_batch_buffer) {
        return 1;
    }
                                                                                            // some dummy value.
    context.font_dynamic_texutre = gle2d_internal_texture_create_empty_texture_for_font_rendering(10, 10); 
    if (context.font_dynamic_texutre.id == 0) {
        return 1;
    }
    
    // Seprate framebuffer for text rotations.
    glGenFramebuffers(1, &context.font_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, context.font_fbo); 
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, context.font_dynamic_texutre.id, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        return 1;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);  

    mat4x4_identity(context.model_matrix);
    mat4x4_identity(context.projection_matrix);
    context.time = 0.0f;

    return 0;
}

static int gle2d_internal_init_shader_programs(void)
{
    // SHAPES SOLID //
    const char* shapes_solid_vert =
    GLE2D_DEFAULT_SHADER_VERSION
    "layout (location = 0) in vec2 in_pos;\n"
    "out vec4 vs_pass_color;\n"
    "uniform vec4 u_color;\n"
    "uniform mat4 u_model;\n"
    "layout (std140) uniform u_BlockProjectionMatrix\n"
    "{\n"
    "   mat4 projection;\n"
    "};\n"
    "void main()\n"
    "{\n"
    "   gl_Position = projection * u_model * vec4(in_pos, 0.0, 1.0);\n"
    "   vs_pass_color = u_color.rgba;\n"
    "}\n";

    const char* shapes_solid_frag =
    GLE2D_DEFAULT_SHADER_VERSION
    "out vec4 FinalColor;\n"
    "in vec4 vs_pass_color;\n"
    "uniform vec2 u_res;\n"
    "uniform float u_time;\n"
    "void main()\n"
    "{\n"
    "   vec2 uv = (gl_FragCoord.xy / u_res.xy) * 2.0 - 1.0;\n"
    "   FinalColor = vs_pass_color;\n"
    "}\n";


    // SHAPES TEXTURED //
    const char* shapes_textured_vert =
    GLE2D_DEFAULT_SHADER_VERSION
    "layout (location = 0) in vec2 in_pos;\n"
    "out vec4 vs_pass_color;\n"
    "out vec2 vs_pass_texture_uv;\n"
    "uniform vec4 u_color;\n"
    "uniform mat4 u_model;\n"
    "layout (std140) uniform u_BlockProjectionMatrix\n"
    "{\n"
    "   mat4 projection;\n"
    "};\n"
    "void main()\n"
    "{\n"
    "   gl_Position = projection * u_model * vec4(in_pos, 0.0, 1.0);\n"
    "   vs_pass_color = u_color;\n"
    "   vs_pass_texture_uv = vec2(in_pos.x, in_pos.y) + vec2(0.5, 0.5);\n"
    "}\n";

    const char* shapes_textured_frag =
    GLE2D_DEFAULT_SHADER_VERSION
    "out vec4 FinalColor;\n"
    "in vec4 vs_pass_color;\n"
    "in vec2 vs_pass_texture_uv;\n"
    "uniform sampler2D u_texture;\n"
    "void main()\n"
    "{\n"
    "   FinalColor = texture(u_texture, vs_pass_texture_uv) * vs_pass_color;\n" 
    "}\n";

    // SHAPES CIRCLE //
    // TBC.......... //

    // FONT //
    const char* font_vertex_src = 
    GLE2D_DEFAULT_SHADER_VERSION
    "layout (location = 0) in vec4 vertex;\n"
    "out vec2 TexCoords;\n"
    "layout (std140) uniform u_BlockProjectionMatrix\n"
    "{\n"
    "   mat4 projection;\n"
    "};\n"
    "void main()\n"
    "{\n"
    "	gl_Position = projection * vec4(vertex.xy, 0.0, 1.0);\n"
    "   TexCoords = vertex.zw;\n"
    "}\n";

    const char* font_fragment_src = 
    GLE2D_DEFAULT_SHADER_VERSION
    "in vec2 TexCoords;\n"
    "out vec4 color;\n"
    "uniform sampler2D u_text;\n"
    "uniform vec4 u_color;\n"
    "void main()\n"
    "{\n"
    "   vec4 sampled = vec4(1.0, 1.0, 1.0, texture(u_text, TexCoords).r);\n"
    "   color = u_color * sampled;\n"
    "}\n";

    context.program_shapes_solid = gle2d_internal_create_shader_from_data(shapes_solid_vert, shapes_solid_frag);
    context.program_shapes_textured = gle2d_internal_create_shader_from_data(shapes_textured_vert, shapes_textured_frag);
    context.program_font = gle2d_internal_create_shader_from_data(font_vertex_src, font_fragment_src);

    if (context.program_shapes_solid    == 0 ||
        context.program_shapes_textured == 0 ||
        context.program_font            == 0) {
            return 1;
    }

    context.solid_loc_u_color = glGetUniformLocation(context.program_shapes_solid, "u_color");
    context.solid_loc_u_model = glGetUniformLocation(context.program_shapes_solid, "u_model");
    context.solid_loc_u_res = glGetUniformLocation(context.program_shapes_solid, "u_res");
    context.solid_loc_u_time = glGetUniformLocation(context.program_shapes_solid, "u_time");

    context.textured_loc_u_color = glGetUniformLocation(context.program_shapes_textured, "u_color");
    context.textured_loc_u_model = glGetUniformLocation(context.program_shapes_textured, "u_model"); 
    context.textured_loc_u_texture = glGetUniformLocation(context.program_shapes_textured, "u_texture");  

    context.font_loc_u_color =  glGetUniformLocation(context.program_font, "u_color");
    context.font_loc_u_text = glGetUniformLocation(context.program_font, "u_text");

    int ubo_index = 0;
    GLuint block_index_one = glGetUniformBlockIndex(context.program_shapes_solid, "u_BlockProjectionMatrix");
    GLuint block_index_two = glGetUniformBlockIndex(context.program_shapes_textured, "u_BlockProjectionMatrix");
    GLuint block_index_three = glGetUniformBlockIndex(context.program_font, "u_BlockProjectionMatrix");
    glUniformBlockBinding(context.program_shapes_solid, block_index_one, ubo_index);
    glUniformBlockBinding(context.program_shapes_textured, block_index_two, ubo_index);
    glUniformBlockBinding(context.program_font, block_index_three, ubo_index);

    glGenBuffers(1, &context.uniform_buffer);
    glBindBuffer(GL_UNIFORM_BUFFER, context.uniform_buffer);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(mat4x4), NULL, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, ubo_index, context.uniform_buffer);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    // map sampler to texture unit 0
    glUseProgram(context.program_shapes_textured);
    glUniform1i(context.textured_loc_u_texture, 0);
    glUseProgram(context.program_font);
    glUniform1i(context.font_loc_u_text, 0);
    glUseProgram(0); // TODO check if this is needed.

    return 0;
}

// The current drawable framebuffer dimensions (the values you pass to glViewport).
void gle2d_update_rendering_area(int viewport_width, int viewport_height)
{
    glViewport(0, 0, viewport_width, viewport_height);

    // update projection matrix across shaders.
    mat4x4_identity(context.projection_matrix);
    mat4x4_ortho(context.projection_matrix, 0.0f, viewport_width, viewport_height, 0.0f, -1.0f, 1.0f);
    glBindBuffer(GL_UNIFORM_BUFFER, context.uniform_buffer);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(mat4x4), context.projection_matrix);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    glUseProgram(context.program_shapes_solid);
    glUniform2f(context.solid_loc_u_res, viewport_width, viewport_height);
    glUseProgram(0);

    context.cached_viewport_width = viewport_width;
    context.cached_viewport_height = viewport_height;
}

static void gle2d_internal_update_rendering_area_and_projection(int viewport_width, int viewport_height)
{
    glViewport(0, 0, viewport_width, viewport_height);
    mat4x4_identity(context.projection_matrix);
    mat4x4_ortho(context.projection_matrix, 0.0f, viewport_width, 0.0f, viewport_height, -1.0f, 1.0f);
    glBindBuffer(GL_UNIFORM_BUFFER, context.uniform_buffer);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(mat4x4), context.projection_matrix);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void gle2d_shutdown(void)
{
    glUseProgram(0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    glDeleteProgram(context.program_shapes_solid);
    glDeleteProgram(context.program_shapes_textured);
    glDeleteProgram(context.program_font);
    glDeleteBuffers(1, &context.uniform_buffer);
    glDeleteBuffers(1, &context.font_quad_vbo);
    glDeleteBuffers(1, &context.shapes_quad_vbo);
    glDeleteBuffers(1, &context.shapes_quad_ebo);
    glDeleteVertexArrays(1, &context.font_quad_vao);
    glDeleteVertexArrays(1, &context.shapes_quad_vao);
    glDeleteFramebuffers(1, &context.font_fbo);
    gle2d_texture_delete(context.font_dynamic_texutre);
    free(context.font_batch_buffer);
}

///////////
// Shapes

void gle2d_update_time_uniform(float time)
{
    context.time = time;
}

void gle2d_shapes_draw_circle(float x, float y, float radius, vec4 color)
{
    (void)x;
    (void)y;
    (void)radius;
    (void)color;
    return;
}

void gle2d_shapes_draw_glpoint(float x, float y, float size, vec4 color)
{
    mat4x4_identity(context.model_matrix);
    mat4x4_translate_in_place(context.model_matrix, x, y, 1.0f);
    
    glUseProgram(context.program_shapes_solid);
    glUniformMatrix4fv(context.solid_loc_u_model, 1, GL_FALSE, &context.model_matrix[0][0]);
    glUniform4fv(context.solid_loc_u_color, 1, color);

    glBindVertexArray(context.shapes_quad_vao);
    glPointSize(size);
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_POINTS, 0, 1); // Draw just the first vertex as a point
    glBindVertexArray(0);
    glPointSize(1.0f);
}

void gle2d_shapes_draw_quad(float x, float y, float w, float h, float rotation, vec4 color, GLuint texture)
{       
    mat4x4_identity(context.model_matrix);

    mat4x4 Translate, Rotate, Scale;
    mat4x4_translate(Translate, x+w/2, y+h/2, 1.0f);
    mat4x4_rotate_Z(Rotate, context.model_matrix, rotation);
    mat4x4_scale_aniso(Scale, context.model_matrix, w, h, 1.0f);

    mat4x4_mul(context.model_matrix, Rotate, Scale);
    mat4x4_mul(context.model_matrix, Translate, context.model_matrix);

    int use_texture = texture > 0 ? 1 : 0;
    if (use_texture) {
        glUseProgram(context.program_shapes_textured);
        glUniform4fv(context.textured_loc_u_color, 1, color);
        glUniformMatrix4fv(context.textured_loc_u_model, 1, GL_FALSE, &context.model_matrix[0][0]);
    } else {
        glUseProgram(context.program_shapes_solid);
        glUniform4fv(context.solid_loc_u_color, 1, color);
        glUniform1f(context.solid_loc_u_time, context.time);
        glUniformMatrix4fv(context.solid_loc_u_model, 1, GL_FALSE, &context.model_matrix[0][0]);
    }

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(context.shapes_quad_vao);
    if (use_texture) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
    }
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

//////////
// FONT

int gle2d_font_create(GLE2D_Font *font, const char *path, float px_size)
{
    assert(font && path);
    assert(px_size > 0);

    // Function specific //
    int result = 1;

    stbtt_fontinfo font_info;
    stbtt_pack_context spc;
    unsigned char *bitmap = NULL;
    //-------------------//

    // GLE2D_Font //
    stbtt_packedchar *packed_char_array = NULL;
    unsigned char *ttf_data = NULL;
    GLuint atlas = 0;
    int num_char = GLE2D_FONT_LAST_CHAR;
    int ascent = 0;
    int descent = 0;
    int linegap = 0;
    float scale = 0.0f;
    //------------// 

    packed_char_array = malloc(sizeof(*packed_char_array) * num_char);
    if (!packed_char_array) {
        goto cleanup;
    }

    ttf_data = gle2d_internal_font_load_into_memory(path);
    if (!ttf_data) {
        goto cleanup;
    }

    if (!stbtt_InitFont(&font_info, ttf_data, stbtt_GetFontOffsetForIndex(ttf_data, 0))) {
        goto cleanup;
    }
    stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &linegap);
    scale = stbtt_ScaleForPixelHeight(&font_info, px_size);

    bitmap = malloc(GLE2D_FONT_ATLAS_SIZE * GLE2D_FONT_ATLAS_SIZE);
    if (!bitmap) {
        goto cleanup;
    }
    memset(bitmap, 0, GLE2D_FONT_ATLAS_SIZE * GLE2D_FONT_ATLAS_SIZE); // set to black

    if (!stbtt_PackBegin(&spc, bitmap, GLE2D_FONT_ATLAS_SIZE, GLE2D_FONT_ATLAS_SIZE, 0, 1, NULL)) {
        goto cleanup;
    }
    if (!stbtt_PackFontRange(&spc, ttf_data, 0, px_size, GLE2D_FONT_FIRST_CHAR, GLE2D_FONT_LAST_CHAR, packed_char_array)) {
        goto cleanup;
    }
    stbtt_PackEnd(&spc);

    glGenTextures(1, &atlas);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, GLE2D_FONT_ATLAS_SIZE, GLE2D_FONT_ATLAS_SIZE, 0, GL_RED, GL_UNSIGNED_BYTE, bitmap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    font->packed_char_array = packed_char_array;
    font->ttf_data = ttf_data;
    font->atlas = atlas; 
    font->num_chars = num_char;
    font->ascent = ascent;
    font->descent = descent;
    font->linegap = linegap;
    font->px_size = px_size;
    font->px_scale = scale;
    
    result = 0;

cleanup:
    if (result != 0) {
        free(bitmap);
        free(ttf_data);
        free(packed_char_array);
    } else {
        free(bitmap);
    }
    return result;
}

static void gle2d_internal_font_calculate_text_bounding_box(const GLE2D_Font* font, const char* text, vec2 out)
{
    float w = 0.0f;
    float h = 0.0f;
    if (text[0] == '\0') {
        out[0] = w;
        out[1] = h;
        return;
    }

    float xpos = 0.0f;
    float ypos = 0.0f;

    float leftmost_x = FLT_MAX;
    float rightmost_x = -FLT_MAX;
    float topmost_y = FLT_MAX;
    float bottommost_y = -FLT_MAX; 

    stbtt_aligned_quad q;
    const char* p = text;

    while (*p != '\0') {
        stbtt_GetPackedQuad(font->packed_char_array,
                            GLE2D_FONT_ATLAS_SIZE,
                            GLE2D_FONT_ATLAS_SIZE,
                            *p - GLE2D_FONT_FIRST_CHAR,
                            &xpos, &ypos,
                            &q, 1);

        // Get the total font bounding box.
        if (q.x0 < leftmost_x) leftmost_x = q.x0;
        if (q.x1 > rightmost_x) rightmost_x = q.x1;
        if (q.y0 < topmost_y) topmost_y = q.y0;
        if (q.y1 > bottommost_y) bottommost_y = q.y1;

        p++;
    }

    out[0] = rightmost_x - leftmost_x;
    out[1] = bottommost_y - topmost_y;
}

void gle2d_font_render_text_rotation(const GLE2D_Font* font, const char* text, float x, float y, float rotation, vec4 color)
{
    if (!font || !text || *text == '\0') {
        return;
    }

    vec2 texture_dims;
    gle2d_internal_font_calculate_text_bounding_box(font, text, texture_dims);

    if (texture_dims[0] != context.font_dynamic_texutre.width ||
        texture_dims[1] != context.font_dynamic_texutre.height) {

        glBindTexture(GL_TEXTURE_2D, context.font_dynamic_texutre.id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture_dims[0], texture_dims[1], 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindTexture(GL_TEXTURE_2D, 0);

        context.font_dynamic_texutre.width = texture_dims[0];
        context.font_dynamic_texutre.height = texture_dims[1];

        // Re-attach after resizing.
        glBindFramebuffer(GL_FRAMEBUFFER, context.font_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, context.font_dynamic_texutre.id, 0);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        assert(status == GL_FRAMEBUFFER_COMPLETE);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, context.font_fbo);
    gle2d_internal_update_rendering_area_and_projection(context.font_dynamic_texutre.width, context.font_dynamic_texutre.height);

    // // Clear to transparent
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    // render text to framebuffer texture.      
    gle2d_font_render_text(font, color, text, 0, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gle2d_update_rendering_area(context.cached_viewport_width, context.cached_viewport_height);

    // Draw text as texutred quad.
    gle2d_shapes_draw_quad(x, y, texture_dims[0], texture_dims[1], 
        rotation, (vec4){1.0f, 1.0f, 1.0f, 1.0f}, context.font_dynamic_texutre.id);
}

void gle2d_font_render_text(const GLE2D_Font* font, vec4 color, const char* text, float x, float y)
{
    if (!font || !text || *text == '\0') {
        return;
    }

    glUseProgram(context.program_font);
    glUniform4f(context.font_loc_u_color, color[0], color[1], color[2], color[3]);

    glBindVertexArray(context.font_quad_vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font->atlas);
    glBindBuffer(GL_ARRAY_BUFFER, context.font_quad_vbo);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    size_t text_len = strlen(text);
    assert(text_len <= GLE2D_FONT_MAX_TEXT_LEN);
    if (text_len > GLE2D_FONT_MAX_TEXT_LEN) {
        text_len = GLE2D_FONT_MAX_TEXT_LEN;
    } 

    float xpos = x;
    float ypos = y + font->ascent * font->px_scale;
    int cursor = 0;
    stbtt_aligned_quad q;
    const char* p = text; // walk the string without modifying 'text'
    while (*p != '\0') {                     // strlen gotcha without this XD
        stbtt_GetPackedQuad(font->packed_char_array,
                            GLE2D_FONT_ATLAS_SIZE,
                            GLE2D_FONT_ATLAS_SIZE,
                            *p - GLE2D_FONT_FIRST_CHAR,
                            &xpos, &ypos,
                            &q, 1);

        float char_vertices[] = {
            q.x0, q.y0, q.s0, q.t0,
            q.x0, q.y1, q.s0, q.t1,
            q.x1, q.y1, q.s1, q.t1,
            q.x1, q.y0, q.s1, q.t0,
            q.x0, q.y0, q.s0, q.t0,
            q.x1, q.y1, q.s1, q.t1,
        };

        memcpy(&context.font_batch_buffer[cursor], char_vertices, sizeof(char_vertices));
        cursor += 6 * 4;
        p++;
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, (cursor * sizeof(float)), context.font_batch_buffer);
    glDrawArrays(GL_TRIANGLES, 0, cursor / 4);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

void gle2d_font_destroy(GLE2D_Font* font)
{
    if (!font) return;

    free(font->packed_char_array);
    font->packed_char_array = NULL;

    free(font->ttf_data);
    font->ttf_data = NULL;

    if (font->atlas) {
        glDeleteTextures(1, &font->atlas);
        font->atlas = 0;
    }

    font->num_chars = 0;
}

/////////////
// TEXTURES

GLE2D_Texture gle2d_texture_load(const char* path)
{
    assert(path && *path != '\0');
    GLE2D_Texture texture = {0};

    int width;
    int height;
    int channel_number;
    stbi_set_flip_vertically_on_load(1);                            // TODO ------- Questionable 4??
    unsigned char* data = stbi_load(path, &width, &height, &channel_number, 4);
    stbi_set_flip_vertically_on_load(0);
    if (!data) {
        return texture;
    } 

    glGenTextures(1, &texture.id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);	
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);

    texture.width = width;
    texture.height = height;
    return texture;
}

static GLE2D_Texture gle2d_internal_texture_create_empty_texture_for_font_rendering(int width, int height)
{
    assert(width > 0 && height > 0);
    GLE2D_Texture texture = {0};

    glGenTextures(1, &texture.id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    // Probably good settings for rendering text onto a texture
    // Gotta keep that in mind.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);   
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    texture.width = width;
    texture.height = height;

    return texture;
}


void gle2d_texture_bind(GLE2D_Texture texture)
{
    assert(texture.id != 0);
    glBindTexture(GL_TEXTURE_2D, texture.id);
}

void gle2d_texture_delete(GLE2D_Texture texture)
{
    assert(texture.id != 0);
    glDeleteTextures(1, &texture.id);
}


/////////
// MISC

int gle2d_misc_texture_save_to_disk_as_png(GLuint texture, const char *name)
{
    glBindTexture(GL_TEXTURE_2D, texture);

    GLint width, height, internal_format;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internal_format);

    if (internal_format != GL_RED && internal_format != GL_R8) {
        glBindTexture(GL_TEXTURE_2D, 0);
        return 1; // not 1 byte per pixel.
    }

    unsigned char *pixels = malloc(width * height);
    if (!pixels) {
        glBindTexture(GL_TEXTURE_2D, 0);
        return 1;
    }

    // How OpenGL writes to YOUR buffer when downloading FROM GPU
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_UNSIGNED_BYTE, (void *)pixels);
    glPixelStorei(GL_PACK_ALIGNMENT, 4); // restore to openGL default = 4.

    int result = stbi_write_png(name, width, height, 1, pixels, width);

    glBindTexture(GL_TEXTURE_2D, 0);
    free(pixels);

    return result ? 0 : 1;
}

int gle2d_misc_shader_hot_reload(GLE2D_ShaderType type, const char* vertex_file_path, const char* fragment_file_path)
{
    assert(vertex_file_path && fragment_file_path);

    unsigned char* vert_data = gle2d_internal_load_file_as_string_null_terminate(vertex_file_path);
    unsigned char* frag_data = gle2d_internal_load_file_as_string_null_terminate(fragment_file_path);
    if (!vert_data || !frag_data) {
        free(vert_data);
        free(frag_data);
        return 1;
    }

    GLuint new_program = 0;
    new_program = gle2d_internal_create_shader_from_data((const char*)vert_data, (const char*)frag_data);
    free(vert_data);
    free(frag_data);

    if (new_program == 0) return 1;

    GLuint* target_program = NULL;
    switch(type) 
    {
        case GLE2D_SHADER_SOLID: 
        {
            target_program = &context.program_shapes_solid;
            // reset uniforms locations.
            context.solid_loc_u_color = glGetUniformLocation(new_program, "u_color");
            context.solid_loc_u_model = glGetUniformLocation(new_program, "u_model");
            context.solid_loc_u_res = glGetUniformLocation(new_program, "u_res");
            context.solid_loc_u_time = glGetUniformLocation(new_program, "u_time");

            GLuint new_block_index = glGetUniformBlockIndex(new_program, "u_BlockProjectionMatrix");
            glUniformBlockBinding(new_program, new_block_index, 0);

            glUseProgram(new_program);
            glUniform2f(context.solid_loc_u_res, context.cached_viewport_width,context.cached_viewport_height);
            glUseProgram(0);

        } break;
        case GLE2D_SHADER_TEXTURED: 
        {
            target_program = &context.program_shapes_textured;

            context.textured_loc_u_color = glGetUniformLocation(new_program, "u_color");
            context.textured_loc_u_model = glGetUniformLocation(new_program, "u_model"); 
            context.textured_loc_u_texture = glGetUniformLocation(new_program, "u_texture"); 

            GLuint new_block_index = glGetUniformBlockIndex(new_program, "u_BlockProjectionMatrix");
            glUniformBlockBinding(new_program, new_block_index, 0);

            glUseProgram(new_program);
            glUniform1i(context.textured_loc_u_texture, 0);
            glUseProgram(0);
        } break;
        case GLE2D_SHADER_FONT: 
        {
            target_program = &context.program_font;

            context.font_loc_u_color =  glGetUniformLocation(new_program, "u_color");
            context.font_loc_u_text = glGetUniformLocation(new_program, "u_text");

            GLuint new_block_index = glGetUniformBlockIndex(new_program, "u_BlockProjectionMatrix");
            glUniformBlockBinding(new_program, new_block_index, 0);

            glUseProgram(new_program);
            glUniform1i(context.font_loc_u_text, 0);
            glUseProgram(0);
        } break;
        default:
            glDeleteProgram(new_program);
            return 1;
    }
    
    glDeleteProgram(*target_program);
    *target_program = new_program;

    return 0;
}

/////////////
// Internal

static GLuint gle2d_internal_create_shader_from_data(const char* vertex, const char* fragment)
{
    assert(vertex && fragment);
    GLuint shader_program = 0;

    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vertex_shader, 1, (const GLchar**)&vertex, NULL);
    glShaderSource(fragment_shader, 1, (const GLchar**)&fragment, NULL);
    glCompileShader(vertex_shader);
    glCompileShader(fragment_shader);

    if (gle2d_internal_shader_compile_error(vertex_shader)   > 0 ||
        gle2d_internal_shader_compile_error(fragment_shader) > 0) {

        glDeleteShader(vertex_shader);  
        glDeleteShader(fragment_shader);

        return shader_program;
    }

    shader_program = glCreateProgram();
    glAttachShader(shader_program, vertex_shader);
    glAttachShader(shader_program, fragment_shader);
    glLinkProgram(shader_program);
    glDeleteShader(vertex_shader);  
    glDeleteShader(fragment_shader);

    if (gle2d_internal_shader_program_link_error(shader_program) > 0) {

        glDeleteProgram(shader_program);
        shader_program = 0;
        return shader_program;

    }

    return shader_program;

}

static int gle2d_internal_shader_compile_error(GLuint shader)
{
	GLint success;
	char info_log[1024] = {0};

	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success) {
		glGetShaderInfoLog(shader, sizeof(info_log), NULL, info_log);
        fprintf(stderr, "[GLE2D][SHADER COMPILE] Log: %s", info_log);
		return 1;
	}
	return 0;
}

static int gle2d_internal_shader_program_link_error(GLuint shader)
{
    GLint success;
    char info_log[1024] = {0};

    glGetProgramiv(shader, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shader, sizeof(info_log), NULL, info_log);
        fprintf(stderr, "[GLE2D][SHADER LINK] Log: %s", info_log);
        return 1;
    }
    return 0;
}

static unsigned char* gle2d_internal_load_file_as_string_null_terminate(const char* path)
{
    assert(path);
    unsigned char *data = NULL;
    int result = 1;

    FILE* f = fopen(path, "rb");
    if (!f) {
        goto cleanup;
    }

    fseek(f, 0L, SEEK_END);
    long size = ftell(f);
    fseek(f, 0L, SEEK_SET);
    if (size <= 0) { // empty or error.
        goto cleanup;
    }

    data = malloc(size + 1); // for null termination.
    if (!data) {
        goto cleanup;
    }

    size_t elements_read = fread(data, 1, size, f);
    if (elements_read != (size_t)size) {
        goto cleanup;
    }

    data[size] = '\0';
    result = 0;

cleanup:
    if (f) fclose(f);
    if (result) {
        free(data);
        data = NULL;
    }
    return data;
}

static unsigned char* gle2d_internal_font_load_into_memory(const char *path)
{
    assert(path);
    unsigned char *data = NULL;

    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }

    fseek(f, 0L, SEEK_END);
    long size = ftell(f);
    fseek(f, 0L, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return NULL;
    }

    data = malloc(size);
    if (!data) {
        fclose(f);
        return NULL;
    }

    size_t bytes_read = fread(data, 1, size, f);
    fclose(f);
    if (bytes_read != (size_t)size) { // Should read exactly size bytes
        free(data);
        return NULL;
    }

    return data;
}
