#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <SDL2/SDL.h>

namespace ilands::gl {

inline PFNGLGENBUFFERSPROC glGenBuffers = nullptr;
inline PFNGLDELETEBUFFERSPROC glDeleteBuffers = nullptr;
inline PFNGLBINDBUFFERPROC glBindBuffer = nullptr;
inline PFNGLBUFFERDATAPROC glBufferData = nullptr;
inline PFNGLBUFFERSUBDATAPROC glBufferSubData = nullptr;
inline PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = nullptr;
inline PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays = nullptr;
inline PFNGLBINDVERTEXARRAYPROC glBindVertexArray = nullptr;
inline PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = nullptr;
inline PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = nullptr;
inline PFNGLCREATESHADERPROC glCreateShader = nullptr;
inline PFNGLSHADERSOURCEPROC glShaderSource = nullptr;
inline PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
inline PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
inline PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = nullptr;
inline PFNGLDELETESHADERPROC glDeleteShader = nullptr;
inline PFNGLCREATEPROGRAMPROC glCreateProgram = nullptr;
inline PFNGLATTACHSHADERPROC glAttachShader = nullptr;
inline PFNGLLINKPROGRAMPROC glLinkProgram = nullptr;
inline PFNGLGETPROGRAMIVPROC glGetProgramiv = nullptr;
inline PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = nullptr;
inline PFNGLUSEPROGRAMPROC glUseProgram = nullptr;
inline PFNGLDELETEPROGRAMPROC glDeleteProgram = nullptr;
inline PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = nullptr;
inline PFNGLUNIFORM1IPROC glUniform1i = nullptr;
inline PFNGLUNIFORM1FPROC glUniform1f = nullptr;
inline PFNGLUNIFORM2FPROC glUniform2f = nullptr;
inline PFNGLUNIFORM3FPROC glUniform3f = nullptr;
inline PFNGLUNIFORM4FPROC glUniform4f = nullptr;
inline PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = nullptr;
inline PFNGLACTIVETEXTUREPROC glActiveTexture = nullptr;

inline bool LoadFunctions() {
    auto load = [](const char* name) -> void* {
        return SDL_GL_GetProcAddress(name);
    };
    glGenBuffers = reinterpret_cast<PFNGLGENBUFFERSPROC>(load("glGenBuffers"));
    glDeleteBuffers = reinterpret_cast<PFNGLDELETEBUFFERSPROC>(load("glDeleteBuffers"));
    glBindBuffer = reinterpret_cast<PFNGLBINDBUFFERPROC>(load("glBindBuffer"));
    glBufferData = reinterpret_cast<PFNGLBUFFERDATAPROC>(load("glBufferData"));
    glBufferSubData = reinterpret_cast<PFNGLBUFFERSUBDATAPROC>(load("glBufferSubData"));
    glGenVertexArrays = reinterpret_cast<PFNGLGENVERTEXARRAYSPROC>(load("glGenVertexArrays"));
    glDeleteVertexArrays = reinterpret_cast<PFNGLDELETEVERTEXARRAYSPROC>(load("glDeleteVertexArrays"));
    glBindVertexArray = reinterpret_cast<PFNGLBINDVERTEXARRAYPROC>(load("glBindVertexArray"));
    glEnableVertexAttribArray = reinterpret_cast<PFNGLENABLEVERTEXATTRIBARRAYPROC>(load("glEnableVertexAttribArray"));
    glVertexAttribPointer = reinterpret_cast<PFNGLVERTEXATTRIBPOINTERPROC>(load("glVertexAttribPointer"));
    glCreateShader = reinterpret_cast<PFNGLCREATESHADERPROC>(load("glCreateShader"));
    glShaderSource = reinterpret_cast<PFNGLSHADERSOURCEPROC>(load("glShaderSource"));
    glCompileShader = reinterpret_cast<PFNGLCOMPILESHADERPROC>(load("glCompileShader"));
    glGetShaderiv = reinterpret_cast<PFNGLGETSHADERIVPROC>(load("glGetShaderiv"));
    glGetShaderInfoLog = reinterpret_cast<PFNGLGETSHADERINFOLOGPROC>(load("glGetShaderInfoLog"));
    glDeleteShader = reinterpret_cast<PFNGLDELETESHADERPROC>(load("glDeleteShader"));
    glCreateProgram = reinterpret_cast<PFNGLCREATEPROGRAMPROC>(load("glCreateProgram"));
    glAttachShader = reinterpret_cast<PFNGLATTACHSHADERPROC>(load("glAttachShader"));
    glLinkProgram = reinterpret_cast<PFNGLLINKPROGRAMPROC>(load("glLinkProgram"));
    glGetProgramiv = reinterpret_cast<PFNGLGETPROGRAMIVPROC>(load("glGetProgramiv"));
    glGetProgramInfoLog = reinterpret_cast<PFNGLGETPROGRAMINFOLOGPROC>(load("glGetProgramInfoLog"));
    glUseProgram = reinterpret_cast<PFNGLUSEPROGRAMPROC>(load("glUseProgram"));
    glDeleteProgram = reinterpret_cast<PFNGLDELETEPROGRAMPROC>(load("glDeleteProgram"));
    glGetUniformLocation = reinterpret_cast<PFNGLGETUNIFORMLOCATIONPROC>(load("glGetUniformLocation"));
    glUniform1i = reinterpret_cast<PFNGLUNIFORM1IPROC>(load("glUniform1i"));
    glUniform1f = reinterpret_cast<PFNGLUNIFORM1FPROC>(load("glUniform1f"));
    glUniform2f = reinterpret_cast<PFNGLUNIFORM2FPROC>(load("glUniform2f"));
    glUniform3f = reinterpret_cast<PFNGLUNIFORM3FPROC>(load("glUniform3f"));
    glUniform4f = reinterpret_cast<PFNGLUNIFORM4FPROC>(load("glUniform4f"));
    glUniformMatrix4fv = reinterpret_cast<PFNGLUNIFORMMATRIX4FVPROC>(load("glUniformMatrix4fv"));
    glActiveTexture = reinterpret_cast<PFNGLACTIVETEXTUREPROC>(load("glActiveTexture"));

    return glGenBuffers && glGenVertexArrays && glCreateShader && glCreateProgram && glActiveTexture;
}

}
