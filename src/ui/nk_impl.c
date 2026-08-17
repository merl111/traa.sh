/* Single translation unit: glad + nuklear + glfw gl3 backend */
#define GLAD_GL_IMPLEMENTATION
#include "glad/gl.h"

#define NK_IMPLEMENTATION
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_KEYSTATE_BASED_INPUT
#include "nuklear/nuklear.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define NK_GLFW_GL3_IMPLEMENTATION
#include "nuklear/nuklear_glfw_gl3.h"
