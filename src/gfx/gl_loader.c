#include "gfx/gl_loader.h"

#include <GLFW/glfw3.h>
#include <string.h>

void (*traash_glClear)(GLuint);
void (*traash_glClearColor)(float, float, float, float);
void (*traash_glViewport)(GLint, GLint, GLsizei, GLsizei);
void (*traash_glEnable)(GLenum);
void (*traash_glBlendFunc)(GLenum, GLenum);
void (*traash_glBlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
GLuint (*traash_glCreateShader)(GLenum);
void (*traash_glShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
void (*traash_glCompileShader)(GLuint);
void (*traash_glGetShaderiv)(GLuint, GLenum, GLint *);
void (*traash_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
GLuint (*traash_glCreateProgram)(void);
void (*traash_glAttachShader)(GLuint, GLuint);
void (*traash_glLinkProgram)(GLuint);
void (*traash_glGetProgramiv)(GLuint, GLenum, GLint *);
void (*traash_glGetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
void (*traash_glDeleteShader)(GLuint);
void (*traash_glUseProgram)(GLuint);
void (*traash_glGenBuffers)(GLsizei, GLuint *);
void (*traash_glBindBuffer)(GLenum, GLuint);
void (*traash_glBufferData)(GLenum, GLsizeiptr, const void *, GLenum);
void (*traash_glGenVertexArrays)(GLsizei, GLuint *);
void (*traash_glBindVertexArray)(GLuint);
void (*traash_glEnableVertexAttribArray)(GLuint);
void (*traash_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei,
                                     const void *);
void (*traash_glDrawArrays)(GLenum, GLint, GLsizei);
void (*traash_glGenTextures)(GLsizei, GLuint *);
void (*traash_glBindTexture)(GLenum, GLuint);
void (*traash_glTexParameteri)(GLenum, GLenum, GLint);
void (*traash_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                            const void *);
void (*traash_glTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum,
                               GLenum, const void *);
void (*traash_glActiveTexture)(GLenum);
GLint (*traash_glGetUniformLocation)(GLuint, const GLchar *);
void (*traash_glUniform1i)(GLint, GLint);
void (*traash_glUniform1f)(GLint, GLfloat);
void (*traash_glUniform2f)(GLint, GLfloat, GLfloat);
void (*traash_glUniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
void (*traash_glPixelStorei)(GLenum, GLint);
void (*traash_glDeleteProgram)(GLuint);
void (*traash_glDeleteBuffers)(GLsizei, const GLuint *);
void (*traash_glDeleteVertexArrays)(GLsizei, const GLuint *);
void (*traash_glDeleteTextures)(GLsizei, const GLuint *);

#define LOAD(name) traash_##name = (void *)glfwGetProcAddress(#name)

bool traash_gl_load(void) {
  LOAD(glClear);
  LOAD(glClearColor);
  LOAD(glViewport);
  LOAD(glEnable);
  LOAD(glBlendFunc);
  LOAD(glBlendFuncSeparate);
  LOAD(glCreateShader);
  LOAD(glShaderSource);
  LOAD(glCompileShader);
  LOAD(glGetShaderiv);
  LOAD(glGetShaderInfoLog);
  LOAD(glCreateProgram);
  LOAD(glAttachShader);
  LOAD(glLinkProgram);
  LOAD(glGetProgramiv);
  LOAD(glGetProgramInfoLog);
  LOAD(glDeleteShader);
  LOAD(glUseProgram);
  LOAD(glGenBuffers);
  LOAD(glBindBuffer);
  LOAD(glBufferData);
  LOAD(glGenVertexArrays);
  LOAD(glBindVertexArray);
  LOAD(glEnableVertexAttribArray);
  LOAD(glVertexAttribPointer);
  LOAD(glDrawArrays);
  LOAD(glGenTextures);
  LOAD(glBindTexture);
  LOAD(glTexParameteri);
  LOAD(glTexImage2D);
  LOAD(glTexSubImage2D);
  LOAD(glActiveTexture);
  LOAD(glGetUniformLocation);
  LOAD(glUniform1i);
  LOAD(glUniform1f);
  LOAD(glUniform2f);
  LOAD(glUniform4f);
  LOAD(glPixelStorei);
  LOAD(glDeleteProgram);
  LOAD(glDeleteBuffers);
  LOAD(glDeleteVertexArrays);
  LOAD(glDeleteTextures);
  return traash_glCreateProgram != NULL && traash_glClear != NULL;
}
