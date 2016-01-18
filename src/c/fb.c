#define _XOPEN_SOURCE 500

#include "luares.h"
#include "lupi.h"
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <linux/fb.h>

int fb_ready = 0;
int fb_file;
struct fb_var_screeninfo fb_vinfo;
struct fb_fix_screeninfo fb_finfo;
char *fb_ptr = 0;
int fb_cw, fb_ch, fb_bpp, fb_bypp, fb_xo, fb_yo, fb_pitch;

int palette[256];

#define XY_TO_FB(x, y) (((fb_yo + (y)) * fb_pitch) + ((fb_xo + (x)) * fb_bypp))

static int l_set_palette (lua_State *L) {
  int i = lua_tonumber(L, 1);
  int pal = lua_tonumber(L, 2);
  if(i >= 0 && i < 256) {
    if (fb_bpp == 16) {
      palette[i] =
        ((((pal & 0xFF0000) >> 16) & 0x1F) << 11) |
        ((((pal & 0x00FF00) >>  8) & 0x3F) <<  5) |
        ((((pal & 0x0000FF) >>  0) & 0x1F) <<  0);
    } else {
      palette[i] = pal;
    }
  }
  return 0;
}

static int l_fbput (lua_State *L) {
  int x = lua_tonumber(L, 1);
  int y = lua_tonumber(L, 2);
  int bg = lua_tonumber(L, 3);
  int fg = lua_tonumber(L, 4);
  int chr = lua_tonumber(L, 5);
  int px, py, c;
  c = 0;

  if (x < 0 || x >= fb_cw || y < 0 || y >= fb_ch || bg < 0 || bg >= 256 || fg < 0 || fg >= 256
     || chr < 0 || chr >= 65536) {
    return 0;
  }

  int cwd = lua_unifont[chr * 33];
  if (fb_bpp == 32) {
    int* ptr;
    for (py = 0; py < 16; py++) {
      if (cwd == 2) {
        c = (lua_unifont[chr * 33 + 2 + (py * 2)] << 8) | lua_unifont[chr * 33 + 1 + (py * 2)];
      } else if (cwd == 1) {
        c = lua_unifont[chr * 33 + 1 + py];
      }
      ptr = (int*) (&fb_ptr[XY_TO_FB((x * 8), ((y * 16) + py))]);
      for (px = (cwd == 2 ? 15 : 7); px >= 0; px--) {
        ptr[px] = (c & 1) ? palette[fg] : palette[bg];
        c >>= 1;
      }
    }
  } else {
    short* ptr;
    for (py = 0; py < 16; py++) {
      if (cwd == 2) {
        c = (lua_unifont[chr * 33 + 2 + (py * 2)] << 8) | lua_unifont[chr * 33 + 1 + (py * 2)];
      } else if (cwd == 1) {
        c = lua_unifont[chr * 33 + 1 + py];
      }
      ptr = (short*) (&fb_ptr[XY_TO_FB((x * 8), ((y * 16) + py))]);
      for (px = (cwd == 2 ? 15 : 7); px >= 0; px--) {
        ptr[px] = (c & 1) ? (short) palette[fg] : (short) palette[bg];
        c >>= 1;
      }
    }
  }
  return 0;
}

static int l_get_width (lua_State *L) {
  lua_pushnumber(L, fb_cw);
  return 1;
}

static int l_get_height (lua_State *L) {
  lua_pushnumber(L, fb_ch);
  return 1;
}

static int l_fb_ready (lua_State *L) {
  lua_pushboolean(L, fb_ready);
  return 1;
}

void fb_start(lua_State *L) {
   fb_file = open("/dev/fb0", O_RDWR);
   if (fb_file == -1) {
     printf("Error: cannot open framebuffer device");
     exit(1);
     return;
   }

  if (ioctl(fb_file, FBIOGET_FSCREENINFO, &fb_finfo) == -1) {
     printf("Error reading fixed information");
     exit(1);
     return;
  }

  if (ioctl(fb_file, FBIOGET_VSCREENINFO, &fb_vinfo) == -1) {
    printf("Error reading variable information");
    exit(1);
    return;
  }

  fb_vinfo.bits_per_pixel = 32;
  fb_cw = fb_vinfo.xres / 8;
  fb_ch = fb_vinfo.yres / 16;

  if (ioctl(fb_file, FBIOPUT_VSCREENINFO, &fb_vinfo) == -1) {
    fb_vinfo.bits_per_pixel = 16;
    if (ioctl(fb_file, FBIOPUT_VSCREENINFO, &fb_vinfo) == -1) {
      printf("Error setting 32 or 16BPP mode");
      exit(1);
      return;
    }
  }

  fb_bpp = fb_vinfo.bits_per_pixel;
  fb_bypp = fb_bpp >> 3;
  fb_pitch = fb_vinfo.xres_virtual * fb_bypp;
  fb_xo = fb_vinfo.xoffset;
  fb_yo = fb_vinfo.yoffset;

  fb_ptr = (char *)mmap(0, fb_vinfo.xres * fb_vinfo.yres * fb_vinfo.bits_per_pixel / 8, PROT_READ | PROT_WRITE, MAP_SHARED, fb_file, 0);
  if ((int)fb_ptr == -1) {
    printf("Failed to map framebuffer device to memory");
    exit(1);
    return;
  }

  fb_ready = 0;
  lua_createtable (L, 0, 1);

  pushctuple(L, "setPalette", l_set_palette);
  pushctuple(L, "getWidth", l_get_width);
  pushctuple(L, "getHeight", l_get_height);
  pushctuple(L, "put", l_fbput);
  pushctuple(L, "isReady", l_fb_ready);

  lua_setglobal(L, "framebuffer");
}
