///////////////////////
// Standard C headers
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <float.h>

//////////////////
// External libs
#define GLFW_INCLUDE_NONE // <- glad/GLFW can be included in any order
#include "glad/glad.h"
#include "GLFW/glfw3.h"

#define MINI_MATH_TYPE_LIN
#ifdef MINI_MATH_TYPE_LIN
#include "linmath/linmath.h"
#else
#include "cglm/call.h"
#endif

// UI
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#include "Nuklear/nuklear.h"

#define NK_GLFW_GL3_IMPLEMENTATION
#include "Nuklear/nuklear_glfw_gl3.h"

// STB
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

// Fonts
#include <ft2build.h>
#include FT_FREETYPE_H  

// wanted to include stb_true_type.h but 
// appratently Nuklear already does it.

///////////////
// My headers
#ifdef DEBUG
#warning "Debug build enabled!"
#endif

// Debug math tools (Linmath only)
#if defined(DEBUG) && defined(MINI_MATH_TYPE_LIN)
#warning "Leaving this for the memorization :D"
#endif

#include "old_shader.h"
#include "draw2d.h"
#include "mini_utils.h"
#include "gl_helpers.h"
#include "math_helpers.h"
#include "data_geometry.h"
#include "mini_types.h"

#include "dungen/dungen.h"
#include "dungen/dice.h"
#include "dun_mesh.h"

///////////////////////////////////////////
//
//  My macros
//
///////////////////////////////////////////

#define UNUSED(var) (void)var
#define MINI_MAX(a,b)  (((a) > (b)) ? (a) : (b))

#define WINDOW_MAX_NAME_LEN 64
#define WINDOW_DEFAULT_WIDTH 1280
#define WINDOW_DEFAULT_HEIGHT 800

#define MAX_VERTEX_BUFFER 512 * 1024
#define MAX_ELEMENT_BUFFER 128 * 1024

///////////////////////////////////////////
//
//  Enums
//
///////////////////////////////////////////

/////////
// Game
typedef enum {
    ACTION_NONE = -1,
    ACTION_MOVE_FORWARD,
    ACTION_MOVE_BACKWARD,
    ACTION_MOVE_LEFT,
    ACTION_MOVE_RIGHT,
    ACTION_COUNT
} GameAction;

typedef enum {
    NORTH,
    EAST,
    SOUTH,
    WEST,
    NWSE_COUNT,
} Direction;

///////////////////////////////////////////
//
//  Structs
//
///////////////////////////////////////////

///////////
// Engine
typedef struct s_window_state {
    char  name[WINDOW_MAX_NAME_LEN];
    int   width;
    int   height;
    bool  resized;
    bool  is_fullscreen;
} WindowState;

typedef struct s_frame_counter {
    double ms_per_frame;
    double avg_fps;
} FrameCounter;

typedef struct s_user_config {
    bool   should_start_vsync;
    bool   should_start_focused;
} UserConfig;

typedef struct s_settings {
    float mouse_sensitivity;
} Settings;

typedef struct s_ui_state {
    bool ui_active;
} UiState;

typedef struct s_font_character {
    vec2 size;
    vec2 bearing;
    unsigned int advance;
} Character;

typedef struct s_font {
    TextureID atlas;
    char*     name;
    Character glyphs[128];
} Font;

typedef struct s_input_state {
    bool actions[ACTION_COUNT];
} InputState;

typedef struct s_camera {
    mat4x4 view;
    mat4x4 projection;
    vec3  pos;
    vec3  direction;
    vec3  up;
    float yaw;
    float pitch;
    float fov;
    float speed; // should be here??
    float near;  // ?? (i'll leave it for now)
    float far;   // ??
} Camera;

///////////////////////////////////////////
//
//  Globals
//
///////////////////////////////////////////

static WindowState g_window_state = {
    .width   = WINDOW_DEFAULT_WIDTH,
    .height  = WINDOW_DEFAULT_HEIGHT,
    .name    = "mini 3D",
    .resized = false, //
    .is_fullscreen = true,
};

static UserConfig g_user_config = {
    .should_start_vsync         = true,
    .should_start_focused       = true,
};

static FrameCounter g_frame_counter = {
    .ms_per_frame = 0.0,
    .avg_fps      = 0.0,
};

static Settings g_settings = {
    .mouse_sensitivity = 0.05f,
};

