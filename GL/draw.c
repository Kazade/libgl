#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include "private.h"
#include "platform.h"

static AttribPointer VERTEX_POINTER;
static AttribPointer UV_POINTER;
static AttribPointer ST_POINTER;
static AttribPointer NORMAL_POINTER;
static AttribPointer DIFFUSE_POINTER;

static GLuint ENABLED_VERTEX_ATTRIBUTES = 0;
static GLubyte ACTIVE_CLIENT_TEXTURE = 0;
static GLboolean FAST_PATH_ENABLED = GL_FALSE;

#define ITERATE(count) \
    GLuint i = count; \
    while(i--)


void _glInitAttributePointers() {
    TRACE();

    VERTEX_POINTER.ptr = NULL;
    VERTEX_POINTER.stride = 0;
    VERTEX_POINTER.type = GL_FLOAT;
    VERTEX_POINTER.size = 4;

    DIFFUSE_POINTER.ptr = NULL;
    DIFFUSE_POINTER.stride = 0;
    DIFFUSE_POINTER.type = GL_FLOAT;
    DIFFUSE_POINTER.size = 4;

    UV_POINTER.ptr = NULL;
    UV_POINTER.stride = 0;
    UV_POINTER.type = GL_FLOAT;
    UV_POINTER.size = 4;

    ST_POINTER.ptr = NULL;
    ST_POINTER.stride = 0;
    ST_POINTER.type = GL_FLOAT;
    ST_POINTER.size = 4;

    NORMAL_POINTER.ptr = NULL;
    NORMAL_POINTER.stride = 0;
    NORMAL_POINTER.type = GL_FLOAT;
    NORMAL_POINTER.size = 3;
}

GL_FORCE_INLINE GLboolean _glIsVertexDataFastPathCompatible() {
    /* The fast path is enabled when all enabled elements of the vertex
     * match the output format. This means:
     *
     * xyz == 3f
     * uv == 2f
     * rgba == argb4444
     * st == 2f
     * normal == 3f
     *
     * When this happens we do inline straight copies of the enabled data
     * and transforms for positions and normals happen while copying.
     */

    if((ENABLED_VERTEX_ATTRIBUTES & VERTEX_ENABLED_FLAG)) {
        if(VERTEX_POINTER.size != 3 || VERTEX_POINTER.type != GL_FLOAT) {
            return GL_FALSE;
        }
    }

    if((ENABLED_VERTEX_ATTRIBUTES & UV_ENABLED_FLAG)) {
        if(UV_POINTER.size != 2 || UV_POINTER.type != GL_FLOAT) {
            return GL_FALSE;
        }
    }

    if((ENABLED_VERTEX_ATTRIBUTES & DIFFUSE_ENABLED_FLAG)) {
        /* FIXME: Shouldn't this be a reversed format? */
        if(DIFFUSE_POINTER.size != GL_BGRA || DIFFUSE_POINTER.type != GL_UNSIGNED_BYTE) {
            return GL_FALSE;
        }
    }

    if((ENABLED_VERTEX_ATTRIBUTES & ST_ENABLED_FLAG)) {
        if(ST_POINTER.size != 2 || ST_POINTER.type != GL_FLOAT) {
            return GL_FALSE;
        }
    }

    if((ENABLED_VERTEX_ATTRIBUTES & NORMAL_ENABLED_FLAG)) {
        if(NORMAL_POINTER.size != 3 || NORMAL_POINTER.type != GL_FLOAT) {
            return GL_FALSE;
        }
    }

    return GL_TRUE;
}

GL_FORCE_INLINE GLsizei byte_size(GLenum type) {
    switch(type) {
    case GL_BYTE: return sizeof(GLbyte);
    case GL_UNSIGNED_BYTE: return sizeof(GLubyte);
    case GL_SHORT: return sizeof(GLshort);
    case GL_UNSIGNED_SHORT: return sizeof(GLushort);
    case GL_INT: return sizeof(GLint);
    case GL_UNSIGNED_INT: return sizeof(GLuint);
    case GL_DOUBLE: return sizeof(GLdouble);
    case GL_UNSIGNED_INT_2_10_10_10_REV: return sizeof(GLuint);
    case GL_FLOAT:
    default: return sizeof(GLfloat);
    }
}

typedef void (*FloatParseFunc)(GLfloat* out, const GLubyte* in);
typedef void (*ByteParseFunc)(GLubyte* out, const GLubyte* in);
typedef void (*PolyBuildFunc)(Vertex* first, Vertex* previous, Vertex* vertex, Vertex* next, const GLsizei i);

static void _readVertexData3f3f(const GLubyte* __restrict__ in, GLubyte* __restrict__ out) {
    vec3cpy(out, in);
}

// 10:10:10:2REV format
static void _readVertexData1i3f(const GLubyte* in, GLubyte* out) {
    const static float MULTIPLIER = 1.0f / 1023.0f;

    GLfloat* output = (GLfloat*) out;

    union {
        int value;
        struct {
            signed int x: 10;
            signed int y: 10;
            signed int z: 10;
            signed int w: 2;
        } bits;
    } input;

    input.value = *((const GLint*) in);

    output[0] = (2.0f * (float) input.bits.x + 1.0f) * MULTIPLIER;
    output[1] = (2.0f * (float) input.bits.y + 1.0f) * MULTIPLIER;
    output[2] = (2.0f * (float) input.bits.z + 1.0f) * MULTIPLIER;
}

static void _readVertexData3us3f(const GLubyte* in, GLubyte* out) {
    const GLushort* input = (const GLushort*) in;
    float* output = (float*) out;

    output[0] = input[0];
    output[1] = input[1];
    output[2] = input[2];
}

static void _readVertexData3ui3f(const GLubyte* in, GLubyte* out) {
    const GLuint* input = (const GLuint*) in;
    float* output = (float*) out;

    output[0] = input[0];
    output[1] = input[1];
    output[2] = input[2];
}


static void _readVertexData3ub3f(const GLubyte* input, GLubyte* out) {
    const float ONE_OVER_TWO_FIVE_FIVE = 1.0f / 255.0f;

    float* output = (float*) out;

    output[0] = input[0] * ONE_OVER_TWO_FIVE_FIVE;
    output[1] = input[1] * ONE_OVER_TWO_FIVE_FIVE;
    output[2] = input[2] * ONE_OVER_TWO_FIVE_FIVE;
}

static void _readVertexData2f2f(const GLubyte* in, GLubyte* out) {
    vec2cpy(out, in);
}

static void _readVertexData2f3f(const GLubyte* in, GLubyte* out) {
    const float* input = (const float*) in;
    float* output = (float*) out;

    vec2cpy(output, input);
    output[2] = 0.0f;
}

static void _readVertexData2ub3f(const GLubyte* input, GLubyte* out) {
    const float ONE_OVER_TWO_FIVE_FIVE = 1.0f / 255.0f;

    float* output = (float*) out;

    output[0] = input[0] * ONE_OVER_TWO_FIVE_FIVE;
    output[1] = input[1] * ONE_OVER_TWO_FIVE_FIVE;
    output[2] = 0.0f;
}

