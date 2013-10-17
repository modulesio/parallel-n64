/*
* Glide64 - Glide video plugin for Nintendo 64 emulators.
* Copyright (c) 2002  Dave2001
* Copyright (c) 2003-2009  Sergey 'Gonetz' Lipski
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "glide.h"
#include "g3ext.h"
#include "main.h"

// FXT1,DXT1,DXT5 support - Hiroshi Morii <koolsmoky(at)users.sourceforge.net>
// NOTE: Glide64 + GlideHQ use the following formats
// GL_COMPRESSED_RGB_S3TC_DXT1_EXT
// GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
// GL_COMPRESSED_RGB_FXT1_3DFX
// GL_COMPRESSED_RGBA_FXT1_3DFX

typedef struct
{
  unsigned int address;
  int width;
  int height;
  unsigned int fbid;
  unsigned int zbid;
  unsigned int texid;
  int buff_clear;
} fb;

int nbTextureUnits;
int nbAuxBuffers, current_buffer;
int width, widtho, heighto, height;
int saved_width, saved_height;
int blend_func_separate_support;
int npot_support;
int fog_coord_support;
int render_to_texture = 0;
int texture_unit;
int buffer_cleared;
// ZIGGY
// to allocate a new static texture name, take the value (free_texture++)
int free_texture;
int default_texture; // the infamous "32*1024*1024" is now configurable
int current_texture;
int depth_texture, color_texture;
int glsl_support = 1;
int viewport_width, viewport_height;
int lfb_color_fmt;
float invtex[2];
//Gonetz

static int savedWidtho, savedHeighto;
static int savedWidth, savedHeight;
unsigned int pBufferAddress;
static int pBufferFmt;
static int pBufferWidth, pBufferHeight;
static fb fbs[100];
static int nb_fb = 0;
static unsigned int curBufferAddr = 0;

struct TMU_USAGE { int min, max; } tmu_usage[2] = { {0xfffffff, 0}, {0xfffffff, 0} };

struct texbuf_t {
  FxU32 start, end;
  int fmt;
};
#define NB_TEXBUFS 128 // MUST be a power of two
static texbuf_t texbufs[NB_TEXBUFS];
static int texbuf_i;

unsigned short frameBuffer[2048*2048];
unsigned short depthBuffer[2048*2048];

//#define VOODOO1

void display_warning(const char *text, ...)
{
  static int first_message = 100;
  if (first_message)
  {
    char buf[4096];

    va_list ap;

    va_start(ap, text);
    vsprintf(buf, text, ap);
    va_end(ap);
    first_message--;
    LOGINFO(buf);
  }
}

FX_ENTRY void FX_CALL
grSstOrigin(GrOriginLocation_t  origin)
{
  LOG("grSstOrigin(%d)\r\n", origin);
  if (origin != GR_ORIGIN_UPPER_LEFT)
    display_warning("grSstOrigin : %x", origin);
}

FX_ENTRY void FX_CALL
grClipWindow( FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy )
{
  LOG("grClipWindow(%d,%d,%d,%d)\r\n", minx, miny, maxx, maxy);

  GLint y =  height - maxy;

  if (render_to_texture)
  {
    if (int(minx) < 0) minx = 0;
    if (int(miny) < 0) miny = 0;
    if (maxx < minx) maxx = minx;
    if (maxy < miny) maxy = miny;
    y = miny;
  }

  glScissor(minx, y, maxx - minx, maxy - miny);
  glEnable(GL_SCISSOR_TEST);
}

FX_ENTRY void FX_CALL
grColorMask( FxBool rgb, FxBool a )
{
  LOG("grColorMask(%d, %d)\r\n", rgb, a);
  glColorMask(rgb, rgb, rgb, a);
}

FX_ENTRY void FX_CALL
grGlideInit( void )
{
  LOG("grGlideInit()\r\n");
}

FX_ENTRY void FX_CALL
grSstSelect( int which_sst )
{
  LOG("grSstSelect(%d)\r\n", which_sst);
}

int isExtensionSupported(const char *extension)
{
  return 0;
  const GLubyte *extensions = NULL;
  const GLubyte *start;
  GLubyte *where, *terminator;

  where = (GLubyte *)strchr(extension, ' ');
  if (where || *extension == '\0')
    return 0;

  extensions = glGetString(GL_EXTENSIONS);

  start = extensions;
  for (;;)
  {
    where = (GLubyte *) strstr((const char *) start, extension);
    if (!where)
      break;

    terminator = where + strlen(extension);
    if (where == start || *(where - 1) == ' ')
      if (*terminator == ' ' || *terminator == '\0')
        return 1;

    start = terminator;
  }

  return 0;
}

#define GrPixelFormat_t int

FX_ENTRY GrContext_t FX_CALL
grSstWinOpenExt(
                HWND                 hWnd,
                GrScreenResolution_t screen_resolution,
                GrScreenRefresh_t    refresh_rate,
                GrColorFormat_t      color_format,
                GrOriginLocation_t   origin_location,
                GrPixelFormat_t      pixelformat,
                int                  nColBuffers,
                int                  nAuxBuffers)
{
  LOG("grSstWinOpenExt(%d, %d, %d, %d, %d, %d %d)\r\n", hWnd, screen_resolution, refresh_rate, color_format, origin_location, nColBuffers, nAuxBuffers);
  return grSstWinOpen(hWnd, screen_resolution, refresh_rate, color_format,
    origin_location, nColBuffers, nAuxBuffers);
}

FX_ENTRY GrContext_t FX_CALL
grSstWinOpen(
             HWND                 hWnd,
             GrScreenResolution_t screen_resolution,
             GrScreenRefresh_t    refresh_rate,
             GrColorFormat_t      color_format,
             GrOriginLocation_t   origin_location,
             int                  nColBuffers,
             int                  nAuxBuffers)
{
  // ZIGGY
  // allocate static texture names
  // the initial value should be big enough to support the maximal resolution
  free_texture = 32*2048*2048;
  default_texture = free_texture++;
  color_texture = free_texture++;
  depth_texture = free_texture++;

  LOG("grSstWinOpen(%08lx, %d, %d, %d, %d, %d %d)\r\n", hWnd, screen_resolution&~0x80000000, refresh_rate, color_format, origin_location, nColBuffers, nAuxBuffers);

  width = height = 0;

  m64p_handle video_general_section;
  printf("&ConfigOpenSection is %p\n", &ConfigOpenSection);
  if (ConfigOpenSection("Video-General", &video_general_section) != M64ERR_SUCCESS)
  {
    printf("Could not open video settings");
    return false;
  }
  width = ConfigGetParamInt(video_general_section, "ScreenWidth");
  height = ConfigGetParamInt(video_general_section, "ScreenHeight");

  glViewport(0, 0, width, height);
  lfb_color_fmt = color_format;
  if (origin_location != GR_ORIGIN_UPPER_LEFT) display_warning("origin must be in upper left corner");
  if (nColBuffers != 2) display_warning("number of color buffer is not 2");
  if (nAuxBuffers != 1) display_warning("number of auxiliary buffer is not 1");

  if (isExtensionSupported("GL_ARB_texture_env_combine") == 0 &&
    isExtensionSupported("GL_EXT_texture_env_combine") == 0)
    display_warning("Your video card doesn't support GL_ARB_texture_env_combine extension");
  if (isExtensionSupported("GL_ARB_multitexture") == 0)
    display_warning("Your video card doesn't support GL_ARB_multitexture extension");
  if (isExtensionSupported("GL_ARB_texture_mirrored_repeat") == 0)
    display_warning("Your video card doesn't support GL_ARB_texture_mirrored_repeat extension");

  nbTextureUnits = 4;
  //glGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB, &nbTextureUnits);
  if (nbTextureUnits == 1) display_warning("You need a video card that has at least 2 texture units");

  nbAuxBuffers = 4;
  //glGetIntegerv(GL_AUX_BUFFERS, &nbAuxBuffers);
  if (nbAuxBuffers > 0)
    printf("Congratulations, you have %d auxilliary buffers, we'll use them wisely !\n", nbAuxBuffers);
#ifdef VOODOO1
  nbTextureUnits = 2;
#endif

  blend_func_separate_support = 1;
  packed_pixels_support = 0;
/*
  if (isExtensionSupported("GL_EXT_blend_func_separate") == 0)
    blend_func_separate_support = 0;
  else
    blend_func_separate_support = 1;

  if (isExtensionSupported("GL_EXT_packed_pixels") == 0)
    packed_pixels_support = 0;
  else {
    printf("packed pixels extension used\n");
    packed_pixels_support = 1;
  }
*/

  if (isExtensionSupported("GL_ARB_texture_non_power_of_two") == 0)
    npot_support = 0;
  else {
    printf("NPOT extension used\n");
    npot_support = 1;
  }

  if (isExtensionSupported("GL_EXT_fog_coord") == 0)
    fog_coord_support = 0;
  else
    fog_coord_support = 1;

  if (isExtensionSupported("GL_ARB_shading_language_100") &&
    isExtensionSupported("GL_ARB_shader_objects") &&
    isExtensionSupported("GL_ARB_fragment_shader") &&
    isExtensionSupported("GL_ARB_vertex_shader"))
  {}

  if (isExtensionSupported("GL_EXT_texture_compression_s3tc") == 0 )
    display_warning("Your video card doesn't support GL_EXT_texture_compression_s3tc extension");
  if (isExtensionSupported("GL_3DFX_texture_compression_FXT1") == 0)
    display_warning("Your video card doesn't support GL_3DFX_texture_compression_FXT1 extension");

  glViewport(0, 0, width, height);
  viewport_width = width;
  viewport_height = height;

  // VP try to resolve z precision issues