static UiState g_ui_state = {
    .ui_active = false,
};

static Font g_default_font = {
    .atlas  = 0,
    .name   = "",
    .glyphs = {0},
};

static Camera g_camera = {
    .view       = MAT4x4_IDENTITY,
    .projection = MAT4x4_IDENTITY,
    .pos        = (vec3){0.0f, 0.0f, 10.0f},
    .direction  = (vec3){0.0f, 0.0f, -1.0f},
    .up         = (vec3){0.0f, 1.0f, 0.0f},
    .yaw        = -90.0f, // start facing north.
    .pitch      = 0.0f,
    .fov        = 60.0f,
    .speed      = 10.0f,
    .near       = 0.1f, 
    .far        = 100.0f,
};

//////////
// Input
static InputState g_input_state = {
    .actions = {false},
};
                                                // off by one
static GameAction g_input_key_to_action[GLFW_KEY_LAST + 1] = {ACTION_NONE};

////////////////////////
// Cardinal Directions
static const vec3 DIR_FORWARD  = {0.0f, 0.0f, -1.0f}; //north (-z)
static const vec3 DIR_BACKWARD = {0.0f, 0.0f, 1.0f};  //south (+z)
static const vec3 DIR_RIGHT    = {1.0f, 0.0f, 0.0f};  //east (+x)
static const vec3 DIR_LEFT     = {-1.0f, 0.0f, 0.0f}; //west (-x)

///////////////////////////////////////////
//
//  Functionality hopefully XD
//
///////////////////////////////////////////

static void
input_init_keybindings(void)          
{   // Need to do this cus the initialization in global scope is shit.
    for (int i = 0; i <= GLFW_KEY_LAST; i++) // (only first element was
        g_input_key_to_action[i] = ACTION_NONE; // initialized to -1 XD)

    g_input_key_to_action[GLFW_KEY_W] = ACTION_MOVE_FORWARD;
    g_input_key_to_action[GLFW_KEY_S] = ACTION_MOVE_BACKWARD;
    g_input_key_to_action[GLFW_KEY_A] = ACTION_MOVE_LEFT;
    g_input_key_to_action[GLFW_KEY_D] = ACTION_MOVE_RIGHT;

    g_input_key_to_action[GLFW_KEY_UP] = ACTION_MOVE_FORWARD;
    g_input_key_to_action[GLFW_KEY_DOWN] = ACTION_MOVE_BACKWARD;
    g_input_key_to_action[GLFW_KEY_LEFT] = ACTION_MOVE_LEFT;
    g_input_key_to_action[GLFW_KEY_RIGHT] = ACTION_MOVE_RIGHT;
}

static void
mini_print_camera(Camera* camera)
{
    fprintf(stdout, "--- CAMERA ---\n");
    fprintf(stdout, "POS: ");
    mini_math_print_vec3(camera->pos);
    fprintf(stdout, "DIR: ");
    mini_math_print_vec3(camera->direction);
    fprintf(stdout, "UP: ");
    mini_math_print_vec3(camera->up);
    fprintf(stdout, "FOV: %f\n", camera->fov);
    fprintf(stdout, "SPEED: %f\n", camera->speed);
    fprintf(stdout, "--------------\n");
    fflush(stdout);
}

static void
input_process(GLFWwindow* window)
// TODO(eter): add HELD, RELEASED!
{
    // reset input state
    for (int i = 0; i < ACTION_COUNT; i++)
        g_input_state.actions[i] = false;

    for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; key++) {
        if (glfwGetKey(window, key) == GLFW_PRESS) {
            GameAction action = g_input_key_to_action[key];
            if (action != ACTION_NONE) {
                g_input_state.actions[action] = true;
            }
        }
    }
}

static void
camera_apply_movement(vec3 direction, double dt)
{
    vec3 final = {0};
    vec3_scale(final, direction, g_camera.speed * dt);
    vec3_add(g_camera.pos, g_camera.pos, final);
}

static void
camera_get_right_vector_norm(vec3 r)
{
    vec3_mul_cross(r, g_camera.direction, g_camera.up);
    vec3_norm(r, r);
}