static void _readVertexData2us3f(const GLubyte* in, GLubyte* out) {
    const GLushort* input = (const GLushort*) in;
    float* output = (float*) out;

    output[0] = input[0];
    output[1] = input[1];
    output[2] = 0.0f;
}

static void _readVertexData2us2f(const GLubyte* in, GLubyte* out) {
    const GLushort* input = (const GLushort*) in;
    float* output = (float*) out;

    output[0] = input[0];
    output[1] = input[1];
}

static void _readVertexData2ui2f(const GLubyte* in, GLubyte* out) {
    const GLuint* input = (const GLuint*) in;
    float* output = (float*) out;

    output[0] = input[0];
    output[1] = input[1];
}

static void _readVertexData2ub2f(const GLubyte* input, GLubyte* out) {
    const float ONE_OVER_TWO_FIVE_FIVE = 1.0f / 255.0f;
    float* output = (float*) out;

    output[0] = input[0] * ONE_OVER_TWO_FIVE_FIVE;
    output[1] = input[1] * ONE_OVER_TWO_FIVE_FIVE;
}

static void _readVertexData2ui3f(const GLubyte* in, GLubyte* out) {
    const GLuint* input = (const GLuint*) in;
    float* output = (float*) out;

    output[0] = input[0];
    output[1] = input[1];
    output[2] = 0.0f;
}

static void _readVertexData4ubARGB(const GLubyte* input, GLubyte* output) {
    output[R8IDX] = input[0];
    output[G8IDX] = input[1];
    output[B8IDX] = input[2];
    output[A8IDX] = input[3];
}

static void _readVertexData4fARGB(const GLubyte* in, GLubyte* output) {
    const float* input = (const float*) in;

    output[R8IDX] = (GLubyte) clamp(input[0] * 255.0f, 0, 255);
    output[G8IDX] = (GLubyte) clamp(input[1] * 255.0f, 0, 255);
    output[B8IDX] = (GLubyte) clamp(input[2] * 255.0f, 0, 255);
    output[A8IDX] = (GLubyte) clamp(input[3] * 255.0f, 0, 255);
}

static void _readVertexData3fARGB(const GLubyte* in, GLubyte* output) {
    const float* input = (const float*) in;

    output[R8IDX] = (GLubyte) clamp(input[0] * 255.0f, 0, 255);
    output[G8IDX] = (GLubyte) clamp(input[1] * 255.0f, 0, 255);
    output[B8IDX] = (GLubyte) clamp(input[2] * 255.0f, 0, 255);
    output[A8IDX] = 1.0f;
}

static void _readVertexData3ubARGB(const GLubyte* __restrict__ input, GLubyte* __restrict__ output) {
    output[R8IDX] = input[0];
    output[G8IDX] = input[1];
    output[B8IDX] = input[2];
    output[A8IDX] = 1.0f;
}

static void _readVertexData4ubRevARGB(const GLubyte* __restrict__ input, GLubyte* __restrict__ output) {
    argbcpy(output, input);
}

static void _readVertexData4fRevARGB(const GLubyte* __restrict__ in, GLubyte* __restrict__ output) {
    const float* input = (const float*) in;

    output[0] = (GLubyte) clamp(input[0] * 255.0f, 0, 255);
    output[1] = (GLubyte) clamp(input[1] * 255.0f, 0, 255);
    output[2] = (GLubyte) clamp(input[2] * 255.0f, 0, 255);
    output[3] = (GLubyte) clamp(input[3] * 255.0f, 0, 255);
}

static void _fillWithNegZVE(const GLubyte* __restrict__ input, GLubyte* __restrict__ out) {
    _GL_UNUSED(input);

    typedef struct {
        float x, y, z;
    } V;

    const static V NegZ = {0.0f, 0.0f, -1.0f};

    *((V*) out) = NegZ;
}

static void  _fillWhiteARGB(const GLubyte* __restrict__ input, GLubyte* __restrict__ output) {
    _GL_UNUSED(input);
    *((uint32_t*) output) = ~0;
}

static void _fillZero2f(const GLubyte* __restrict__ input, GLubyte* __restrict__ out) {
    _GL_UNUSED(input);
    memset(out, sizeof(float) * 2, 0);
}

static void _readVertexData3usARGB(const GLubyte* input, GLubyte* output) {
    _GL_UNUSED(input);
    _GL_UNUSED(output);
    assert(0 && "Not Implemented");
}

static void _readVertexData3uiARGB(const GLubyte* input, GLubyte* output) {
    _GL_UNUSED(input);
    _GL_UNUSED(output);
    assert(0 && "Not Implemented");
}

static void _readVertexData4usARGB(const GLubyte* input, GLubyte* output) {
    _GL_UNUSED(input);
    _GL_UNUSED(output);
    assert(0 && "Not Implemented");
}

static void _readVertexData4uiARGB(const GLubyte* input, GLubyte* output) {
    _GL_UNUSED(input);
    _GL_UNUSED(output);
    assert(0 && "Not Implemented");
}

static void _readVertexData4usRevARGB(const GLubyte* input, GLubyte* output) {
    _GL_UNUSED(input);
    _GL_UNUSED(output);
    assert(0 && "Not Implemented");
}

static void _readVertexData4uiRevARGB(const GLubyte* input, GLubyte* output) {
    _GL_UNUSED(input);
    _GL_UNUSED(output);
    assert(0 && "Not Implemented");
}

GLuint* _glGetEnabledAttributes() {
    return &ENABLED_VERTEX_ATTRIBUTES;
}

AttribPointer* _glGetVertexAttribPointer() {
    return &VERTEX_POINTER;
}

AttribPointer* _glGetDiffuseAttribPointer() {
    return &DIFFUSE_POINTER;
}

AttribPointer* _glGetNormalAttribPointer() {
    return &NORMAL_POINTER;
}

AttribPointer* _glGetUVAttribPointer() {
    return &UV_POINTER;
}

AttribPointer* _glGetSTAttribPointer() {
    return &ST_POINTER;
}

typedef GLuint (*IndexParseFunc)(const GLubyte* in);

static inline GLuint _parseUByteIndex(const GLubyte* in) {
    return (GLuint) *in;
}

static inline GLuint _parseUIntIndex(const GLubyte* in) {
    return *((GLuint*) in);
}

static inline GLuint _parseUShortIndex(const GLubyte* in) {
    return *((GLshort*) in);
}


GL_FORCE_INLINE IndexParseFunc _calcParseIndexFunc(GLenum type) {
    switch(type) {
    case GL_UNSIGNED_BYTE:
        return &_parseUByteIndex;
    break;
    case GL_UNSIGNED_INT:
        return &_parseUIntIndex;
    break;
    case GL_UNSIGNED_SHORT:
    default:
        break;
    }

    return &_parseUShortIndex;
}