//  glMatrixMode(GL_MODELVIEW);
//  glLoadIdentity();
//  glTranslatef(0, 0, 1-zscale);
//  glScalef(1, 1, zscale);

  widtho = width/2;
  heighto = height/2;

  pBufferWidth = pBufferHeight = -1;

  current_buffer = GL_BACK;

  texture_unit = GL_TEXTURE0;

  {
    int i;
    for (i=0; i<NB_TEXBUFS; i++)
      texbufs[i].start = texbufs[i].end = 0xffffffff;
  }

  void FindBestDepthBias();
  FindBestDepthBias();

  init_geometry();
  init_textures();
  init_combiner();

  return 1;
}

FX_ENTRY void FX_CALL
grGlideShutdown( void )
{
  LOG("grGlideShutdown\r\n");
}

FX_ENTRY FxBool FX_CALL
grSstWinClose( GrContext_t context )
{
  int i;
  LOG("grSstWinClose(%d)\r\n", context);

  for (i=0; i<2; i++) {
    tmu_usage[i].min = 0xfffffff;
    tmu_usage[i].max = 0;
    invtex[i] = 0;
  }

  free_combiners();
  sglBindFramebuffer( GL_FRAMEBUFFER, 0 );

  {
    for (i=0; i<nb_fb; i++)
    {
      glDeleteTextures( 1, &(fbs[i].texid) );
      glDeleteFramebuffers( 1, &(fbs[i].fbid) );
      glDeleteRenderbuffers( 1, &(fbs[i].zbid) );
    }
  }
  nb_fb = 0;

  free_textures();
  remove_tex(0, 0xfffffff);

  return FXTRUE;
}