static void
mini_update_camera_movement(double dt)
// NOTE(mdether7): Right now this function runs every frame. 
{
    vec3 movement_direction = {0};

    if (g_input_state.actions[ACTION_MOVE_FORWARD]) {
        vec3_add(movement_direction, movement_direction, g_camera.direction);
    }
    if (g_input_state.actions[ACTION_MOVE_BACKWARD]) {
        vec3_sub(movement_direction, movement_direction, g_camera.direction);
    }

    if (g_input_state.actions[ACTION_MOVE_LEFT] || g_input_state.actions[ACTION_MOVE_RIGHT]) {
        vec3 right;
        camera_get_right_vector_norm(right);

        if (g_input_state.actions[ACTION_MOVE_LEFT]) {
            vec3_sub(movement_direction, movement_direction, right);
        }
        if (g_input_state.actions[ACTION_MOVE_RIGHT]) {
            vec3_add(movement_direction, movement_direction, right);
        }
    }

    if (vec3_len(movement_direction) <= FLT_EPSILON) 
        return;

    vec3 normalized_movement = {0};
    vec3_norm(normalized_movement, movement_direction);
    camera_apply_movement(normalized_movement, dt);   
}

static void
camera_update_view_matrix(Camera* cam)
{
    vec3 center = {0};
    // we need to add to put the direction of the camera at
    vec3_add(center, cam->pos, cam->direction); // its current position
    mat4x4_look_at(cam->view, cam->pos, center, cam->up);
}

static void
camera_update_projection_matrix(Camera* cam)
{
    mat4x4_perspective(cam->projection, // should be here??
                       mini_degrees_to_radians(cam->fov),
                       ((float)g_window_state.width / (float)g_window_state.height),
                       cam->near,
                       cam->far);
}

static void
mini_update_framecounter(FrameCounter* counter, double ms_per_frame)
{   
    counter->ms_per_frame = ms_per_frame;
    counter->avg_fps = 1000.0 / ms_per_frame;
}

static Direction
camera_get_facing_direction(Camera* cam)
// Doting with 2D vector ignoring Y.
{
    float dot_products[NWSE_COUNT];

    vec2 dir2d = { cam->direction[0], cam->direction[2] };
    dot_products[NORTH] = vec2_dot(dir2d, (vec2){DIR_FORWARD[0], DIR_FORWARD[2]});
    dot_products[EAST]  = vec2_dot(dir2d, (vec2){DIR_RIGHT[0], DIR_RIGHT[2]});
    dot_products[SOUTH] = vec2_dot(dir2d, (vec2){DIR_BACKWARD[0], DIR_BACKWARD[2]});
    dot_products[WEST]  = vec2_dot(dir2d, (vec2){DIR_LEFT[0], DIR_LEFT[2]}); 

    // assume it's the first one.
    Direction max_dir = NORTH;
    float max_val = dot_products[NORTH];

    for (size_t i = 1; i < NWSE_COUNT; i++)
        if (dot_products[i] > max_val) {
            max_val = dot_products[i];
            max_dir = (Direction)i;
        }

    return max_dir; 
}

static void
render_text(Font* font, ProgramSlot slot, const char* text, float x, float y);

//////////
// Utils

static const char*
util_get_direction_as_string(Direction dir)
{
    assert(dir >= NORTH && dir < NWSE_COUNT);
    static const char* names[] = {"NORTH", "EAST", "SOUTH", "WEST"};
    return names[dir];
}

///////////////////////////////////////////
//
//  GLFW callbacks
//
///////////////////////////////////////////

static void error_callback(int error, const char* description)
{
    fprintf(stderr, "Error: %s\n", description);
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) 
{ 
    glViewport(0, 0, width, height);
    g_window_state.width   = width;
    g_window_state.height  = height;
    renderer2d_update_ortho(width, height);
    g_window_state.resized = true;
    // those width and height are the framebuffer sizes not window ones
}