/* There was a bug in this macro that shipped with Kos
 * which has now been fixed. But just in case...
 */
#undef mat_trans_single3_nodiv
#define mat_trans_single3_nodiv(x, y, z) { \
    register float __x __asm__("fr12") = (x); \
    register float __y __asm__("fr13") = (y); \
    register float __z __asm__("fr14") = (z); \
    __asm__ __volatile__( \
                          "fldi1 fr15\n" \
                          "ftrv  xmtrx, fv12\n" \
                          : "=f" (__x), "=f" (__y), "=f" (__z) \
                          : "0" (__x), "1" (__y), "2" (__z) \
                          : "fr15"); \
    x = __x; y = __y; z = __z; \
}


/* FIXME: Is this right? Shouldn't it be fr12->15? */
#undef mat_trans_normal3
#define mat_trans_normal3(x, y, z) { \
    register float __x __asm__("fr8") = (x); \
    register float __y __asm__("fr9") = (y); \
    register float __z __asm__("fr10") = (z); \
    __asm__ __volatile__( \
                          "fldi0 fr11\n" \
                          "ftrv  xmtrx, fv8\n" \
                          : "=f" (__x), "=f" (__y), "=f" (__z) \
                          : "0" (__x), "1" (__y), "2" (__z) \
                          : "fr11"); \
    x = __x; y = __y; z = __z; \
}


GL_FORCE_INLINE void transformToEyeSpace(GLfloat* point) {
    _glMatrixLoadModelView();
    mat_trans_single3_nodiv(point[0], point[1], point[2]);
}

GL_FORCE_INLINE void transformNormalToEyeSpace(GLfloat* normal) {
    _glMatrixLoadNormal();
    mat_trans_normal3(normal[0], normal[1], normal[2]);
}

GL_FORCE_INLINE PolyHeader *_glSubmissionTargetHeader(SubmissionTarget* target) {
    assert(target->header_offset < target->output->vector.size);
    return aligned_vector_at(&target->output->vector, target->header_offset);
}

GL_INLINE_DEBUG Vertex* _glSubmissionTargetStart(SubmissionTarget* target) {
    assert(target->start_offset < target->output->vector.size);
    return aligned_vector_at(&target->output->vector, target->start_offset);
}

Vertex* _glSubmissionTargetEnd(SubmissionTarget* target) {
    return _glSubmissionTargetStart(target) + target->count;
}

GL_FORCE_INLINE void genTriangles(Vertex* output, GLuint count) {
    Vertex* it = output + 2;

    GLuint i;
    for(i = 0; i < count; i += 3) {
        it->flags = GPU_CMD_VERTEX_EOL;
        it += 3;
    }
}

GL_FORCE_INLINE void genQuads(Vertex* output, GLuint count) {
    Vertex* pen = output + 2;
    Vertex* final = output + 3;
    GLuint i = count >> 2;
    while(i--) {
        __asm__("pref @%0" : : "r"(pen + 4));

        swapVertex(pen, final);
        final->flags = GPU_CMD_VERTEX_EOL;

        pen += 4;
        final += 4;
    }
}

GL_FORCE_INLINE void genTriangleStrip(Vertex* output, GLuint count) {
    output[count - 1].flags = GPU_CMD_VERTEX_EOL;
}

static void genTriangleFan(Vertex* output, GLuint count) {
    assert(count <= 255);

    Vertex* dst = output + (((count - 2) * 3) - 1);
    Vertex* src = output + (count - 1);

    GLubyte i = count - 2;
    while(i--) {
        *dst = *src--;
        (*dst--).flags = GPU_CMD_VERTEX_EOL;
        *dst-- = *src;
        *dst-- = *output;
    }
}

typedef void (*ReadPositionFunc)(const GLubyte*, GLubyte*);
typedef void (*ReadDiffuseFunc)(const GLubyte*, GLubyte*);
typedef void (*ReadUVFunc)(const GLubyte*, GLubyte*);
typedef void (*ReadNormalFunc)(const GLubyte*, GLubyte*);

ReadPositionFunc calcReadDiffuseFunc() {
    if((ENABLED_VERTEX_ATTRIBUTES & DIFFUSE_ENABLED_FLAG) != DIFFUSE_ENABLED_FLAG) {
        /* Just fill the whole thing white if the attribute is disabled */
        return _fillWhiteARGB;
    }

    switch(DIFFUSE_POINTER.type) {
        default:
        case GL_DOUBLE:
        case GL_FLOAT:
            return (DIFFUSE_POINTER.size == 3) ? _readVertexData3fARGB:
                   (DIFFUSE_POINTER.size == 4) ? _readVertexData4fARGB:
                    _readVertexData4fRevARGB;
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:
            return (DIFFUSE_POINTER.size == 3) ? _readVertexData3ubARGB:
                   (DIFFUSE_POINTER.size == 4) ? _readVertexData4ubARGB:
                    _readVertexData4ubRevARGB;
        case GL_SHORT:
        case GL_UNSIGNED_SHORT:
            return (DIFFUSE_POINTER.size == 3) ? _readVertexData3usARGB:
                   (DIFFUSE_POINTER.size == 4) ? _readVertexData4usARGB:
                    _readVertexData4usRevARGB;
        case GL_INT:
        case GL_UNSIGNED_INT:
            return (DIFFUSE_POINTER.size == 3) ? _readVertexData3uiARGB:
                   (DIFFUSE_POINTER.size == 4) ? _readVertexData4uiARGB:
                    _readVertexData4uiRevARGB;
    }
}

ReadPositionFunc calcReadPositionFunc() {
    switch(VERTEX_POINTER.type) {
        default:
        case GL_DOUBLE:
        case GL_FLOAT:
            return (VERTEX_POINTER.size == 3) ? _readVertexData3f3f:
                    _readVertexData2f3f;
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:
            return (VERTEX_POINTER.size == 3) ? _readVertexData3ub3f:
                    _readVertexData2ub3f;
        case GL_SHORT:
        case GL_UNSIGNED_SHORT:
            return (VERTEX_POINTER.size == 3) ? _readVertexData3us3f:
                    _readVertexData2us3f;
        case GL_INT:
        case GL_UNSIGNED_INT:
            return (VERTEX_POINTER.size == 3) ? _readVertexData3ui3f:
                    _readVertexData2ui3f;
    }
}

ReadUVFunc calcReadUVFunc() {
    if((ENABLED_VERTEX_ATTRIBUTES & UV_ENABLED_FLAG) != UV_ENABLED_FLAG) {
        return _fillZero2f;
    }

    switch(UV_POINTER.type) {
        default:
        case GL_DOUBLE:
        case GL_FLOAT:
            return _readVertexData2f2f;
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:
            return _readVertexData2ub2f;
        case GL_SHORT:
        case GL_UNSIGNED_SHORT:
            return _readVertexData2us2f;
        case GL_INT:
        case GL_UNSIGNED_INT:
            return _readVertexData2ui2f;
    }
}

