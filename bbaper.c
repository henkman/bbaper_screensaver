#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#ifdef __linux__
#include <sys/random.h>
typedef struct {
	unsigned off;
	uint8_t random[32 * 1024];
} RandomSource;
static void random_init(RandomSource *s) { s->off = sizeof(s->random); }
static void random_destroy(RandomSource *s) {}
static uint8_t random_uint8(RandomSource *s)
{
	if (s->off == sizeof(s->random)) {
		s->off = 0;
		getrandom(s->random, sizeof(s->random), 0);
	}
	return s->random[s->off++];
}
#elif _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
typedef struct {
	HCRYPTPROV hcrypt;
	unsigned off;
	uint8_t random[32 * 1024];
} RandomSource;
static void random_init(RandomSource *s)
{
	s->off = sizeof(s->random);
	CryptAcquireContext(&s->hcrypt, NULL, NULL, PROV_RSA_FULL, 0);
}
static void random_destroy(RandomSource *s)
{
	CryptReleaseContext(s->hcrypt, 0);
}
static uint8_t random_uint8(RandomSource *s)
{
	if (s->off == sizeof(s->random)) {
		s->off = 0;
		CryptGenRandom(s->hcrypt, sizeof(s->random), s->random);
	}
	return s->random[s->off++];
}
#endif

static int8_t random_int8(RandomSource *s) { return (int8_t)random_uint8(s); }
static uint32_t random_uint32(RandomSource *s)
{
	return (uint32_t)(random_uint8(s)) | (uint32_t)(random_uint8(s) << 8) |
		   (uint32_t)(random_uint8(s) << 16) |
		   (uint32_t)(random_uint8(s) << 24);
}

#include <SDL2/SDL.h>
#include <yaml.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

static SDL_Texture *texture_load(SDL_Renderer *render, char *path)
{
	int rw, rh, bpp;
	uint32_t rmask, gmask, bmask, amask;
	SDL_Surface *surf;
	unsigned char *data = stbi_load(path, &rw, &rh, &bpp, 0);
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	rmask = 0xff000000;
	gmask = 0x00ff0000;
	bmask = 0x0000ff00;
	amask = 0x000000ff;
#else
	rmask = 0x000000ff;
	gmask = 0x0000ff00;
	bmask = 0x00ff0000;
	amask = 0xff000000;
#endif
	if (bpp == 4) {
		surf = SDL_CreateRGBSurface(0, rw, rh, 32, rmask, gmask, bmask, amask);
	} else if (bpp == 3) {
		surf = SDL_CreateRGBSurface(0, rw, rh, 24, rmask, gmask, bmask, 0);
	} else {
		stbi_image_free(data);
		return NULL;
	}
	memcpy(surf->pixels, data, bpp * rw * rh);
	stbi_image_free(data);
	SDL_Texture *tex = SDL_CreateTextureFromSurface(render, surf);
	SDL_FreeSurface(surf);
	return tex;
}

typedef struct {
	unsigned current, delay, count;
	SDL_Texture **frame;
	unsigned *delays;
} Animation;
static void animation_update(Animation *an, unsigned dt)
{
	an->delay += dt;
	if (an->delay > an->delays[an->current]) {
		an->delay -= an->delays[an->current];
		an->current = (an->current + 1) % an->count;
	}
}
static void animation_render(Animation *an, SDL_Renderer *rend, int x, int y)
{
	SDL_Texture *current = an->frame[an->current];
	int w, h;
	SDL_QueryTexture(current, NULL, NULL, &w, &h);
	SDL_Rect src = {0, 0, w, h};
	SDL_Rect dst = {x, y, w, h};
	SDL_RenderCopy(rend, current, &src, &dst);
}

typedef struct {
	float x, y;
} Vec2;
static Vec2 vec2_add(Vec2 a, Vec2 b) { return (Vec2){a.x + b.x, a.y + b.y}; }

typedef struct {
	Vec2 pos, move;
	union {
		Animation;
		Animation animation;
	};
} Thing;