FX_ENTRY void FX_CALL grTextureBufferExt( GrChipID_t  		tmu,
                                         FxU32 				startAddress,
                                         GrLOD_t 			lodmin,
                                         GrLOD_t 			lodmax,
                                         GrAspectRatio_t 	aspect,
                                         GrTextureFormat_t 	fmt,
                                         FxU32 				evenOdd)
{
  int i;
  static int fbs_init = 0;

  //printf("grTextureBufferExt(%d, %d, %d, %d, %d, %d, %d)\r\n", tmu, startAddress, lodmin, lodmax, aspect, fmt, evenOdd);
  LOG("grTextureBufferExt(%d, %d, %d, %d %d, %d, %d)\r\n", tmu, startAddress, lodmin, lodmax, aspect, fmt, evenOdd);
  if (lodmin != lodmax)display_warning("grTextureBufferExt : loading more than one LOD");
  {
    if (!render_to_texture) //initialization
    {
      if(!fbs_init)
      {
        for(i=0; i<100; i++) fbs[i].address = 0;
        fbs_init = 1;
        nb_fb = 0;
      }
      return; //no need to allocate FBO if render buffer is not texture buffer
    }

    render_to_texture = 2;

    if (aspect < 0)
    {
      pBufferHeight = 1 << lodmin;
      pBufferWidth = pBufferHeight >> -aspect;
    }
    else
    {
      pBufferWidth = 1 << lodmin;
      pBufferHeight = pBufferWidth >> aspect;
    }
    pBufferAddress = startAddress+1;

    width = pBufferWidth;
    height = pBufferHeight;

    widtho = width/2;
    heighto = height/2;

    for (i=0; i<nb_fb; i++)
    {
      if (fbs[i].address == pBufferAddress)
      {
        if (fbs[i].width == width && fbs[i].height == height) //select already allocated FBO
        {
          sglBindFramebuffer( GL_FRAMEBUFFER, fbs[i].fbid );
          glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbs[i].texid, 0 );
          glBindRenderbuffer( GL_RENDERBUFFER, fbs[i].zbid );
          glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fbs[i].zbid );
          glViewport( 0, 0, width, height);
          glScissor( 0, 0, width, height);
          if (fbs[i].buff_clear)
          {
            glDepthMask(1);
            glClear( GL_DEPTH_BUFFER_BIT ); //clear z-buffer only. we may need content, stored in the frame buffer
            fbs[i].buff_clear = 0;
          }
          CHECK_FRAMEBUFFER_STATUS();
          curBufferAddr = pBufferAddress;
          return;
        }
        else //create new FBO at the same address, delete old one
        {
          glDeleteFramebuffers( 1, &(fbs[i].fbid) );
          glDeleteRenderbuffers( 1, &(fbs[i].zbid) );
          glDeleteTextures(1, &(fbs[i].texid));
          if (nb_fb > 1)
            memmove(&(fbs[i]), &(fbs[i+1]), sizeof(fb)*(nb_fb-i));
          nb_fb--;
          break;
        }
      }
    }

    remove_tex(pBufferAddress, pBufferAddress + width*height*2/*grTexFormatSize(fmt)*/);
    //create new FBO
    glGenFramebuffers( 1, &(fbs[nb_fb].fbid) );
    glGenRenderbuffers( 1, &(fbs[nb_fb].zbid) );
    glBindRenderbuffer( GL_RENDERBUFFER, fbs[nb_fb].zbid );
    glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);
    fbs[nb_fb].texid = sglAddTextureMap(pBufferAddress);
    fbs[nb_fb].address = pBufferAddress;
    fbs[nb_fb].width = width;
    fbs[nb_fb].height = height;
    fbs[nb_fb].buff_clear = 0;
    add_tex(fbs[nb_fb].address);
    glBindTexture(GL_TEXTURE_2D, fbs[nb_fb].texid);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
      GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    glBindTexture(GL_TEXTURE_2D, 0);

    sglBindFramebuffer( GL_FRAMEBUFFER, fbs[nb_fb].fbid);
    glFramebufferTexture2D(GL_FRAMEBUFFER,
      GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbs[nb_fb].texid, 0);
    glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fbs[nb_fb].zbid );
    glViewport(0,0,width,height);
    glScissor(0,0,width,height);
    glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
    glDepthMask(1);
    glClear( GL_DEPTH_BUFFER_BIT );
    CHECK_FRAMEBUFFER_STATUS();
    curBufferAddr = pBufferAddress;
    nb_fb++;
  }
}

int CheckTextureBufferFormat(GrChipID_t tmu, FxU32 startAddress, GrTexInfo *info )
{
  int found, i;
  {
    found = i = 0;
    while (i < nb_fb)
    {
      unsigned int end = fbs[i].address + fbs[i].width*fbs[i].height*2;
      if (startAddress >= fbs[i].address &&  startAddress < end)
      {
        found = 1;
        break;
      }
      i++;
    }
  }

  invtex[tmu] = 0;

  if (info->format == GR_TEXFMT_ALPHA_INTENSITY_88 ) {
    if (!found) {
      return 0;
    }
    if(tmu == 0)
    {
      if(blackandwhite1 != found)
      {
        blackandwhite1 = found;
        need_to_compile = 1;
      }
    }
    else
    {
      if(blackandwhite0 != found)
      {
        blackandwhite0 = found;
        need_to_compile = 1;
      }
    }
    return 1;
  }
  return 0;

}


FX_ENTRY void FX_CALL
grTextureAuxBufferExt( GrChipID_t tmu,
                      FxU32      startAddress,
                      GrLOD_t    thisLOD,
                      GrLOD_t    largeLOD,
                      GrAspectRatio_t aspectRatio,
                      GrTextureFormat_t format,
                      FxU32      odd_even_mask )
{
  LOG("grTextureAuxBufferExt(%d, %d, %d, %d %d, %d, %d)\r\n", tmu, startAddress, thisLOD, largeLOD, aspectRatio, format, odd_even_mask);
  //display_warning("grTextureAuxBufferExt");
}

FX_ENTRY void FX_CALL grAuxBufferExt( GrBuffer_t buffer );

FX_ENTRY GrProc FX_CALL
grGetProcAddress( char *procName )
{
  LOG("grGetProcAddress(%s)\r\n", procName);
  if(!strcmp(procName, "grSstWinOpenExt"))
    return (GrProc)grSstWinOpenExt;
  if(!strcmp(procName, "grTextureBufferExt"))
    return (GrProc)grTextureBufferExt;
  if(!strcmp(procName, "grChromaRangeExt"))
    return (GrProc)grChromaRangeExt;
  if(!strcmp(procName, "grChromaRangeModeExt"))
    return (GrProc)grChromaRangeModeExt;
  if(!strcmp(procName, "grTexChromaRangeExt"))
    return (GrProc)grTexChromaRangeExt;
  if(!strcmp(procName, "grTexChromaModeExt"))
    return (GrProc)grTexChromaModeExt;
  // ZIGGY framebuffer copy extension
  if(!strcmp(procName, "grFramebufferCopyExt"))
    return (GrProc)grFramebufferCopyExt;
  if(!strcmp(procName, "grColorCombineExt"))
    return (GrProc)grColorCombineExt;
  if(!strcmp(procName, "grAlphaCombineExt"))
    return (GrProc)grAlphaCombineExt;
  if(!strcmp(procName, "grTexColorCombineExt"))
    return (GrProc)grTexColorCombineExt;
  if(!strcmp(procName, "grTexAlphaCombineExt"))
    return (GrProc)grTexAlphaCombineExt;
  if(!strcmp(procName, "grConstantColorValueExt"))
    return (GrProc)grConstantColorValueExt;
  if(!strcmp(procName, "grTextureAuxBufferExt"))
    return (GrProc)grTextureAuxBufferExt;
  if(!strcmp(procName, "grAuxBufferExt"))
    return (GrProc)grAuxBufferExt;
  if(!strcmp(procName, "grWrapperFullScreenResolutionExt"))
    return (GrProc)grWrapperFullScreenResolutionExt;
  if(!strcmp(procName, "grConfigWrapperExt"))
    return (GrProc)grConfigWrapperExt;
  if(!strcmp(procName, "grGetGammaTableExt"))
    return (GrProc)grGetGammaTableExt;
  display_warning("grGetProcAddress : %s", procName);
  return 0;
}