ReadUVFunc calcReadSTFunc() {
    if((ENABLED_VERTEX_ATTRIBUTES & ST_ENABLED_FLAG) != ST_ENABLED_FLAG) {
        return _fillZero2f;
    }

    switch(ST_POINTER.type) {
        default:
        case GL_DOUBLE:
        case GL_FLOAT:
            return _readVertexData2f2f;
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:
            return _readVertexData2ub2f;
        case GL_SHORT:
        case GL_UNSIGNED_SHORT:
            return _readVertexData2us2f;
        case GL_INT:
        case GL_UNSIGNED_INT:
            return _readVertexData2ui2f;
    }
}

ReadNormalFunc calcReadNormalFunc() {
    if((ENABLED_VERTEX_ATTRIBUTES & NORMAL_ENABLED_FLAG) != NORMAL_ENABLED_FLAG) {
        return _fillWithNegZVE;
    }

    switch(NORMAL_POINTER.type) {
        default:
        case GL_DOUBLE:
        case GL_FLOAT:
            return _readVertexData3f3f;
        break;
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:
            return _readVertexData3ub3f;
        break;
        case GL_SHORT:
        case GL_UNSIGNED_SHORT:
            return _readVertexData3us3f;
        break;
        case GL_INT:
        case GL_UNSIGNED_INT:
            return _readVertexData3ui3f;
        break;
        case GL_UNSIGNED_INT_2_10_10_10_REV:
            return _readVertexData1i3f;
        break;
    }
}

static void _readPositionData(ReadDiffuseFunc func, const GLuint first, const GLuint count, const Vertex* output) {
    const GLsizei vstride = (VERTEX_POINTER.stride) ? VERTEX_POINTER.stride : VERTEX_POINTER.size * byte_size(VERTEX_POINTER.type);
    const GLubyte* vptr = ((GLubyte*) VERTEX_POINTER.ptr + (first * vstride));

    GLubyte* out = (GLubyte*) output[0].xyz;
    uint32_t* flags;

    ITERATE(count) {
        __asm__("pref @%0" : : "r"(vptr + vstride));

        func(vptr, out);
        vptr += vstride;

        /* Set the flags which are 4 bytes before the position. Doing it here saves
         * an additional loop */
        flags = (uint32_t*) out - 1;
        *flags = GPU_CMD_VERTEX;

        out += sizeof(Vertex);
    }
}

static void _readUVData(ReadUVFunc func, const GLuint first, const GLuint count, const Vertex* output) {
    const GLsizei uvstride = (UV_POINTER.stride) ? UV_POINTER.stride : UV_POINTER.size * byte_size(UV_POINTER.type);
    const GLubyte* uvptr = ((GLubyte*) UV_POINTER.ptr + (first * uvstride));

    GLubyte* out = (GLubyte*) output[0].uv;

    ITERATE(count) {
        __asm__("pref @%0" : : "r"(uvptr + uvstride));

        func(uvptr, out);
        uvptr += uvstride;
        out += sizeof(Vertex);
    }
}

static void _readSTData(ReadUVFunc func, const GLuint first, const GLuint count, const VertexExtra* extra) {
    const GLsizei ststride = (ST_POINTER.stride) ? ST_POINTER.stride : ST_POINTER.size * byte_size(ST_POINTER.type);
    const GLubyte* stptr = ((GLubyte*) ST_POINTER.ptr + (first * ststride));

    GLubyte* out = (GLubyte*) extra[0].st;

    ITERATE(count) {
        __asm__("pref @%0" : : "r"(stptr + ststride));

        func(stptr, out);
        stptr += ststride;
        out += sizeof(VertexExtra);
    }
}

static void _readNormalData(ReadNormalFunc func, const GLuint first, const GLuint count, const VertexExtra* extra) {
    const GLsizei nstride = (NORMAL_POINTER.stride) ? NORMAL_POINTER.stride : NORMAL_POINTER.size * byte_size(NORMAL_POINTER.type);
    const GLubyte* nptr = ((GLubyte*) NORMAL_POINTER.ptr + (first * nstride));

    GLubyte* out = (GLubyte*) extra[0].nxyz;

    ITERATE(count) {
        func(nptr, out);
        nptr += nstride;
        out += sizeof(VertexExtra);
    }

    if(_glIsNormalizeEnabled()) {
        GLubyte* ptr = (GLubyte*) extra->nxyz;
        ITERATE(count) {
            GLfloat* n = (GLfloat*) ptr;
            float temp = n[0] * n[0];
            temp = MATH_fmac(n[1], n[1], temp);
            temp = MATH_fmac(n[2], n[2], temp);

            float ilength = MATH_fsrra(temp);
            n[0] *= ilength;
            n[1] *= ilength;
            n[2] *= ilength;

            ptr += sizeof(VertexExtra);
        }
    }
}

GL_FORCE_INLINE GLuint diffusePointerSize() {
    return (DIFFUSE_POINTER.size == GL_BGRA) ? 4 : DIFFUSE_POINTER.size;
}

static void _readDiffuseData(ReadDiffuseFunc func, const GLuint first, const GLuint count, const Vertex* output) {
    const GLuint size = diffusePointerSize();
    const GLuint cstride = (DIFFUSE_POINTER.stride) ? DIFFUSE_POINTER.stride : size * byte_size(DIFFUSE_POINTER.type);
    const GLubyte* cptr = ((GLubyte*) DIFFUSE_POINTER.ptr) + (first * cstride);

    GLubyte* out = (GLubyte*) output[0].bgra;

    ITERATE(count) {
        __asm__("pref @%0" : : "r"(cptr + cstride));

        func(cptr, out);
        cptr += cstride;
        out += sizeof(Vertex);
    }
}