int main(int argc, char **argv)
{
	struct {
		char *frameformat;
		unsigned framecount;
		unsigned framedelay;
		unsigned thingcount;
	} config = {0};
	{
		FILE *fd = fopen("./cfg.yaml", "r");
		if (!fd) {
			return 1;
		}
		int done = 0, err = 0;
		yaml_parser_t parser;
		yaml_event_t event;
		yaml_parser_initialize(&parser);
		yaml_parser_set_input_file(&parser, fd);
		enum {
			NONE,
			FRAMEFORMAT,
			FRAMECOUNT,
			FRAMEDELAY,
			THINGCOUNT
		} cur = NONE;
		while (!done) {
			if (!yaml_parser_parse(&parser, &event)) {
				err = 1;
				goto error;
			}
			if (event.type == YAML_SCALAR_EVENT) {
				switch (cur) {
					case FRAMEFORMAT: {
						config.frameformat =
							malloc(sizeof(char) * event.data.scalar.length + 1);
						memcpy(config.frameformat, event.data.scalar.value,
							   event.data.scalar.length);
						config.frameformat[event.data.scalar.length] = 0;
						cur = NONE;
					} break;
					case FRAMECOUNT: {
						config.framecount = (unsigned)atoi(
							(const char *)event.data.scalar.value);
						cur = NONE;
					} break;
					case FRAMEDELAY: {
						config.framedelay = (unsigned)atoi(
							(const char *)event.data.scalar.value);
						cur = NONE;
					} break;
					case THINGCOUNT: {
						config.thingcount = (unsigned)atoi(
							(const char *)event.data.scalar.value);
						cur = NONE;
					} break;
					default:
						if (event.data.scalar.length ==
								(sizeof("frameformat") - 1) &&
							memcmp(event.data.scalar.value, "frameformat",
								   sizeof("frameformat") - 1) == 0) {
							cur = FRAMEFORMAT;
						} else if (event.data.scalar.length ==
									   sizeof("framecount") - 1 &&
								   memcmp(event.data.scalar.value, "framecount",
										  sizeof("framecount") - 1) == 0) {
							cur = FRAMECOUNT;
						} else if (event.data.scalar.length ==
									   sizeof("framedelay") - 1 &&
								   memcmp(event.data.scalar.value, "framedelay",
										  sizeof("framedelay") - 1) == 0) {
							cur = FRAMEDELAY;
						} else if (event.data.scalar.length ==
									   sizeof("thingcount") - 1 &&
								   memcmp(event.data.scalar.value, "thingcount",
										  sizeof("thingcount") - 1) == 0) {
							cur = THINGCOUNT;
						}
				}
				done = 0;
			} else {
				done = (event.type == YAML_STREAM_END_EVENT);
			}
			yaml_event_delete(&event);
		}
	error:
		yaml_parser_delete(&parser);
		fclose(fd);
		if (err || config.frameformat == NULL) {
			return 1;
		}
	}
	RandomSource s;
	random_init(&s);
	SDL_Init(SDL_INIT_VIDEO);
	SDL_ShowCursor(SDL_DISABLE);
	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");

	int displaycount = SDL_GetNumVideoDisplays();
	struct {
		SDL_Window *win;
		SDL_Renderer *rend;
		int width, height;
		SDL_Texture *frames[config.framecount];
		unsigned delays[config.framecount];
		Thing things[config.thingcount];
	} display[displaycount];

	for (unsigned d = 0; d < displaycount; d++) {
		SDL_Rect sr;
		SDL_GetDisplayBounds(d, &sr);
		display[d].win = SDL_CreateWindow("", sr.x, sr.y, 0, 0,
										  SDL_WINDOW_FULLSCREEN_DESKTOP);
		display[d].rend = SDL_CreateRenderer(
			display[d].win, -1,
			SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

		char path[256];
		for (unsigned i = 0; i < config.framecount; i++) {
			sprintf(path, config.frameformat, i + 1);
			display[d].frames[i] = texture_load(display[d].rend, path);
			display[d].delays[i] = config.framedelay;
		}

		SDL_GetWindowSize(display[d].win, &display[d].width,
						  &display[d].height);
		for (unsigned i = 0; i < config.thingcount; i++) {
			Thing *th = &display[d].things[i];
			th->pos = (Vec2){(float)(random_uint32(&s) % display[d].width),
							 (float)(random_uint32(&s) % display[d].height)};
			th->move = (Vec2){(float)random_int8(&s) / 200,
							  (float)random_int8(&s) / 200};
			th->animation = (Animation){random_uint8(&s) % config.framecount, 0,
										config.framecount, display[d].frames,
										display[d].delays};
		}
	}
	free(config.frameformat);

	unsigned lt, ct;
	lt = SDL_GetTicks();
	for (;;) {
		ct = SDL_GetTicks();
		unsigned dt = (ct - lt) * 1000;
		lt = ct;
		{
			SDL_Event e;
			while (SDL_PollEvent(&e))
				if (e.type == SDL_QUIT || e.type == SDL_KEYUP ||
					e.type == SDL_MOUSEBUTTONUP)
					goto end;
		}
		for (unsigned d = 0; d < displaycount; d++) {
			for (unsigned i = 0; i < config.thingcount; i++) {
				Thing *thing = &display[d].things[i];
				thing->pos = vec2_add(thing->pos, thing->move);
#define GAP 50
				if (thing->pos.x > (display[d].width + GAP) ||
					thing->pos.x < -GAP ||
					thing->pos.y > (display[d].height + GAP) ||
					thing->pos.y < -GAP) {
					switch (random_uint8(&s) % 4) {
						case 0:  // left
							thing->pos = (Vec2){
								-(GAP / 2),
								(float)(random_uint32(&s) % display[d].height)};
							thing->move =
								(Vec2){fabs((float)random_int8(&s) / 200),
									   (float)random_int8(&s) / 200};
							break;
						case 1:  // top
							thing->pos = (Vec2){
								(float)(random_uint32(&s) % display[d].width),
								-(GAP / 2)};
							thing->move =
								(Vec2){(float)random_int8(&s) / 200,
									   fabs((float)random_int8(&s) / 200)};
							break;
						case 2:  // right
							thing->pos = (Vec2){
								display[d].width + (GAP / 2),
								(float)(random_uint32(&s) % display[d].height)};
							thing->move =
								(Vec2){-fabs((float)random_int8(&s) / 200),
									   (float)random_int8(&s) / 200};
							break;
						case 3:  // bottom
							thing->pos = (Vec2){
								(float)(random_uint32(&s) % display[d].width),
								display[d].height + (GAP / 2)};
							thing->move =
								(Vec2){(float)random_int8(&s) / 200,
									   -fabs((float)random_int8(&s) / 200)};
							break;
					}
				}
				animation_update(&thing->animation, dt);
			}
			SDL_SetRenderDrawColor(display[d].rend, 0x00, 0x00, 0x00, 0xFF);
			SDL_RenderClear(display[d].rend);
			for (unsigned i = 0; i < config.thingcount; i++) {
				Thing *thing = &display[d].things[i];
				animation_render(&thing->animation, display[d].rend,
								 thing->pos.x, thing->pos.y);
			}
			SDL_RenderPresent(display[d].rend);
		}
	}
end:
	for (unsigned d = 0; d < displaycount; d++) {
		SDL_DestroyRenderer(display[d].rend);
		SDL_DestroyWindow(display[d].win);
	}
	SDL_Quit();
	random_destroy(&s);
	return 0;
}