static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos)
{
    //TODO: Move it to polling/while loop.
    static float xlast = 0;
    static float ylast = 0;
    static bool first_mouse = true;
    
    if (first_mouse) {
        xlast = xpos;
        ylast = ypos;
        first_mouse = false;
        return;
    }

    float xoffset = xpos - xlast; // calculate delta
    float yoffset = ylast - ypos; // same here, inverted!
    xlast = xpos;
    ylast = ypos;

    xoffset *= g_settings.mouse_sensitivity;
    yoffset *= g_settings.mouse_sensitivity;

    // g_camera.pitch = asin(-d.Y); // direction to pitch/yaw?
    // g_camera.yaw = atan2(d.X, d.Z)

    g_camera.yaw += xoffset;
    g_camera.pitch += yoffset;

    if(g_camera.pitch > 89.0f)
        g_camera.pitch = 89.0f;
    if(g_camera.pitch < -89.0f)
        g_camera.pitch = -89.0f;

    vec3 direction;
    direction[0] = cos(mini_degrees_to_radians(g_camera.yaw)) * cos(mini_degrees_to_radians(g_camera.pitch));
    direction[1] = sin(mini_degrees_to_radians(g_camera.pitch));
    direction[2] = sin(mini_degrees_to_radians(g_camera.yaw)) * cos(mini_degrees_to_radians(g_camera.pitch));
    vec3_norm(direction, direction);
    vec3_dup(g_camera.direction, direction);    
}


static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    // Thats crazy useful! // 
    // int* my_int = (int*)glfwGetWindowUserPointer(window);
    if (g_ui_state.ui_active)
        nk_glfw3_key_callback(window, key, scancode, action, mods);

   // if (nk_item_is_any_active())

    if (key == GLFW_KEY_Q && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GLFW_TRUE);

    if (key == GLFW_KEY_F2 && action == GLFW_PRESS) {
        static bool wireframe = false;
        wireframe = !wireframe;
        glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
    }

    if (key == GLFW_KEY_F1 && action == GLFW_PRESS) {
        g_ui_state.ui_active = !g_ui_state.ui_active;
    }

    // TODO: Add somekind of menu after pressing escape.
// #if 1
//     if (key == GLFW_KEY_U && action == GLFW_PRESS)
//         glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
// #endif

    if (key == GLFW_KEY_R && action == GLFW_PRESS) {
        bool result = false;
        const char* message;
        result = shader_program_hot_reload(&g_shader_programs[PROGRAM_SLOT_1].handle,
            "shaders/quad.vert", "shaders/quad.frag");
        if (result)
            shader_init_uniforms(&g_shader_programs[PROGRAM_SLOT_1]);
        // variable = condition ? value_if_true : value_if_false;
        message = result ? "[SHADER RELOAD SUCCESS!]" : "[SHADER RELOAD FAILED!]";
        util_print_n_flush("%s\n", message);
    }
    
}

///////////////////////////////////////////
//
//  Main finally.
//
///////////////////////////////////////////

int main(int argc, char* argv[])
{
    UNUSED(argc);
    UNUSED(argv);

    GLFWwindow*  window = NULL;

    /* GLFW */
#ifdef __linux__
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
#endif
    glfwSetErrorCallback(error_callback);
    if (!glfwInit()) {
        const char* error;
        glfwGetError(&error);
        mini_die("[GLFW] %s", error);
    }
    
    /* Window creation hints */
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    if (g_window_state.is_fullscreen) {
        glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    }
    if (g_user_config.should_start_focused) {
        glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    } 

#ifdef ENABLE_GL_DEBUG_OUTPUT
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

    /* create window */
    window = glfwCreateWindow(g_window_state.width, g_window_state.height, g_window_state.name, NULL, NULL);
    if (window == NULL) { 
        const char* error;
        glfwGetError(&error);
        glfwTerminate();
        mini_die("[GLFW] %s", error);
    }

    /* Make this window the gl context */
    glfwMakeContextCurrent(window);
    g_user_config.should_start_vsync ? glfwSwapInterval(1) : glfwSwapInterval(0);

    /* GLFW callbacks setup */
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    
    /* Game/Engine specific input settings. (not sure if it is in good place tho)*/
    input_init_keybindings();
    // For motion based camera controls
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported()) // <= raw motion is better for controlling
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE); //  a 3D camera.

    /* openGL */
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        glfwTerminate();
        mini_die("[GLAD] Failed to load openGL functions!");
    }

    {
        /* Set initial viewport */
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
    }

#ifdef ENABLE_GL_DEBUG_OUTPUT
    {
        int flags;
        glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
        if (flags & GL_CONTEXT_FLAG_DEBUG_BIT)
        {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(gl_debug_output_callback, NULL);
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE,
                 GL_DONT_CARE, 0, NULL, GL_TRUE);
            util_print_n_flush("[GL] Debug output enabled");
        }
    }

    misc_gl_display_information();