static void generateElements(
        SubmissionTarget* target, const GLsizei first, const GLuint count,
        const GLubyte* indices, const GLenum type) {

    const GLsizei istride = byte_size(type);
    const IndexParseFunc IndexFunc = _calcParseIndexFunc(type);

    GLubyte* xyz;
    GLubyte* uv;
    GLubyte* bgra;
    GLubyte* st;
    GLubyte* nxyz;

    Vertex* output = _glSubmissionTargetStart(target);
    VertexExtra* ve = aligned_vector_at(target->extras, 0);

    uint32_t i = first;
    uint32_t idx = 0;

    const ReadPositionFunc pos_func = calcReadPositionFunc();
    const ReadUVFunc uv_func = calcReadUVFunc();
    const ReadUVFunc st_func = calcReadSTFunc();
    const ReadDiffuseFunc diffuse_func = calcReadDiffuseFunc();
    const ReadNormalFunc normal_func = calcReadNormalFunc();

    const GLuint vstride = (VERTEX_POINTER.stride) ?
        VERTEX_POINTER.stride : VERTEX_POINTER.size * byte_size(VERTEX_POINTER.type);

    const GLuint uvstride = (UV_POINTER.stride) ?
        UV_POINTER.stride : UV_POINTER.size * byte_size(UV_POINTER.type);

    const GLuint ststride = (ST_POINTER.stride) ?
        ST_POINTER.stride : ST_POINTER.size * byte_size(ST_POINTER.type);

    const GLuint dstride = (DIFFUSE_POINTER.stride) ?
        DIFFUSE_POINTER.stride : diffusePointerSize() * byte_size(DIFFUSE_POINTER.type);

    const GLuint nstride = (NORMAL_POINTER.stride) ?
        NORMAL_POINTER.stride : NORMAL_POINTER.size * byte_size(NORMAL_POINTER.type);

    for(; i < first + count; ++i) {
        idx = IndexFunc(indices + (i * istride));

        xyz = (GLubyte*) VERTEX_POINTER.ptr + (idx * vstride);
        uv = (GLubyte*) UV_POINTER.ptr + (idx * uvstride);
        bgra = (GLubyte*) DIFFUSE_POINTER.ptr + (idx * dstride);
        st = (GLubyte*) ST_POINTER.ptr + (idx * ststride);
        nxyz = (GLubyte*) NORMAL_POINTER.ptr + (idx * nstride);

        pos_func(xyz, (GLubyte*) output->xyz);
        uv_func(uv, (GLubyte*) output->uv);
        diffuse_func(bgra, output->bgra);
        st_func(st, (GLubyte*) ve->st);
        normal_func(nxyz, (GLubyte*) ve->nxyz);

        output->flags = GPU_CMD_VERTEX;
        ++output;
        ++ve;
    }
}

typedef struct {
    float x, y, z;
} Float3;

typedef struct {
    float u, v;
} Float2;

static const Float3 F3Z = {0.0f, 0.0f, 1.0f};
static const Float3 F3ZERO = {0.0f, 0.0f, 0.0f};
static const Float2 F2ZERO = {0.0f, 0.0f};

static void generateElementsFastPath(
        SubmissionTarget* target, const GLsizei first, const GLuint count,
        const GLubyte* indices, const GLenum type) {

    Vertex* start = _glSubmissionTargetStart(target);

    const GLuint vstride = (VERTEX_POINTER.stride) ?
        VERTEX_POINTER.stride : VERTEX_POINTER.size * byte_size(VERTEX_POINTER.type);

    const GLuint uvstride = (UV_POINTER.stride) ?
        UV_POINTER.stride : UV_POINTER.size * byte_size(UV_POINTER.type);

    const GLuint ststride = (ST_POINTER.stride) ?
        ST_POINTER.stride : ST_POINTER.size * byte_size(ST_POINTER.type);

    const GLuint dstride = (DIFFUSE_POINTER.stride) ?
        DIFFUSE_POINTER.stride : diffusePointerSize() * byte_size(DIFFUSE_POINTER.type);

    const GLuint nstride = (NORMAL_POINTER.stride) ?
        NORMAL_POINTER.stride : NORMAL_POINTER.size * byte_size(NORMAL_POINTER.type);

    const GLsizei istride = byte_size(type);
    const IndexParseFunc IndexFunc = _calcParseIndexFunc(type);

    /* Copy the pos, uv and color directly in one go */
    const GLubyte* pos = (ENABLED_VERTEX_ATTRIBUTES & VERTEX_ENABLED_FLAG) ? VERTEX_POINTER.ptr : NULL;
    const GLubyte* uv = (ENABLED_VERTEX_ATTRIBUTES & UV_ENABLED_FLAG) ? UV_POINTER.ptr : NULL;
    const GLubyte* col = (ENABLED_VERTEX_ATTRIBUTES & DIFFUSE_ENABLED_FLAG) ? DIFFUSE_POINTER.ptr : NULL;
    const GLubyte* st = (ENABLED_VERTEX_ATTRIBUTES & ST_ENABLED_FLAG) ? ST_POINTER.ptr : NULL;
    const GLubyte* n = (ENABLED_VERTEX_ATTRIBUTES & NORMAL_ENABLED_FLAG) ? NORMAL_POINTER.ptr : NULL;

    VertexExtra* ve = aligned_vector_at(target->extras, 0);
    Vertex* it = start;

    const float w = 1.0f;

    for(GLuint i = first; i < first + count; ++i) {
        GLuint idx = IndexFunc(indices + (i * istride));

        it->flags = GPU_CMD_VERTEX;

        if(pos) {
            pos = (GLubyte*) VERTEX_POINTER.ptr + (idx * vstride);
            TransformVertex((const float*) pos, &w, it->xyz, &it->w);
        } else {
            *((Float3*) it->xyz) = F3ZERO;
        }

        if(uv) {
            uv = (GLubyte*) UV_POINTER.ptr + (idx * uvstride);
            MEMCPY4(it->uv, uv, sizeof(float) * 2);
        } else {
            *((Float2*) it->uv) = F2ZERO;
        }

        if(col) {
            col = (GLubyte*) DIFFUSE_POINTER.ptr + (idx * dstride);
            MEMCPY4(it->bgra, col, sizeof(uint32_t));
        } else {
            *((uint32_t*) it->bgra) = ~0;
        }

        if(st) {
            st = (GLubyte*) ST_POINTER.ptr + (idx * ststride);
            MEMCPY4(ve->st, st, sizeof(float) * 2);
        } else {
            *((Float2*) ve->st) = F2ZERO;
        }

        if(n) {
            n = (GLubyte*) NORMAL_POINTER.ptr + (idx * nstride);
            MEMCPY4(ve->nxyz, n, sizeof(float) * 3);
        } else {
            *((Float3*) ve->nxyz) = F3Z;
        }

        it++;
        ve++;
    }
}

#define likely(x)      __builtin_expect(!!(x), 1)