FX_ENTRY FxU32 FX_CALL
grGet( FxU32 pname, FxU32 plength, FxI32 *params )
{
  LOG("grGet(%d,%d)\r\n", pname, plength);
  switch(pname)
  {
  case GR_MAX_TEXTURE_SIZE:
    if (plength < 4 || params == NULL) return 0;
    params[0] = 2048;
    return 4;
    break;
  case GR_NUM_TMU:
    if (plength < 4 || params == NULL) return 0;
    if (!nbTextureUnits)
    {
      grSstWinOpen((unsigned long)NULL, GR_RESOLUTION_640x480 | 0x80000000, 0, GR_COLORFORMAT_ARGB,
        GR_ORIGIN_UPPER_LEFT, 2, 1);
      grSstWinClose(0);
    }
#ifdef VOODOO1
    params[0] = 1;
#else
    if (nbTextureUnits > 2)
      params[0] = 2;
    else
      params[0] = 1;
#endif
    return 4;
    break;
  case GR_NUM_BOARDS:
  case GR_NUM_FB:
  case GR_REVISION_FB:
  case GR_REVISION_TMU:
    if (plength < 4 || params == NULL) return 0;
    params[0] = 1;
    return 4;
    break;
  case GR_MEMORY_FB:
    if (plength < 4 || params == NULL) return 0;
    params[0] = 16*1024*1024;
    return 4;
    break;
  case GR_MEMORY_TMU:
    if (plength < 4 || params == NULL) return 0;
    params[0] = 16*1024*1024;
    return 4;
    break;
  case GR_MEMORY_UMA:
    if (plength < 4 || params == NULL) return 0;
    params[0] = 16*1024*1024*nbTextureUnits;
    return 4;
    break;
  case GR_BITS_RGBA:
    if (plength < 16 || params == NULL) return 0;
    params[0] = 8;
    params[1] = 8;
    params[2] = 8;
    params[3] = 8;
    return 16;
    break;
  case GR_BITS_DEPTH:
    if (plength < 4 || params == NULL) return 0;
    params[0] = 16;
    return 4;
    break;
  case GR_BITS_GAMMA:
    if (plength < 4 || params == NULL) return 0;
    params[0] = 8;
    return 4;
    break;
  case GR_GAMMA_TABLE_ENTRIES:
    if (plength < 4 || params == NULL) return 0;
    params[0] = 256;
    return 4;
    break;
  case GR_FOG_TABLE_ENTRIES:
    if (plength < 4 || params == NULL) return 0;
    params[0] = 64;
    return 4;
    break;
  case GR_WDEPTH_MIN_MAX:
    if (plength < 8 || params == NULL) return 0;
    params[0] = 0;
    params[1] = 65528;
    return 8;
    break;
  case GR_ZDEPTH_MIN_MAX:
    if (plength < 8 || params == NULL) return 0;
    params[0] = 0;
    params[1] = 65535;
    return 8;
    break;
  case GR_LFB_PIXEL_PIPE:
    if (plength < 4 || params == NULL) return 0;
    params[0] = FXFALSE;
    return 4;
    break;
  case GR_MAX_TEXTURE_ASPECT_RATIO:
    if (plength < 4 || params == NULL) return 0;
    params[0] = 3;
    return 4;
    break;
  case GR_NON_POWER_OF_TWO_TEXTURES:
    if (plength < 4 || params == NULL) return 0;
    params[0] = FXFALSE;
    return 4;
    break;
  case GR_TEXTURE_ALIGN:
    if (plength < 4 || params == NULL) return 0;
    params[0] = 0;
    return 4;
    break;
  default:
    display_warning("unknown pname in grGet : %x", pname);
  }
  return 0;
}

FX_ENTRY const char * FX_CALL
grGetString( FxU32 pname )
{
  LOG("grGetString(%d)\r\n", pname);
  switch(pname)
  {
  case GR_EXTENSION:
    {
      static char extension[] = "CHROMARANGE TEXCHROMA TEXMIRROR PALETTE6666 FOGCOORD EVOODOO TEXTUREBUFFER TEXUMA TEXFMT COMBINE GETGAMMA";
      return extension;
    }
    break;
  case GR_HARDWARE:
    {
      static char hardware[] = "Voodoo5 (tm)";
      return hardware;
    }
    break;
  case GR_VENDOR:
    {
      static char vendor[] = "3Dfx Interactive";
      return vendor;
    }
    break;
  case GR_RENDERER:
    {
      static char renderer[] = "Glide";
      return renderer;
    }
    break;
  case GR_VERSION:
    {
      static char version[] = "3.0";
      return version;
    }
    break;
  default:
    display_warning("unknown grGetString selector : %x", pname);
  }
  return NULL;
}