#endif

    /* Load OpenGL Stuff */

    // ------------------------- SHADERS ------------------------- //
    GLuint default_program = shader_program_compile("shaders/default.vert",
                                                  "shaders/default.frag"); 
    GLuint quad_program = shader_program_compile("shaders/quad.vert",
                                                "shaders/quad.frag");

    GLuint draw2d_program = shader_program_compile("shaders/draw2d.vert",
                                                "shaders/draw2d.frag");
                                                
    GLuint draw2d_program2 = shader_program_compile("shaders/draw2dtexture.vert",
                                                "shaders/draw2dtexture.frag");

    GLuint renderer2d_program = shader_program_compile("shaders/renderer2d.vert",
                                                "shaders/renderer2d.frag");


    if (default_program == 0 ||
        quad_program == 0 ||
        draw2d_program == 0 ||
        draw2d_program2 == 0 ||
        renderer2d_program == 0) 
    {
        glfwTerminate();
        mini_die("[GL] Shader compilation failed!");
    } 

    g_shader_programs[PROGRAM_SLOT_0].handle = default_program; 
    g_shader_programs[PROGRAM_SLOT_0].name   = "default";

    g_shader_programs[PROGRAM_SLOT_1].handle = quad_program;
    g_shader_programs[PROGRAM_SLOT_1].name   = "quad";

    g_shader_programs[PROGRAM_SLOT_2].handle = draw2d_program;
    g_shader_programs[PROGRAM_SLOT_2].name   = "draw2d";

    g_shader_programs[PROGRAM_SLOT_3].handle = draw2d_program2;
    g_shader_programs[PROGRAM_SLOT_3].name   = "draw2dtexture";

    shader_init_uniforms(&g_shader_programs[PROGRAM_SLOT_0]);
    shader_init_uniforms(&g_shader_programs[PROGRAM_SLOT_1]);
    shader_init_uniforms(&g_shader_programs[PROGRAM_SLOT_2]);
    shader_init_uniforms(&g_shader_programs[PROGRAM_SLOT_3]);

    // -------------------------FONT------------------------------ //
    FT_Library ft; // initialize free type.
    if (FT_Init_FreeType(&ft)) {
        glfwTerminate();
        mini_die("[FT] Could not initialize free type!");
    }

    // load font.
    FT_Face face; {
        const char* path = "fonts/ligurino.ttf";
        if (FT_New_Face(ft, path, 0, &face)) {
            glfwTerminate();
            mini_die("[FT] Failed loading font form: %s", path);
        } }
        
    FT_Set_Pixel_Sizes(face, 0, 48);

    int font_spacing = 2;
    // calculate max width/height for font texture atlas.
    unsigned int font_atlas_w = 0;
    unsigned int font_atlas_h = 0;
    for (unsigned char c = 0; c < 128; c++) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
            util_print_n_flush("Failed to load glyph! %c", c);
            continue;
        }
        font_atlas_w += face->glyph->bitmap.width + font_spacing;
        font_atlas_h = MINI_MAX(font_atlas_h, face->glyph->bitmap.rows);
    }

    // create empty texture(NULL) to store all the glyphs
    GLuint font_atlas;
    glGenTextures(1, &font_atlas);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font_atlas);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, font_atlas_w, font_atlas_h, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    g_default_font.atlas = font_atlas;
    g_default_font.name  = face->family_name;

    // How OpenGL interprets YOUR buffer when uploading TO GPU
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

   // Character glyphs[128]; 

    // populate the atlass with letters.
    {
        int x_pos = 0;
        for (unsigned char c = 0; c < 128; c++) {
            if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
                util_print_n_flush("Failed to load glyph! %c", c);
                continue;
            }

            glTexSubImage2D(GL_TEXTURE_2D,
                            0,
                            x_pos,
                            0,
                            face->glyph->bitmap.width,
                            face->glyph->bitmap.rows,
                            GL_RED,
                            GL_UNSIGNED_BYTE,
                            face->glyph->bitmap.buffer);
            x_pos += face->glyph->bitmap.width + font_spacing;

            g_default_font.glyphs[c].size[0] =    face->glyph->bitmap.width;
            g_default_font.glyphs[c].size[1] =    face->glyph->bitmap.rows;
            g_default_font.glyphs[c].bearing[0] = face->glyph->bitmap_left;
            g_default_font.glyphs[c].bearing[1] = face->glyph->bitmap_top;
            g_default_font.glyphs[c].advance =    face->glyph->advance.x;
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4); // restore to openGL default.   
    }

    // Just to be sure theres no mismatch.
    {GLint width, height; // Dunno there might.
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    assert((GLint)font_atlas_w == width && (GLint)font_atlas_h == height);}

    FT_Done_Face(face);
    FT_Done_FreeType(ft);  
    

    if (util_texture_save_to_disk_as_png(font_atlas, "hehefile.png"))
        util_print_n_flush(TERMINAL_RED "Failed when saving texuture to disk!\n" TERMINAL_RESET);


    // -------------------------TEXTURE-------------------------- //
    /**
     * Texture coordinates UVs
     * (0,1)    (1,1)      U = x-axis
     *                     V = y-axis
     * (0,0)    (1,0)
     */
    int tex_width, tex_height, channel_number;
    //stbi_set_flip_vertically_on_load(true); for now comment out
    unsigned char* texture_data = stbi_load("textures/mini_dirt.png", &tex_width, 
                                                &tex_height, &channel_number, 4);
    if (texture_data == NULL) {                               // 4 to force RGBA
        glfwTerminate();
        mini_die("[STB IMAGE] Failed loading a texture! Reason: %s", stbi_failure_reason());
    }

    GLuint texture;
    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0); // Select slot 0
    glBindTexture(GL_TEXTURE_2D, texture); // Plug texture into slot 0

    // set the texture wrapping/filtering options 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);	
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
//   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
//   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex_width, tex_height, 0, 
                                GL_RGBA, GL_UNSIGNED_BYTE, texture_data);
    glGenerateMipmap(GL_TEXTURE_2D);

    // finish texture work
    // Unbinding in the render loop is usually pointless.
    glBindTexture(GL_TEXTURE_2D, 0); // here it's fine.
    stbi_image_free(texture_data);

    // plug in the texture locations (TODO: maybe do it in the init YES)
    glUseProgram(g_shader_programs[PROGRAM_SLOT_0].handle);
    GLint dirt_loc = glGetUniformLocation(g_shader_programs[PROGRAM_SLOT_0].handle, "dirt_texture");
    if (dirt_loc == -1)
        util_print_n_flush("Warning location not found!");
    glUniform1i(dirt_loc, 0); // <= It's mapping shader samplers to OpenGL texture units.

    //--------------------------GEOMETRY-------------------------------------//
    // Cube (EBO approach)
    GLuint cube_VAO;
    glGenVertexArrays(1, &cube_VAO);
    glBindVertexArray(cube_VAO);

    GLuint cube_VBO;
    glGenBuffers(1, &cube_VBO);
    glBindBuffer(GL_ARRAY_BUFFER, cube_VBO);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(geo_cube_vertices), geo_cube_vertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0); // Position
    glEnableVertexAttribArray(1); // Normals
    glEnableVertexAttribArray(2); // UVs

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)0); // IN BYTES!
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)(3 * sizeof(GLfloat)));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), (void*)(6 * sizeof(GLfloat)));

    GLuint cube_EBO;
    glGenBuffers(1, &cube_EBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cube_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)sizeof(geo_cube_indices), geo_cube_indices, GL_STATIC_DRAW);

    // Fullscreen EboQuad
    GLuint quad_VAO;
    glGenVertexArrays(1, &quad_VAO);
    glBindVertexArray(quad_VAO);

    GLuint quad_VBO;
    glGenBuffers(1, &quad_VBO);
    glBindBuffer(GL_ARRAY_BUFFER, quad_VBO);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(geo_quad_vertices), geo_quad_vertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0); // only enable position.
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), (void*)0);

    GLuint quad_EBO;
    glGenBuffers(1, &quad_EBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quad_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)sizeof(geo_quad_indices), geo_quad_indices, GL_STATIC_DRAW);

    /*----------------------------NK UI SETUP--------------------------------*/
    struct nk_glfw ui_render_ctx = {0};
    struct nk_context *ui_ctx;

    ui_ctx = nk_glfw3_init(&ui_render_ctx, window, NK_GLFW3_DEFAULT);
    {struct nk_font_atlas *atlas;
        nk_glfw3_font_stash_begin(&ui_render_ctx, &atlas); // add custom font here maybe?
        nk_glfw3_font_stash_end(&ui_render_ctx);}
    /*--------------------------------------------------------------------*/

    /* Game/Engine specific initialization */
    renderer2d_init(renderer2d_program, g_window_state.width, g_window_state.height);
    draw2d_init();
    draw2d_set_program(PROGRAM_SLOT_3);

    // some cube test movement.
    mat4x4 model_cube;
    mat4x4_identity(model_cube);

    mat4x4_translate_in_place(model_cube, 0.0f, 0.0f, 0.0f);
    mat4x4_scale_aniso(model_cube, model_cube, 1.0f, 1.0f, 1.0f);

    // Dungen test.