static void generateArraysFastPath(SubmissionTarget* target, const GLsizei first, const GLuint count) {
    Vertex* start = _glSubmissionTargetStart(target);

    const GLuint vstride = (VERTEX_POINTER.stride) ?
        VERTEX_POINTER.stride : VERTEX_POINTER.size * byte_size(VERTEX_POINTER.type);

    const GLuint uvstride = (UV_POINTER.stride) ?
        UV_POINTER.stride : UV_POINTER.size * byte_size(UV_POINTER.type);

    const GLuint ststride = (ST_POINTER.stride) ?
        ST_POINTER.stride : ST_POINTER.size * byte_size(ST_POINTER.type);

    const GLuint dstride = (DIFFUSE_POINTER.stride) ?
        DIFFUSE_POINTER.stride : diffusePointerSize() * byte_size(DIFFUSE_POINTER.type);

    const GLuint nstride = (NORMAL_POINTER.stride) ?
        NORMAL_POINTER.stride : NORMAL_POINTER.size * byte_size(NORMAL_POINTER.type);


    /* Copy the pos, uv and color directly in one go */
    const GLubyte* pos = (ENABLED_VERTEX_ATTRIBUTES & VERTEX_ENABLED_FLAG) ? VERTEX_POINTER.ptr + (first * vstride) : NULL;
    const GLubyte* uv = (ENABLED_VERTEX_ATTRIBUTES & UV_ENABLED_FLAG) ? UV_POINTER.ptr + (first * uvstride) : NULL;
    const GLubyte* col = (ENABLED_VERTEX_ATTRIBUTES & DIFFUSE_ENABLED_FLAG) ? DIFFUSE_POINTER.ptr + (first * dstride) : NULL;
    const GLubyte* st = (ENABLED_VERTEX_ATTRIBUTES & ST_ENABLED_FLAG) ? ST_POINTER.ptr + (first * ststride) : NULL;
    const GLubyte* n = (ENABLED_VERTEX_ATTRIBUTES & NORMAL_ENABLED_FLAG) ? NORMAL_POINTER.ptr + (first * nstride) : NULL;

    VertexExtra* ve = aligned_vector_at(target->extras, 0);
    Vertex* it = start;

    const float w = 1.0f;

    uint32_t i = count;

    while(i--) {
        it->flags = GPU_CMD_VERTEX;

        if(pos) {
            TransformVertex((const float*) pos, &w, it->xyz, &it->w);
            pos += vstride;
        } else {
            *((Float3*) it->xyz) = F3ZERO;
        }


        if(uv) {
            MEMCPY4(it->uv, uv, sizeof(float) * 2);
            uv += uvstride;
        } else {
            *((Float2*) it->uv) = F2ZERO;
        }

        if(col) {
            MEMCPY4(it->bgra, col, sizeof(uint32_t));
            col += dstride;
        } else {
            *((uint32_t*) it->bgra) = ~0;
        }

        if(st) {
            MEMCPY4(ve->st, st, sizeof(float) * 2);
            st += ststride;
        } else {
            *((Float2*) ve->st) = F2ZERO;
        }

        if(n) {
            MEMCPY4(ve->nxyz, n, sizeof(float) * 3);
            n += nstride;
        } else {
            *((Float3*) ve->nxyz) = F3Z;
        }

        it++;
        ve++;
    }
}

static void generateArrays(SubmissionTarget* target, const GLsizei first, const GLuint count) {
    Vertex* start = _glSubmissionTargetStart(target);
    VertexExtra* ve = aligned_vector_at(target->extras, 0);

    ReadPositionFunc pfunc = calcReadPositionFunc();
    ReadDiffuseFunc dfunc = calcReadDiffuseFunc();
    ReadUVFunc uvfunc = calcReadUVFunc();
    ReadNormalFunc nfunc = calcReadNormalFunc();
    ReadUVFunc stfunc = calcReadSTFunc();

    _readPositionData(pfunc, first, count, start);
    _readDiffuseData(dfunc, first, count, start);
    _readUVData(uvfunc, first, count, start);
    _readNormalData(nfunc, first, count, ve);
    _readSTData(stfunc, first, count, ve);
}

static void generate(SubmissionTarget* target, const GLenum mode, const GLsizei first, const GLuint count,
        const GLubyte* indices, const GLenum type) {
    /* Read from the client buffers and generate an array of ClipVertices */
    TRACE();

    if(FAST_PATH_ENABLED) {
        if(indices) {
            generateElementsFastPath(target, first, count, indices, type);
        } else {
            generateArraysFastPath(target, first, count);
        }
    } else {
        if(indices) {
            generateElements(target, first, count, indices, type);
        } else {
            generateArrays(target, first, count);
        }
    }

    Vertex* it = _glSubmissionTargetStart(target);
    // Drawing arrays
    switch(mode) {
    case GL_TRIANGLES:
        genTriangles(it, count);
        break;
    case GL_QUADS:
        genQuads(it, count);
        break;
    case GL_TRIANGLE_FAN:
        genTriangleFan(it, count);
        break;
    case GL_TRIANGLE_STRIP:
        genTriangleStrip(it, count);
        break;
    default:
        assert(0 && "Not Implemented");
    }
}

static void transform(SubmissionTarget* target) {
    TRACE();

    /* Perform modelview transform, storing W */
    Vertex* vertex = _glSubmissionTargetStart(target);

    TransformVertices(vertex, target->count);
}

static void clip(SubmissionTarget* target) {
    TRACE();

    /* Perform clipping, generating new vertices as necessary */
    _glClipTriangleStrip(target, _glGetShadeModel() == GL_FLAT);

    /* Reset the count now that we may have added vertices */
    target->count = target->output->vector.size - target->start_offset;
}

static void mat_transform3(const float* xyz, const float* xyzOut, const uint32_t count, const uint32_t inStride, const uint32_t outStride) {
    const uint8_t* dataIn = (const uint8_t*) xyz;
    uint8_t* dataOut = (uint8_t*) xyzOut;

    ITERATE(count) {
        const float* in = (const float*) dataIn;
        float* out = (float*) dataOut;

        TransformVec3NoMod(
            in,
            out
        );

        dataIn += inStride;
        dataOut += outStride;
    }
}

static void mat_transform_normal3(const float* xyz, const float* xyzOut, const uint32_t count, const uint32_t inStride, const uint32_t outStride) {
    const uint8_t* dataIn = (const uint8_t*) xyz;
    uint8_t* dataOut = (uint8_t*) xyzOut;

    ITERATE(count) {
        const float* in = (const float*) dataIn;
        float* out = (float*) dataOut;

        TransformNormalNoMod(in, out);

        dataIn += inStride;
        dataOut += outStride;
    }
}

static void light(SubmissionTarget* target) {

    static AlignedVector* eye_space_data = NULL;

    if(!eye_space_data) {
        eye_space_data = (AlignedVector*) malloc(sizeof(AlignedVector));
        aligned_vector_init(eye_space_data, sizeof(EyeSpaceData));
    }

    aligned_vector_resize(eye_space_data, target->count);

    /* Perform lighting calculations and manipulate the colour */
    Vertex* vertex = _glSubmissionTargetStart(target);
    VertexExtra* extra = aligned_vector_at(target->extras, 0);
    EyeSpaceData* eye_space = (EyeSpaceData*) eye_space_data->data;

    _glMatrixLoadNormal();
    mat_transform_normal3(extra->nxyz, eye_space->n, target->count, sizeof(VertexExtra), sizeof(EyeSpaceData));

    EyeSpaceData* ES = aligned_vector_at(eye_space_data, 0);
    _glPerformLighting(vertex, ES, target->count);
}