static void render_rectangle(int texture_number,
                             int dst_x, int dst_y,
                             int src_width, int src_height,
                             int tex_width, int tex_height, int invert)
{
  LOGINFO("render_rectangle(%d,%d,%d,%d,%d,%d,%d,%d)",texture_number,dst_x,dst_y,src_width,src_height,tex_width,tex_height,invert);
  int vertexOffset_location;
  int textureSizes_location;
  static float data[] = {
    ((int)dst_x),                             //X 0
    invert*-((int)dst_y),                     //Y 0 
    0.0f,                                     //U 0 
    0.0f,                                     //V 0

    ((int)dst_x),                             //X 1
    invert*-((int)dst_y + (int)src_height),   //Y 1
    0.0f,                                     //U 1
    (float)src_height / (float)tex_height,    //V 1

    ((int)dst_x + (int)src_width), 
    invert*-((int)dst_y + (int)src_height),
    (float)src_width / (float)tex_width,
    (float)src_height / (float)tex_height,

    ((int)dst_x),
    invert*-((int)dst_y),
    0.0f,
    0.0f
  };

  vbo_disable();
  glDisableVertexAttribArray(COLOUR_ATTR);
  glDisableVertexAttribArray(TEXCOORD_1_ATTR);
  glDisableVertexAttribArray(FOG_ATTR);

  glVertexAttribPointer(POSITION_ATTR,2,GL_FLOAT,false,4 * sizeof(float),data); //Position
  glVertexAttribPointer(TEXCOORD_0_ATTR,2,GL_FLOAT,false,4 * sizeof(float),&data[2]); //Tex

  glEnableVertexAttribArray(COLOUR_ATTR);
  glEnableVertexAttribArray(TEXCOORD_1_ATTR);
  glEnableVertexAttribArray(FOG_ATTR);


  disable_textureSizes();

  glDrawArrays(GL_TRIANGLE_STRIP,0,4);
  vbo_enable();


/*
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBegin(GL_QUADS);
  glMultiTexCoord2fARB(texture_number, 0.0f, 0.0f);
  glVertex2f(((int)dst_x - widtho) / (float)(width/2),
    invert*-((int)dst_y - heighto) / (float)(height/2));
  glMultiTexCoord2fARB(texture_number, 0.0f, (float)src_height / (float)tex_height);
  glVertex2f(((int)dst_x - widtho) / (float)(width/2),
    invert*-((int)dst_y + (int)src_height - heighto) / (float)(height/2));
  glMultiTexCoord2fARB(texture_number, (float)src_width / (float)tex_width, (float)src_height / (float)tex_height);
  glVertex2f(((int)dst_x + (int)src_width - widtho) / (float)(width/2),
    invert*-((int)dst_y + (int)src_height - heighto) / (float)(height/2));
  glMultiTexCoord2fARB(texture_number, (float)src_width / (float)tex_width, 0.0f);
  glVertex2f(((int)dst_x + (int)src_width - widtho) / (float)(width/2),
    invert*-((int)dst_y - heighto) / (float)(height/2));
  glMultiTexCoord2fARB(texture_number, 0.0f, 0.0f);
  glVertex2f(((int)dst_x - widtho) / (float)(width/2),
    invert*-((int)dst_y - heighto) / (float)(height/2));
  glEnd();
*/

  compile_shader();

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
}

FX_ENTRY void FX_CALL grFramebufferCopyExt(int x, int y, int w, int h,
                                           int from, int to, int mode)
{
  if (mode == GR_FBCOPY_MODE_DEPTH) {

    int tw = 1, th = 1;
    if (npot_support) {
      tw = width; th = height;
    } else {
      while (tw < width) tw <<= 1;
      while (th < height) th <<= 1;
    }

    if (from == GR_FBCOPY_BUFFER_BACK && to == GR_FBCOPY_BUFFER_FRONT) {
      //printf("save depth buffer %d\n", render_to_texture);
      // save the depth image in a texture
      //glReadBuffer(current_buffer);
      glBindTexture(GL_TEXTURE_2D, depth_texture);
      glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT,
        0, 0, tw, th, 0);
      glBindTexture(GL_TEXTURE_2D, default_texture);
      return;
    }
    if (from == GR_FBCOPY_BUFFER_FRONT && to == GR_FBCOPY_BUFFER_BACK) {
      //printf("writing to depth buffer %d\n", render_to_texture);
      //glPushAttrib(GL_ALL_ATTRIB_BITS);
      //glDisable(GL_ALPHA_TEST);
      //glDrawBuffer(current_buffer);
      glActiveTexture(texture_unit);
      glBindTexture(GL_TEXTURE_2D, depth_texture);
      glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
      set_depth_shader();
      glEnable(GL_DEPTH_TEST);
      glDepthFunc(GL_ALWAYS);
      glDisable(GL_CULL_FACE);
      render_rectangle(texture_unit,
        0, 0,
        width,  height,
        tw, th, -1);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glBindTexture(GL_TEXTURE_2D, default_texture);
      //glPopAttrib();
      return;
    }

  }
}

FX_ENTRY void FX_CALL
grRenderBuffer( GrBuffer_t buffer )
{
  LOG("grRenderBuffer(%d)\r\n", buffer);

  switch(buffer)
  {
  case GR_BUFFER_BACKBUFFER:
    if(render_to_texture)
    {
      // VP z fix
      //glMatrixMode(GL_MODELVIEW);
      //glLoadIdentity();
      //glTranslatef(0, 0, 1-zscale);
      //glScalef(1, 1, zscale);
      inverted_culling = 0;
      grCullMode(culling_mode);

      width = savedWidth;
      height = savedHeight;
      widtho = savedWidtho;
      heighto = savedHeighto;
      sglBindFramebuffer(GL_FRAMEBUFFER, 0);
      glBindRenderbuffer( GL_RENDERBUFFER, 0 );
      curBufferAddr = 0;

      glViewport(0, 0, width, viewport_height);
      glScissor(0, 0, width, height);

      render_to_texture = 0;
    }
    //glDrawBuffer(GL_BACK);
    break;
  case 6: // RENDER TO TEXTURE
    if(!render_to_texture)
    {
      savedWidth = width;
      savedHeight = height;
      savedWidtho = widtho;
      savedHeighto = heighto;
    }

    {
/*
        float m[4*4] = {1.0f, 0.0f, 0.0f, 0.0f,
          0.0f,-1.0f, 0.0f, 0.0f,
          0.0f, 0.0f, 1.0f, 0.0f,
          0.0f, 0.0f, 0.0f, 1.0f};
        glMatrixMode(GL_MODELVIEW);
        glLoadMatrixf(m);
        // VP z fix
        glTranslatef(0, 0, 1-zscale);
        glScalef(1, 1*1, zscale);
*/
        inverted_culling = 1;
        grCullMode(culling_mode);
    }
    render_to_texture = 1;
    break;
  default:
    display_warning("grRenderBuffer : unknown buffer : %x", buffer);
  }
}

