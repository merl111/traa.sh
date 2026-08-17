#ifndef TRAASH_GL_LOADER_H
#define TRAASH_GL_LOADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool traash_gl_load(void);

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef float GLfloat;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef void GLvoid;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLES 0x0004
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_RED 0x1903
#define GL_R8 0x8229
#define GL_BLEND 0x0BE2
#define GL_MULTISAMPLE 0x809D
#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_UNPACK_ALIGNMENT 0x0CF5

extern void (*traash_glClear)(GLuint);
extern void (*traash_glClearColor)(float, float, float, float);
extern void (*traash_glViewport)(GLint, GLint, GLsizei, GLsizei);
extern void (*traash_glEnable)(GLenum);
extern void (*traash_glBlendFunc)(GLenum, GLenum);
extern void (*traash_glBlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
extern GLuint (*traash_glCreateShader)(GLenum);
extern void (*traash_glShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
extern void (*traash_glCompileShader)(GLuint);
extern void (*traash_glGetShaderiv)(GLuint, GLenum, GLint *);
extern void (*traash_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
extern GLuint (*traash_glCreateProgram)(void);
extern void (*traash_glAttachShader)(GLuint, GLuint);
extern void (*traash_glLinkProgram)(GLuint);
extern void (*traash_glGetProgramiv)(GLuint, GLenum, GLint *);
extern void (*traash_glGetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
extern void (*traash_glDeleteShader)(GLuint);
extern void (*traash_glUseProgram)(GLuint);
extern void (*traash_glGenBuffers)(GLsizei, GLuint *);
extern void (*traash_glBindBuffer)(GLenum, GLuint);
extern void (*traash_glBufferData)(GLenum, GLsizeiptr, const void *, GLenum);
extern void (*traash_glGenVertexArrays)(GLsizei, GLuint *);
extern void (*traash_glBindVertexArray)(GLuint);
extern void (*traash_glEnableVertexAttribArray)(GLuint);
extern void (*traash_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei,
                                           const void *);
extern void (*traash_glDrawArrays)(GLenum, GLint, GLsizei);
extern void (*traash_glGenTextures)(GLsizei, GLuint *);
extern void (*traash_glBindTexture)(GLenum, GLuint);
extern void (*traash_glTexParameteri)(GLenum, GLenum, GLint);
extern void (*traash_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum,
                                  GLenum, const void *);
extern void (*traash_glTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum,
                                     GLenum, const void *);
extern void (*traash_glActiveTexture)(GLenum);
extern GLint (*traash_glGetUniformLocation)(GLuint, const GLchar *);
extern void (*traash_glUniform1i)(GLint, GLint);
extern void (*traash_glUniform1f)(GLint, GLfloat);
extern void (*traash_glUniform2f)(GLint, GLfloat, GLfloat);
extern void (*traash_glUniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
extern void (*traash_glPixelStorei)(GLenum, GLint);
extern void (*traash_glDeleteProgram)(GLuint);
extern void (*traash_glDeleteBuffers)(GLsizei, const GLuint *);
extern void (*traash_glDeleteVertexArrays)(GLsizei, const GLuint *);
extern void (*traash_glDeleteTextures)(GLsizei, const GLuint *);

#endif