GL_FORCE_INLINE void divide(SubmissionTarget* target) {
    TRACE();

    /* Perform perspective divide on each vertex */
    Vertex* vertex = _glSubmissionTargetStart(target);

    const float h = GetVideoMode()->height;

    ITERATE(target->count) {
        const float f = MATH_Fast_Invert(vertex->w);

        /* Convert to NDC and apply viewport */
        vertex->xyz[0] = MATH_fmac(
            VIEWPORT.hwidth, vertex->xyz[0] * f, VIEWPORT.x_plus_hwidth
        );
        vertex->xyz[1] = h - MATH_fmac(
            VIEWPORT.hheight, vertex->xyz[1] * f, VIEWPORT.y_plus_hheight
        );

        /* Apply depth range */
        vertex->xyz[2] = MAX(
            1.0f - MATH_fmac(vertex->xyz[2] * f, 0.5f, 0.5f),
            PVR_MIN_Z
        );

        ++vertex;
    }
}

GL_FORCE_INLINE void push(PolyHeader* header, GLboolean multiTextureHeader, PolyList* activePolyList, GLshort textureUnit) {
    TRACE();

    // Compile the header
    PolyContext cxt = *_glGetPVRContext();
    cxt.list_type = activePolyList->list_type;

    _glUpdatePVRTextureContext(&cxt, textureUnit);

    if(multiTextureHeader) {
        assert(cxt.list_type == GPU_LIST_TR_POLY);

        cxt.gen.alpha = GPU_ALPHA_ENABLE;
        cxt.txr.alpha = GPU_TXRALPHA_ENABLE;
        cxt.blend.src = GPU_BLEND_ZERO;
        cxt.blend.dst = GPU_BLEND_DESTCOLOR;
        cxt.depth.comparison = GPU_DEPTHCMP_EQUAL;
    }

    CompilePolyHeader(header, &cxt);

    /* Post-process the vertex list */
    /*
     * This is currently unnecessary. aligned_vector memsets the allocated objects
     * to zero, and we don't touch oargb, also, we don't *enable* oargb yet in the
     * pvr header so it should be ignored anyway. If this ever becomes a problem,
     * uncomment this.
    ClipVertex* vout = output;
    const ClipVertex* end = output + count;
    while(vout < end) {
        vout->oargb = 0;
    }
    */
}

#define DEBUG_CLIPPING 0

GL_FORCE_INLINE void submitVertices(GLenum mode, GLsizei first, GLuint count, GLenum type, const GLvoid* indices) {
    TRACE();

    /* Do nothing if vertices aren't enabled */
    if(!(ENABLED_VERTEX_ATTRIBUTES & VERTEX_ENABLED_FLAG)) {
        return;
    }

    /* No vertices? Do nothing */
    if(!count) {
        return;
    }

    if(mode == GL_LINE_STRIP || mode == GL_LINES) {
        fprintf(stderr, "Line drawing is currently unsupported\n");
        return;
    }

    static SubmissionTarget* target = NULL;
    static AlignedVector extras;

    /* Initialization of the target and extras */
    if(!target) {
        target = (SubmissionTarget*) malloc(sizeof(SubmissionTarget));
        target->extras = NULL;
        target->count = 0;
        target->output = NULL;
        target->header_offset = target->start_offset = 0;

        aligned_vector_init(&extras, sizeof(VertexExtra));
        target->extras = &extras;
    }

    GLboolean doMultitexture, doTexture, doLighting;
    GLint activeTexture;
    glGetIntegerv(GL_ACTIVE_TEXTURE_ARB, &activeTexture);

    glActiveTextureARB(GL_TEXTURE0);
    glGetBooleanv(GL_TEXTURE_2D, &doTexture);

    glActiveTextureARB(GL_TEXTURE1);
    glGetBooleanv(GL_TEXTURE_2D, &doMultitexture);

    doLighting = _glIsLightingEnabled();

    glActiveTextureARB(activeTexture);

    /* Polygons are treated as triangle fans, the only time this would be a
     * problem is if we supported glPolygonMode(..., GL_LINE) but we don't.
     * We optimise the triangle and quad cases.
     */
    if(mode == GL_POLYGON) {
        if(count == 3) {
            mode = GL_TRIANGLES;
        } else if(count == 4) {
            mode = GL_QUADS;
        } else {
            mode = GL_TRIANGLE_FAN;
        }
    }

    // We don't handle this any further, so just make sure we never pass it down */
    assert(mode != GL_POLYGON);

    target->output = _glActivePolyList();
    target->count = (mode == GL_TRIANGLE_FAN) ? ((count - 2) * 3) : count;
    target->header_offset = target->output->vector.size;
    target->start_offset = target->header_offset + 1;

    assert(target->count);

    /* Make sure we have enough room for all the "extra" data */
    aligned_vector_resize(&extras, target->count);

    /* Make room for the vertices and header */
    aligned_vector_extend(&target->output->vector, target->count + 1);

    /* If we're lighting, then we need to do some work in
     * eye-space, so we only transform vertices by the modelview
     * matrix, and then later multiply by projection.
     *
     * If we're not doing lighting though we can optimise by taking
     * vertices straight to clip-space */

    if(doLighting) {
        _glMatrixLoadModelView();
    } else {
        _glMatrixLoadModelViewProjection();
    }

    /* If we're FAST_PATH_ENABLED, then this will do the transform for us */
    generate(target, mode, first, count, (GLubyte*) indices, type);

    /* No fast path, then we have to do another iteration :( */
    if(!FAST_PATH_ENABLED) {
        /* Multiply by modelview */
        transform(target);
    }

    if(doLighting){
        light(target);

        /* OK eye-space work done, now move into clip space */
        _glMatrixLoadProjection();
        transform(target);
    }

    if(_glIsClippingEnabled()) {
#if DEBUG_CLIPPING
        uint32_t i = 0;
        fprintf(stderr, "=========\n");

        for(i = 0; i < target->count; ++i) {
            Vertex* v = aligned_vector_at(&target->output->vector, target->start_offset + i);
            if(v->flags == 0xe0000000 || v->flags == 0xf0000000) {
                fprintf(stderr, "(%f, %f, %f, %f) -> %x\n", v->xyz[0], v->xyz[1], v->xyz[2], v->w, v->flags);
            } else {
                fprintf(stderr, "%x\n", *((uint32_t*)v));
            }
        }
#endif

        clip(target);

        assert(extras.size == target->count);

#if DEBUG_CLIPPING
        fprintf(stderr, "--------\n");
        for(i = 0; i < target->count; ++i) {
            Vertex* v = aligned_vector_at(&target->output->vector, target->start_offset + i);
            if(v->flags == 0xe0000000 || v->flags == 0xf0000000) {
                fprintf(stderr, "(%f, %f, %f, %f) -> %x\n", v->xyz[0], v->xyz[1], v->xyz[2], v->w, v->flags);
            } else {
                fprintf(stderr, "%x\n", *((uint32_t*)v));
            }
        }
#endif

    }

    push(_glSubmissionTargetHeader(target), GL_FALSE, target->output, 0);

    /*
       Now, if multitexturing is enabled, we want to send exactly the same vertices again, except:
       - We want to enable blending, and send them to the TR list
       - We want to set the depth func to GL_EQUAL
       - We want to set the second texture ID
       - We want to set the uv coordinates to the passed st ones
    */

    if(!doMultitexture) {
        /* Multitexture actively disabled */
        return;
    }

    TextureObject* texture1 = _glGetTexture1();

    /* Multitexture implicitly disabled */
    if(!texture1 || ((ENABLED_VERTEX_ATTRIBUTES & ST_ENABLED_FLAG) != ST_ENABLED_FLAG)) {
        /* Multitexture actively disabled */
        return;
    }

    /* Push back a copy of the list to the transparent poly list, including the header
        (hence the + 1)
    */
    Vertex* vertex = aligned_vector_push_back(
        &_glTransparentPolyList()->vector, (Vertex*) _glSubmissionTargetHeader(target), target->count + 1
    );

    assert(vertex);

    PolyHeader* mtHeader = (PolyHeader*) vertex++;

    /* Replace the UV coordinates with the ST ones */
    VertexExtra* ve = aligned_vector_at(target->extras, 0);
    ITERATE(target->count) {
        vertex->uv[0] = ve->st[0];
        vertex->uv[1] = ve->st[1];
        ++vertex;
        ++ve;
    }

    /* Send the buffer again to the transparent list */
    push(mtHeader, GL_TRUE, _glTransparentPolyList(), 1);
}