FX_ENTRY void FX_CALL
grAuxBufferExt( GrBuffer_t buffer )
{
  LOG("grAuxBufferExt(%d)\r\n", buffer);
  //display_warning("grAuxBufferExt");

  if (buffer == GR_BUFFER_AUXBUFFER) {
    invtex[0] = 0;
    invtex[1] = 0;
    need_to_compile = 0;
    set_depth_shader();
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDisable(GL_CULL_FACE);
    //glDisable(GL_ALPHA_TEST);
    glDepthMask(GL_TRUE);
    grTexFilterMode(GR_TMU1, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
  } else {
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    need_to_compile = 1;
  }
}

FX_ENTRY void FX_CALL
grBufferClear( GrColor_t color, GrAlpha_t alpha, FxU32 depth )
{
  vbo_draw();
  LOG("grBufferClear(%d,%d,%d)\r\n", color, alpha, depth);
  glClearColor(0, 0, 0, 0);

  if (w_buffer_mode)
    glClearDepthf(1.0f - ((1.0f + (depth >> 4) / 4096.0f) * (1 << (depth & 0xF))) / 65528.0);
  else
    glClearDepthf(depth / 65535.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // ZIGGY TODO check that color mask is on
  buffer_cleared = 1;
}

FX_ENTRY void FX_CALL
grBufferSwap( FxU32 swap_interval )
{
  LOG("grBufferSwap(%d)\r\n", swap_interval);

  // don't swap while rendering to texture
  if (render_to_texture)
    return;

  retro_return(true);

  int i;

  for (i = 0; i < nb_fb; i++)
    fbs[i].buff_clear = 1;
}

// frame buffer

FX_ENTRY FxBool FX_CALL
grLfbLock( GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t writeMode,
          GrOriginLocation_t origin, FxBool pixelPipeline,
          GrLfbInfo_t *info )
{
   LOG("grLfbLock(%d,%d,%d,%d,%d)\r\n", type, buffer, writeMode, origin, pixelPipeline);

#ifndef NDEBUG
   // grLfblock is nop for GR_LBFB_WRITE_ONLY
   if (type == GR_LFB_WRITE_ONLY)
      return FXTRUE;
#endif

   unsigned char *buf;
   int i,j;

   switch(buffer)
   {
      case GR_BUFFER_FRONTBUFFER:
         //glReadBuffer(GL_FRONT);
         break;
      case GR_BUFFER_BACKBUFFER:
         //glReadBuffer(GL_BACK);
         break;
      default:
         display_warning("grLfbLock : unknown buffer : %x", buffer);
   }

   if(buffer != GR_BUFFER_AUXBUFFER)
   {
      if (writeMode == GR_LFBWRITEMODE_888) {
         //printf("LfbLock GR_LFBWRITEMODE_888\n");
         info->lfbPtr = frameBuffer;
         info->strideInBytes = width*4;
         info->writeMode = GR_LFBWRITEMODE_888;
         info->origin = origin;
         //glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, frameBuffer);
      } else {
         buf = (unsigned char*)malloc(width*height*4);

         info->lfbPtr = frameBuffer;
         info->strideInBytes = width*2;
         info->writeMode = GR_LFBWRITEMODE_565;
         info->origin = origin;
         glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buf);

         for (j=0; j<height; j++)
         {
            for (i=0; i<width; i++)
            {
               frameBuffer[(height-j-1)*width+i] =
                  ((buf[j*width*4+i*4+0] >> 3) << 11) |
                  ((buf[j*width*4+i*4+1] >> 2) <<  5) |
                  (buf[j*width*4+i*4+2] >> 3);
            }
         }
         free(buf);
      }
   }
   else
   {
      info->lfbPtr = depthBuffer;
      info->strideInBytes = width*2;
      info->writeMode = GR_LFBWRITEMODE_ZA16;
      info->origin = origin;
      glReadPixels(0, 0, width, height, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, depthBuffer);
   }

   return FXTRUE;
}

FX_ENTRY FxBool FX_CALL
grLfbUnlock( GrLock_t type, GrBuffer_t buffer )
{
  LOG("grLfbUnlock(%d,%d)\r\n", type, buffer);

#ifndef NDEBUG
  // grLbfUnlock - type not for GR_LFB_WRITE_ONLY
  if (type == GR_LFB_WRITE_ONLY)
    display_warning("grLfbUnlock : write only");
#endif

  return FXTRUE;
}

FX_ENTRY FxBool FX_CALL
grLfbReadRegion( GrBuffer_t src_buffer,
                FxU32 src_x, FxU32 src_y,
                FxU32 src_width, FxU32 src_height,
                FxU32 dst_stride, void *dst_data )
{
  unsigned char *buf;
  unsigned int i,j;
  unsigned short *frameBuffer = (unsigned short*)dst_data;
  unsigned short *depthBuffer = (unsigned short*)dst_data;
  LOG("grLfbReadRegion(%d,%d,%d,%d,%d,%d)\r\n", src_buffer, src_x, src_y, src_width, src_height, dst_stride);

  switch(src_buffer)
  {
     case GR_BUFFER_FRONTBUFFER:
        //glReadBuffer(GL_FRONT);
        break;
     case GR_BUFFER_BACKBUFFER:
        //glReadBuffer(GL_BACK);
        break;
        /*case GR_BUFFER_AUXBUFFER:
          glReadBuffer(current_buffer);
          break;*/
     default:
        display_warning("grReadRegion : unknown buffer : %x", src_buffer);
  }

  if(src_buffer != GR_BUFFER_AUXBUFFER)
  {
    buf = (unsigned char*)malloc(src_width*src_height*4);

    glReadPixels(src_x, height-src_y-src_height, src_width, src_height, GL_RGBA, GL_UNSIGNED_BYTE, buf);

    for (j=0; j<src_height; j++)
    {
      for (i=0; i<src_width; i++)
      {
        frameBuffer[j*(dst_stride/2)+i] =
          ((buf[(src_height-j-1)*src_width*4+i*4+0] >> 3) << 11) |
          ((buf[(src_height-j-1)*src_width*4+i*4+1] >> 2) <<  5) |
          (buf[(src_height-j-1)*src_width*4+i*4+2] >> 3);
      }
    }
    free(buf);
  }
  else
  {
    buf = (unsigned char*)malloc(src_width*src_height*2);

    glReadPixels(src_x, height-src_y-src_height, src_width, src_height, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, depthBuffer);

    for (j=0;j<src_height; j++)
    {
      for (i=0; i<src_width; i++)
      {
        depthBuffer[j*(dst_stride/2)+i] =
          ((unsigned short*)buf)[(src_height-j-1)*src_width*4+i*4];
      }
    }
    free(buf);
  }

  return FXTRUE;
}

FX_ENTRY FxBool FX_CALL
grLfbWriteRegion( GrBuffer_t dst_buffer,
                 FxU32 dst_x, FxU32 dst_y,
                 GrLfbSrcFmt_t src_format,
                 FxU32 src_width, FxU32 src_height,
                 FxBool pixelPipeline,
                 FxI32 src_stride, void *src_data )
{
  unsigned char *buf;
  unsigned int i,j;
  unsigned short *frameBuffer = (unsigned short*)src_data;
  int texture_number;
  unsigned int tex_width = 1, tex_height = 1;
  LOG("grLfbWriteRegion(%d,%d,%d,%d,%d,%d,%d,%d)\r\n",dst_buffer, dst_x, dst_y, src_format, src_width, src_height, pixelPipeline, src_stride);

  //glPushAttrib(GL_ALL_ATTRIB_BITS);

  while (tex_width < src_width) tex_width <<= 1;
  while (tex_height < src_height) tex_height <<= 1;

  switch(dst_buffer)
  {
     case GR_BUFFER_BACKBUFFER:
        //glDrawBuffer(GL_BACK);
        break;
     case GR_BUFFER_AUXBUFFER:
        //glDrawBuffer(current_buffer);
        break;
     default:
        display_warning("grLfbWriteRegion : unknown buffer : %x", dst_buffer);
  }

  if(dst_buffer != GR_BUFFER_AUXBUFFER)
  {
    buf = (unsigned char*)malloc(tex_width*tex_height*4);

    texture_number = GL_TEXTURE0;
    glActiveTexture(texture_number);

    const unsigned int half_stride = src_stride / 2;
    switch(src_format)
    {
    case GR_LFB_SRC_FMT_1555:
      for (j=0; j<src_height; j++)
      {
        for (i=0; i<src_width; i++)
        {
          const unsigned int col = frameBuffer[j*half_stride+i];
          buf[j*tex_width*4+i*4+0]=((col>>10)&0x1F)<<3;
          buf[j*tex_width*4+i*4+1]=((col>>5)&0x1F)<<3;
          buf[j*tex_width*4+i*4+2]=((col>>0)&0x1F)<<3;
          buf[j*tex_width*4+i*4+3]= (col>>15) ? 0xFF : 0;
        }
      }
      break;
    case GR_LFBWRITEMODE_555:
      for (j=0; j<src_height; j++)
      {
        for (i=0; i<src_width; i++)
        {
          const unsigned int col = frameBuffer[j*half_stride+i];
          buf[j*tex_width*4+i*4+0]=((col>>10)&0x1F)<<3;
          buf[j*tex_width*4+i*4+1]=((col>>5)&0x1F)<<3;
          buf[j*tex_width*4+i*4+2]=((col>>0)&0x1F)<<3;
          buf[j*tex_width*4+i*4+3]=0xFF;
        }
      }
      break;
    case GR_LFBWRITEMODE_565:
      for (j=0; j<src_height; j++)
      {
        for (i=0; i<src_width; i++)
        {
          const unsigned int col = frameBuffer[j*half_stride+i];
          buf[j*tex_width*4+i*4+0]=((col>>11)&0x1F)<<3;
          buf[j*tex_width*4+i*4+1]=((col>>5)&0x3F)<<2;
          buf[j*tex_width*4+i*4+2]=((col>>0)&0x1F)<<3;
          buf[j*tex_width*4+i*4+3]=0xFF;
        }
      }
      break;
    default:
      display_warning("grLfbWriteRegion : unknown format : %d", src_format);
    }

    glBindTexture(GL_TEXTURE_2D, default_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, 4, tex_width, tex_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    free(buf);

    set_copy_shader();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    render_rectangle(texture_number,
      dst_x, dst_y,
      src_width,  src_height,
      tex_width,  tex_height, +1);

  }
  else
  {
    float *buf = (float*)malloc(src_width * src_height * sizeof(float));

    if (src_format != GR_LFBWRITEMODE_ZA16)
      display_warning("unknown depth buffer write format:%x", src_format);

    if(dst_x || dst_y)
      display_warning("dst_x:%d, dst_y:%d\n",dst_x, dst_y);

    for (j=0; j<src_height; j++)
    {
      for (i=0; i<src_width; i++)
      {
        buf[j * src_width+i] =
          (frameBuffer[(src_height-j-1)*(src_stride/2)+i]/(65536.0f*(2.0f/zscale)))+1-zscale/2.0f;
      }
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);

    //glDrawBuffer(GL_BACK);
    glClear( GL_DEPTH_BUFFER_BIT );
    glDepthMask(1);
    //glDrawPixels(src_width, src_height, GL_DEPTH_COMPONENT, GL_FLOAT, buf);

    free(buf);
  }
  //glDrawBuffer(current_buffer);
  //glPopAttrib();
  return FXTRUE;
}

/* wrapper-specific glide extensions */

FX_ENTRY GrScreenResolution_t FX_CALL grWrapperFullScreenResolutionExt(FxU32* width, FxU32* height)
{
  return 0;
}

FX_ENTRY void FX_CALL grConfigWrapperExt(FxI32 resolution, FxI32 vram, FxBool fbo, FxBool aniso)
{
  LOG("grConfigWrapperExt\r\n");
}

// unused by glide64

FX_ENTRY FxBool FX_CALL
grReset( FxU32 what )
{
  display_warning("grReset");
  return 1;
}

FX_ENTRY void FX_CALL
grEnable( GrEnableMode_t mode )
{
  LOG("grEnable(%d)\r\n", mode);
}

FX_ENTRY void FX_CALL
grDisable( GrEnableMode_t mode )
{
  LOG("grDisable(%d)\r\n", mode);
}

FX_ENTRY void FX_CALL
grDisableAllEffects( void )
{
  display_warning("grDisableAllEffects");
}

FX_ENTRY void FX_CALL
grErrorSetCallback( GrErrorCallbackFnc_t fnc )
{
  display_warning("grErrorSetCallback");
}

FX_ENTRY void FX_CALL
grFinish(void)
{
  display_warning("grFinish");
}

FX_ENTRY void FX_CALL
grFlush(void)
{
  display_warning("grFlush");
}

FX_ENTRY void FX_CALL
grTexMultibase( GrChipID_t tmu,
               FxBool     enable )
{
  display_warning("grTexMultibase");
}

FX_ENTRY void FX_CALL
grTexMipMapMode( GrChipID_t     tmu,
                GrMipMapMode_t mode,
                FxBool         lodBlend )
{
  display_warning("grTexMipMapMode");
}

FX_ENTRY void FX_CALL
grTexDownloadTablePartial( GrTexTable_t type,
                          void         *data,
                          int          start,
                          int          end )
{
  display_warning("grTexDownloadTablePartial");
}

FX_ENTRY void FX_CALL
grTexDownloadTable( GrTexTable_t type,
                   void         *data )
{
  display_warning("grTexDownloadTable");
}

FX_ENTRY FxBool FX_CALL
grTexDownloadMipMapLevelPartial( GrChipID_t        tmu,
                                FxU32             startAddress,
                                GrLOD_t           thisLod,
                                GrLOD_t           largeLod,
                                GrAspectRatio_t   aspectRatio,
                                GrTextureFormat_t format,
                                FxU32             evenOdd,
                                void              *data,
                                int               start,
                                int               end )
{
  display_warning("grTexDownloadMipMapLevelPartial");
  return 1;
}

FX_ENTRY void FX_CALL
grTexDownloadMipMapLevel( GrChipID_t        tmu,
                         FxU32             startAddress,
                         GrLOD_t           thisLod,
                         GrLOD_t           largeLod,
                         GrAspectRatio_t   aspectRatio,
                         GrTextureFormat_t format,
                         FxU32             evenOdd,
                         void              *data )
{
  display_warning("grTexDownloadMipMapLevel");
}

FX_ENTRY void FX_CALL
grTexNCCTable( GrNCCTable_t table )
{
  display_warning("grTexNCCTable");
}

FX_ENTRY void FX_CALL
grViewport( FxI32 x, FxI32 y, FxI32 width, FxI32 height )
{
  display_warning("grViewport");
}

FX_ENTRY void FX_CALL
grDepthRange( FxFloat n, FxFloat f )
{
  display_warning("grDepthRange");
}

FX_ENTRY void FX_CALL
grSplash(float x, float y, float width, float height, FxU32 frame)
{
  display_warning("grSplash");
}

FX_ENTRY FxBool FX_CALL
grSelectContext( GrContext_t context )
{
  display_warning("grSelectContext");
  return 1;
}

FX_ENTRY void FX_CALL
grAADrawTriangle(
                 const void *a, const void *b, const void *c,
                 FxBool ab_antialias, FxBool bc_antialias, FxBool ca_antialias
                 )
{
  display_warning("grAADrawTriangle");
}

FX_ENTRY void FX_CALL
grAlphaControlsITRGBLighting( FxBool enable )
{
  display_warning("grAlphaControlsITRGBLighting");
}

FX_ENTRY void FX_CALL
grGlideSetVertexLayout( const void *layout )
{
  display_warning("grGlideSetVertexLayout");
}

FX_ENTRY void FX_CALL
grGlideGetVertexLayout( void *layout )
{
  display_warning("grGlideGetVertexLayout");
}

FX_ENTRY void FX_CALL
grGlideSetState( const void *state )
{
  display_warning("grGlideSetState");
}

FX_ENTRY void FX_CALL
grGlideGetState( void *state )
{
  display_warning("grGlideGetState");
}

FX_ENTRY void FX_CALL
grLfbWriteColorFormat(GrColorFormat_t colorFormat)
{
  display_warning("grLfbWriteColorFormat");
}

FX_ENTRY void FX_CALL
grLfbWriteColorSwizzle(FxBool swizzleBytes, FxBool swapWords)
{
  display_warning("grLfbWriteColorSwizzle");
}

FX_ENTRY void FX_CALL
grLfbConstantDepth( FxU32 depth )
{
  display_warning("grLfbConstantDepth");
}

FX_ENTRY void FX_CALL
grLfbConstantAlpha( GrAlpha_t alpha )
{
  display_warning("grLfbConstantAlpha");
}

FX_ENTRY void FX_CALL
grTexMultibaseAddress( GrChipID_t       tmu,
                      GrTexBaseRange_t range,
                      FxU32            startAddress,
                      FxU32            evenOdd,
                      GrTexInfo        *info )
{
  display_warning("grTexMultibaseAddress");
}

FX_ENTRY void FX_CALL
grLoadGammaTable( FxU32 nentries, FxU32 *red, FxU32 *green, FxU32 *blue)
{
}

FX_ENTRY void FX_CALL
grGetGammaTableExt(FxU32 nentries, FxU32 *red, FxU32 *green, FxU32 *blue)
{
}

FX_ENTRY void FX_CALL
guGammaCorrectionRGB( FxFloat gammaR, FxFloat gammaG, FxFloat gammaB )
{
}

FX_ENTRY void FX_CALL
grDitherMode( GrDitherMode_t mode )
{
  display_warning("grDitherMode");
}

void grChromaRangeExt(GrColor_t color0, GrColor_t color1, FxU32 mode)
{
  display_warning("grChromaRangeExt");
}

void grChromaRangeModeExt(GrChromakeyMode_t mode)
{
  display_warning("grChromaRangeModeExt");
}

void grTexChromaRangeExt(GrChipID_t tmu, GrColor_t color0, GrColor_t color1, GrTexChromakeyMode_t mode)
{
  display_warning("grTexChromaRangeExt");
}

void grTexChromaModeExt(GrChipID_t tmu, GrChromakeyMode_t mode)
{
  display_warning("grTexChromaRangeModeExt");
}