#if 0
    char state[256];
    dice_init_state(state);
#endif
    dungeon_generate();
    for (int i = 0; i < DUN_SIZE; i++) {
        for (int j = 0; j <DUN_SIZE; j++) {
            printf("%c", get_terrain_char(g_dungeon[i][j]));
        }
        putchar('\n');
    }
    fflush(stdout);

    DungeonMesh* mesh = dungeon_generate_mesh(g_dungeon);
    if (mesh == NULL) {
        glfwTerminate();
        mini_die("Dungeon mesh creation failed!");
    }

    // IMPORTANT!
    // Projection needs to be updated at least once before start
    camera_update_projection_matrix(&g_camera);

    // Fucking testing here, nothing to look at /////
    /////////////////////////////////////////////////

    // some ortographic magic.
    // float aspect = (float)g_window_state.width / g_window_state.height;
    // float half_height = g_window_state.height / 2.0f;
    // float half_width  = half_height * aspect;   
    // util_print_n_flush("%f", aspect);

    /* OpenGL initial options setup */
    glClearColor(0.21f, 0.72f, 0.43f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glLineWidth(2.0f); // for wireframe

    glfwSetTime(0.0);
    float last_time = glfwGetTime();
    float previous_time = glfwGetTime();
    unsigned int frames = 0;
    float delta_time = 0.0f; // TODO: Do something else than delta time (Ticks?) 

    while (!glfwWindowShouldClose(window))
    { /*/.LOOP*/
        /* Start frame */
        float current_time = glfwGetTime();
        delta_time = current_time - previous_time;
        previous_time = current_time;

        /* Input */
        input_process(window);
        nk_glfw3_new_frame(&ui_render_ctx); // new

        /* Update Game logic kinda? */
        mini_update_camera_movement(delta_time);
        // Update Matrices
        camera_update_view_matrix(&g_camera);

        // Update UI logic
        if (g_ui_state.ui_active) {
            if (nk_begin(ui_ctx, "Demo", nk_rect(50, 50, 230, 250),
                NK_WINDOW_BORDER|NK_WINDOW_MOVABLE|NK_WINDOW_SCALABLE|
                NK_WINDOW_MINIMIZABLE|NK_WINDOW_TITLE))
            {
                enum {EASY, HARD};
                static int op = EASY;
                static int property = 20;
                nk_layout_row_static(ui_ctx, 30, 80, 1);
                if (nk_button_label(ui_ctx, "button"))
                    mini_die("DIE!");

                nk_layout_row_dynamic(ui_ctx, 30, 2);
                if (nk_option_label(ui_ctx, "easy", op == EASY)) op = EASY;
                if (nk_option_label(ui_ctx, "hard", op == HARD)) op = HARD;

                nk_layout_row_dynamic(ui_ctx, 25, 1);
                nk_property_int(ui_ctx, "Compression:", 0, &property, 100, 10, 1);
            }
            nk_end(ui_ctx);
        }

        // check for resizes
        if (g_window_state.resized) {
            camera_update_projection_matrix(&g_camera);
            util_print_n_flush("W: %d, H: %d", g_window_state.width,
                                               g_window_state.height);
            g_window_state.resized = false;
        }
#if 0
        {
            Direction dir = camera_get_facing_direction(&g_camera);
            printf("[FACING:%s]\n", util_get_direction_as_string(dir));
        }
#endif
        /* Render */
        glEnable(GL_DEPTH_TEST); //Nuklear render modifies this
        glEnable(GL_CULL_FACE);  //thats why i set this up again.
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shader_use_program(PROGRAM_SLOT_0);

        // upload glfw time
        glUniform1f(g_shader_programs[PROGRAM_SLOT_0].u_locations[UNIFORM_TIME], current_time);

        // upload view and projection matrix (camera)
        glUniformMatrix4fv(g_shader_programs[PROGRAM_SLOT_0].u_locations[UNIFORM_VIEW], 1, GL_FALSE, &g_camera.view[0][0]);
        glUniformMatrix4fv(g_shader_programs[PROGRAM_SLOT_0].u_locations[UNIFORM_PROJECTION], 1, GL_FALSE, &g_camera.projection[0][0]);

        // set cube model
        glUniformMatrix4fv(g_shader_programs[PROGRAM_SLOT_0].u_locations[UNIFORM_MODEL], 1, GL_FALSE, &model_cube[0][0]);
        // bind Texture              //texture
        glBindTexture(GL_TEXTURE_2D, texture);
        glBindVertexArray(cube_VAO); 
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);

        glBindVertexArray(mesh->VAO); // <- Dungeon
        glDrawElements(GL_TRIANGLES, mesh->indices_count, GL_UNSIGNED_INT, 0);
        // unbind Texture
        glBindTexture(GL_TEXTURE_2D, 0);

        /**
         * NOTE: 
         * Use glProgramUniform to send the data and 
         * specified program doesnt need to be bound
         */

        shader_use_program(PROGRAM_SLOT_2);
        glUniform2f(g_shader_programs[PROGRAM_SLOT_2].u_locations[UNIFORM_RESOLUTION], 
            (GLfloat)g_window_state.width, (GLfloat)g_window_state.height);
        float color[4];
        color[0] = 0.0f;
        color[1] = 1.0f;
        color[2] = 0.0f;
        color[3] = 1.0f;
        draw2d_quad(10.0f, 10.0f, 1000.0f, 20.0f, color);

        renderer2d_draw_quad_textured(font_atlas, 0, 0, g_window_state.width, 200);


#if 0 /* DRAW FULL SCREEN QUAD */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        shader_use_program(PROGRAM_SLOT_1);

        // upload uniforms
        glUniform1f(g_shader_programs[PROGRAM_SLOT_1].u_locations[UNIFORM_TIME], current_time);
        glUniform2i(g_shader_programs[PROGRAM_SLOT_1].u_locations[UNIFORM_RESOLUTION], 
                (GLint)g_window_state.width, (GLint)g_window_state.height);

        glBindVertexArray(quad_VAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        glDisable(GL_BLEND);
#endif
        // on top of everything render UI.
        if (g_ui_state.ui_active)
            nk_glfw3_render(&ui_render_ctx, NK_ANTI_ALIASING_ON, MAX_VERTEX_BUFFER, MAX_ELEMENT_BUFFER);
       
        /* Present frame */
        glfwSwapBuffers(window);
        glfwPollEvents();

        /* Frame Counter */
        frames++;
        if ( current_time - last_time >= 1.0) {
            mini_update_framecounter(&g_frame_counter, 1000.0f/(float)frames);
            util_print_n_flush("[FPS COUNTER: %.2f ms/frame | %.2f FPS]", 
                g_frame_counter.ms_per_frame, g_frame_counter.avg_fps);
            util_print_n_flush("[WINDOW SIZE: W = %d | H = %d]",
                g_window_state.width, g_window_state.height);
            frames = 0;
            last_time += 1.0;
            mini_print_camera(&g_camera); // DEBUG ONLY
        }
    }

    draw2d_cleanup();
    renderer2d_cleanup();
    dungeon_free_mesh(mesh);
    mesh = NULL;

    /* OpenGL objects cleanup */
    // shader programs
    shader_delete_program(PROGRAM_SLOT_0);
    shader_delete_program(PROGRAM_SLOT_1);
    shader_delete_program(PROGRAM_SLOT_2);

    // textures
    glDeleteTextures(1, &texture);

    glDeleteVertexArrays(1, &cube_VAO);
    glDeleteBuffers(1, &cube_VBO);
    glDeleteBuffers(1, &cube_EBO);

    glDeleteVertexArrays(1, &quad_VAO);
    glDeleteBuffers(1, &quad_VBO);
    glDeleteBuffers(1, &quad_EBO);

    nk_glfw3_shutdown(&ui_render_ctx);
    // no need to destroy technically.
    glfwDestroyWindow(window);

    glfwTerminate();
    return EXIT_SUCCESS;
}