void APIENTRY glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices) {
    TRACE();

    if(_glCheckImmediateModeInactive(__func__)) {
        return;
    }
    _glRecalcFastPath();

    submitVertices(mode, 0, count, type, indices);
}

void APIENTRY glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    TRACE();

    if(_glCheckImmediateModeInactive(__func__)) {
        return;
    }
    _glRecalcFastPath();

    submitVertices(mode, first, count, GL_UNSIGNED_INT, NULL);
}

void APIENTRY glEnableClientState(GLenum cap) {
    TRACE();

    switch(cap) {
    case GL_VERTEX_ARRAY:
        ENABLED_VERTEX_ATTRIBUTES |= VERTEX_ENABLED_FLAG;
    break;
    case GL_COLOR_ARRAY:
        ENABLED_VERTEX_ATTRIBUTES |= DIFFUSE_ENABLED_FLAG;
    break;
    case GL_NORMAL_ARRAY:
        ENABLED_VERTEX_ATTRIBUTES |= NORMAL_ENABLED_FLAG;
    break;
    case GL_TEXTURE_COORD_ARRAY:
        (ACTIVE_CLIENT_TEXTURE) ?
            (ENABLED_VERTEX_ATTRIBUTES |= ST_ENABLED_FLAG):
            (ENABLED_VERTEX_ATTRIBUTES |= UV_ENABLED_FLAG);
    break;
    default:
        _glKosThrowError(GL_INVALID_ENUM, __func__);
    }
}

void APIENTRY glDisableClientState(GLenum cap) {
    TRACE();

    switch(cap) {
    case GL_VERTEX_ARRAY:
        ENABLED_VERTEX_ATTRIBUTES &= ~VERTEX_ENABLED_FLAG;
    break;
    case GL_COLOR_ARRAY:
        ENABLED_VERTEX_ATTRIBUTES &= ~DIFFUSE_ENABLED_FLAG;
    break;
    case GL_NORMAL_ARRAY:
        ENABLED_VERTEX_ATTRIBUTES &= ~NORMAL_ENABLED_FLAG;
    break;
    case GL_TEXTURE_COORD_ARRAY:
        (ACTIVE_CLIENT_TEXTURE) ?
            (ENABLED_VERTEX_ATTRIBUTES &= ~ST_ENABLED_FLAG):
            (ENABLED_VERTEX_ATTRIBUTES &= ~UV_ENABLED_FLAG);
    break;
    default:
        _glKosThrowError(GL_INVALID_ENUM, __func__);
    }
}

GLuint _glGetActiveClientTexture() {
    return ACTIVE_CLIENT_TEXTURE;
}

void APIENTRY glClientActiveTextureARB(GLenum texture) {
    TRACE();

    if(texture < GL_TEXTURE0_ARB || texture > GL_TEXTURE0_ARB + MAX_TEXTURE_UNITS) {
        _glKosThrowError(GL_INVALID_ENUM, __func__);
    }

    if(_glKosHasError()) {
        _glKosPrintError();
        return;
    }

    ACTIVE_CLIENT_TEXTURE = (texture == GL_TEXTURE1_ARB) ? 1 : 0;
}

GLboolean _glRecalcFastPath() {
    FAST_PATH_ENABLED = _glIsVertexDataFastPathCompatible();
    return FAST_PATH_ENABLED;
}

void APIENTRY glTexCoordPointer(GLint size,  GLenum type,  GLsizei stride,  const GLvoid * pointer) {
    TRACE();

    if(size < 1 || size > 4) {
        _glKosThrowError(GL_INVALID_VALUE, __func__);
        _glKosPrintError();
        return;
    }

    AttribPointer* tointer = (ACTIVE_CLIENT_TEXTURE == 0) ? &UV_POINTER : &ST_POINTER;

    tointer->ptr = pointer;
    tointer->stride = stride;
    tointer->type = type;
    tointer->size = size;
}

void APIENTRY glVertexPointer(GLint size,  GLenum type,  GLsizei stride,  const GLvoid * pointer) {
    TRACE();

    if(size < 2 || size > 4) {
        _glKosThrowError(GL_INVALID_VALUE, __func__);
        _glKosPrintError();
        return;
    }

    VERTEX_POINTER.ptr = pointer;
    VERTEX_POINTER.stride = stride;
    VERTEX_POINTER.type = type;
    VERTEX_POINTER.size = size;
}

void APIENTRY glColorPointer(GLint size,  GLenum type,  GLsizei stride,  const GLvoid * pointer) {
    TRACE();

    if(size != 3 && size != 4 && size != GL_BGRA) {
        _glKosThrowError(GL_INVALID_VALUE, __func__);
        _glKosPrintError();
        return;
    }

    DIFFUSE_POINTER.ptr = pointer;
    DIFFUSE_POINTER.stride = stride;
    DIFFUSE_POINTER.type = type;
    DIFFUSE_POINTER.size = size;
}

void APIENTRY glNormalPointer(GLenum type,  GLsizei stride,  const GLvoid * pointer) {
    TRACE();

    GLint validTypes[] = {
        GL_DOUBLE,
        GL_FLOAT,
        GL_BYTE,
        GL_UNSIGNED_BYTE,
        GL_INT,
        GL_UNSIGNED_INT,
        GL_UNSIGNED_INT_2_10_10_10_REV,
        0
    };

    if(_glCheckValidEnum(type, validTypes, __func__) != 0) {
        return;
    }

    NORMAL_POINTER.ptr = pointer;
    NORMAL_POINTER.stride = stride;
    NORMAL_POINTER.type = type;
    NORMAL_POINTER.size = (type == GL_UNSIGNED_INT_2_10_10_10_REV) ? 1 : 3;

}
