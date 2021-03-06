#define _CRT_SECURE_NO_WARNINGS
#include <glad/glad.h>
#include <SDL2/SDL.h>

#define TINYPNG_IMPLEMENTATION
#include <tinypng.h>

#define SPRITEBATCH_IMPLEMENTATION
#include <tinyspritebatch.h>

#define TINYGL_IMPLEMENTATION
#include <tinygl.h>

#define TINYTIME_IMPLEMENTATION
#include <tinytime.h>

spritebatch_t sb;
SDL_Window* window;
SDL_GLContext ctx_gl;
tgContext* ctx_tg;
tgShader sprite_shader;
tgRenderable sprite_renderable;
float projection[16];

// example file/asset i/o system
const char* image_names[] = {
	"basu.png",
	"bat.png",
	"behemoth.png",
	"crow.png",
	"dragon_zombie.png",
	"fire_whirl.png",
	"giant_pignon.png",
	"night_spirit.png",
	"orangebell.png",
	"petit.png",
	"polish.png",
	"power_critter.png",
};

int images_count = sizeof(image_names) / sizeof(*image_names);
tpImage images[sizeof(image_names) / sizeof(*image_names)];

// example data for storing + transforming sprite vertices on CPU
typedef struct
{
	float x, y;
	float u, v;
} vertex_t;

#define SPRITE_VERTS_MAX (1024 * 10)
int sprite_verts_count;
vertex_t sprite_verts[SPRITE_VERTS_MAX];

typedef struct
{
	float x, y;
} v2;

// example of a game sprite
typedef struct
{
	SPRITEBATCH_U64 image_id;
	int depth;
	float x, y;
	float sx, sy;
	float c, s;
} sprite_t;

#include <math.h>

sprite_t make_sprite(SPRITEBATCH_U64 image_id, float x, float y, float scale, float angle_radians, int depth)
{
	sprite_t s;
	s.image_id = image_id;
	s.depth = depth;
	s.x = x;
	s.y = y;
	s.sx = (float)images[s.image_id].w * 2.0f * scale;
	s.sy = (float)images[s.image_id].h * 2.0f * scale;
	s.c = cosf(angle_radians);
	s.s = sinf(angle_radians);
	return s;
}

// callbacks for tinyspritebatch.h
void batch_report(spritebatch_sprite_t* sprites, int count)
{
	printf("begin batch\n");
	for (int i = 0; i < count; ++i) printf("\t%llu\n", sprites[i].texture_id);
	printf("end batch\n");

	// build the draw call
	tgDrawCall call;
	call.r = &sprite_renderable;
	call.textures[0] = (uint32_t)sprites[0].texture_id;
	call.texture_count = 1;

	// set texture uniform in shader
	tgSendTexture(call.r->program, "u_sprite_texture", 0);

	// NOTE:
	// perform any additional sorting here

	// build vertex buffer of quads from all sprite transforms
	call.verts = sprite_verts + sprite_verts_count;
	call.vert_count = count * 6;
	sprite_verts_count += call.vert_count;
	assert(sprite_verts_count < SPRITE_VERTS_MAX);

	vertex_t* verts = call.verts;
	for (int i = 0; i < count; ++i)
	{
		spritebatch_sprite_t* s = sprites + i;

		v2 quad[] = {
			{ -0.5f,  0.5f },
			{  0.5f,  0.5f },
			{  0.5f, -0.5f },
			{ -0.5f, -0.5f },
		};

		for (int j = 0; j < 4; ++j)
		{
			float x = quad[j].x;
			float y = quad[j].y;

			// scale sprite about origin
			x *= s->sx;
			y *= s->sy;

			// rotate sprite about origin
			float x0 = s->c * x - s->s * y;
			float y0 = s->s * x + s->c * y;
			x = x0;
			y = y0;

			// translate sprite into the world
			x += s->x;
			y += s->y;

			quad[j].x = x;
			quad[j].y = y;
		}

		// output transformed quad into CPU buffer
		vertex_t* out_verts = verts + i * 6;

		out_verts[0].x = quad[0].x;
		out_verts[0].y = quad[0].y;
		out_verts[0].u = s->minx;
		out_verts[0].v = s->maxy;

		out_verts[1].x = quad[3].x;
		out_verts[1].y = quad[3].y;
		out_verts[1].u = s->minx;
		out_verts[1].v = s->miny;

		out_verts[2].x = quad[1].x;
		out_verts[2].y = quad[1].y;
		out_verts[2].u = s->maxx;
		out_verts[2].v = s->maxy;

		out_verts[3].x = quad[1].x;
		out_verts[3].y = quad[1].y;
		out_verts[3].u = s->maxx;
		out_verts[3].v = s->maxy;

		out_verts[4].x = quad[3].x;
		out_verts[4].y = quad[3].y;
		out_verts[4].u = s->minx;
		out_verts[4].v = s->miny;

		out_verts[5].x = quad[2].x;
		out_verts[5].y = quad[2].y;
		out_verts[5].u = s->maxx;
		out_verts[5].v = s->miny;
	}

	// submit call to tinygl (does not get flushed to screen until `tgFlush` is called)
	tgPushDrawCall(ctx_tg, call);
}

void* get_pixels(SPRITEBATCH_U64 image_id)
{
	return images[image_id].pix;
}

SPRITEBATCH_U64 generate_texture_handle(void* pixels, int w, int h)
{
	GLuint location;
	glGenTextures(1, &location);
	glBindTexture(GL_TEXTURE_2D, location);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glBindTexture(GL_TEXTURE_2D, 0);
	return (SPRITEBATCH_U64)location;
}

void destroy_texture_handle(SPRITEBATCH_U64 texture_id)
{
	GLuint id = (GLuint)texture_id;
	glDeleteTextures(1, &id);
}

// required setup (unrelated to tinyspritebatch.h)
void setup_SDL_and_glad()
{
	// Setup SDL and OpenGL and a window
	SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO);

	// Request OpenGL 3.2 context.
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);

	// set double buffer
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	// immediate swaps
	SDL_GL_SetSwapInterval(0);

	SDL_DisplayMode dm;
	if (SDL_GetDesktopDisplayMode(0, &dm))
	{
		SDL_Log("SDL_GetDesktopDisplayMode failed: %s", SDL_GetError());
		return;
	}

	int screen_w = 640;
	int screen_h = 480;
	int centered_x = dm.w / 2 - screen_w / 2;
	int centered_y = dm.h / 2 - screen_h / 2;
	window = SDL_CreateWindow("tinyspritebatch example", centered_x, centered_y, screen_w, screen_h, SDL_WINDOW_OPENGL|SDL_WINDOW_ALLOW_HIGHDPI);
	ctx_gl = SDL_GL_CreateContext(window);

	gladLoadGLES2Loader(SDL_GL_GetProcAddress);

	int major, minor;
	SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
	SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor);
	printf("SDL says running on OpenGL ES version %d.%d\n", major, minor);
	printf("Glad says OpenGL ES version : %d.%d\n", GLVersion.major, GLVersion.minor);
	printf("OpenGL says : ES %s, GLSL %s\n", glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));
}

void load_images()
{
	for (int i = 0; i < images_count; ++i)
		images[i] = tpLoadPNG(image_names[i]);
}

void setup_tinygl()
{
	// setup tinygl
	int clear_bits = GL_COLOR_BUFFER_BIT;
	int settings_bits = 0;
	ctx_tg = tgMakeCtx(32, clear_bits, settings_bits);

#define STR(x) #x

	const char* vs = STR(
		#version 300 es\n

		uniform mat4 u_mvp;

		in vec2 in_pos;
		in vec2 in_uv;

		out vec2 v_uv;

		void main( )
		{
			v_uv = in_uv;
			gl_Position = u_mvp * vec4(in_pos, 0, 1);
		}
	);

	const char* ps = STR(
		#version 300 es\n
		precision mediump float;
	
		uniform sampler2D u_sprite_texture;

		in vec2 v_uv;
		out vec4 out_col;

		void main()
		{
			out_col = texture(u_sprite_texture, v_uv);
		}
	);

	tgVertexData vd;
	tgMakeVertexData(&vd, 1024 * 1024, GL_TRIANGLES, sizeof(vertex_t), GL_DYNAMIC_DRAW);
	tgAddAttribute(&vd, "in_pos", 2, TG_FLOAT, TG_OFFSET_OF(vertex_t, x));
	tgAddAttribute(&vd, "in_uv", 2, TG_FLOAT, TG_OFFSET_OF(vertex_t, u));

	tgMakeRenderable(&sprite_renderable, &vd);
	tgLoadShader(&sprite_shader, vs, ps);
	tgSetShader(&sprite_renderable, &sprite_shader);
	
	tgOrtho2D((float)640, (float)480, 0, 0, projection);
	glViewport(0, 0, 640, 480);

	tgSendMatrix(&sprite_shader, "u_mvp", projection);
	tgLineMVP(ctx_tg, projection);
}

void swap_buffers()
{
	SDL_GL_SwapWindow(window);
}

spritebatch_config_t get_demo_config()
{
	spritebatch_config_t config;
	config.pixel_stride = sizeof(uint8_t) * 4; // RGBA uint8_t from tinypng.h
	config.atlas_width_in_pixels = 1024;
	config.atlas_height_in_pixels = 1024;
	config.ticks_to_decay_texture = 3;
	config.lonely_buffer_count_till_flush = 1;
	config.lonely_buffer_count_till_decay = 1;
	config.ratio_to_decay_atlas = 0.5f;
	config.ratio_to_merge_atlases = 0.25f;
	config.allocator_context = 0;
	return config;
}

#define push_sprite(sp) \
	spritebatch_push(&sb, sp.image_id, images[sp.image_id].w, images[sp.image_id].h, sp.x, sp.y, sp.sx, sp.sy, sp.c, sp.s, (SPRITEBATCH_U64)sp.depth)

void scene0()
{
	sprite_t basu = make_sprite(0, 0, 0, 1.0f, 0, 0);
	sprite_t bat = make_sprite(1, 30, 30, 1.0f, 0, 0);
	sprite_t behemoth = make_sprite(2, 80, 30, 1.0f, 3.14159265f / 4.0f, 0);
	sprite_t crow = make_sprite(3, 70, -50, 1.0f, -3.14159265f / 4.0f, 0);

	push_sprite(basu);
	push_sprite(bat);
	push_sprite(behemoth);
	push_sprite(crow);
}

void scene1()
{
	sprite_t basu = make_sprite(0, 0, 0, 1.0f, 0, 0);
	sprite_t bat = make_sprite(1, 30, 30, 1.0f, 0, 0);
	push_sprite(basu);
	push_sprite(bat);
}

void scene2()
{
	sprite_t basu = make_sprite(0, 0, 0, 1.0f, 0, 0);
	sprite_t bat = make_sprite(1, 30, 30, 1.0f, 0, 0);
	sprite_t behemoth = make_sprite(2, 80, 30, 1.0f, 3.14159265f / 4.0f, 0);
	sprite_t crow = make_sprite(3, 70, -50, 1.0f, -3.14159265f / 4.0f, 0);

	static int which = 0;

	switch (which)
	{
	case 0: push_sprite(basu);     break;
	case 1: push_sprite(bat);      break;
	case 2: push_sprite(behemoth); break;
	case 3: push_sprite(crow);     break;
	}

	which = (which + 1) % 4;
}

	//"dragon_zombie.png",
	//"fire_whirl.png",
	//"giant_pignon.png",
	//"night_spirit.png",
	//"orangebell.png",
	//"petit.png",
	//"polish.png",
	//"power_critter.png",

void scene3()
{
	sprite_t dragon_zombie = make_sprite(4, -250, -200, 1, 0, 0);
	sprite_t fire_whirl = make_sprite(5, -150, -100, 1, 0, 0);
	sprite_t giant_pignon = make_sprite(6, -200, 0, 1, 0, 0);
	sprite_t night_spirit = make_sprite(7, -225, 100, 1, 0, 0);
	sprite_t orangebell = make_sprite(8, -200, 200, 1, 0, 0);
	sprite_t petit = make_sprite(9, -100, 200, 1, 0, 0);
	sprite_t power_critter = make_sprite(11, -25, 75, 1, 0, 0);

	push_sprite(dragon_zombie);
	push_sprite(fire_whirl);
	push_sprite(giant_pignon);
	push_sprite(night_spirit);
	push_sprite(orangebell);
	push_sprite(petit);
	push_sprite(power_critter);

	sprite_t polish = make_sprite(10, 50, 180, 1, 0, 0);
	sprite_t translated = polish;
	for (int i = 0; i < 4; ++i)
	{
		translated.x = polish.x + polish.sx * i;

		for (int j = 0; j < 6; ++j)
		{
			translated.y = polish.y - polish.sy * j;
			push_sprite(translated);
		}
	}
}

int main(int argc, char** argv)
{
	// initial "game" setup
	setup_SDL_and_glad();
	setup_tinygl();
	load_images();

	// setup tinyspritebatch configuration
	// this configuration is specialized to test out the demo. don't use these settings
	// in your own project. Instead, start with `spritebatch_set_default_config`.
	spritebatch_config_t config = get_demo_config();
	//spritebatch_set_default_config(&config); // turn default config off to test out demo

	// assign the 4 callbacks
	config.batch_callback = batch_report;                       // report batches of sprites from `spritebatch_flush`
	config.get_pixels_callback = get_pixels;                    // used to retrieve image pixels from `spritebatch_flush` and `spritebatch_defrag`
	config.generate_texture_callback = generate_texture_handle; // used to generate a texture handle from `spritebatch_flush` and `spritebatch_defrag`
	config.delete_texture_callback = destroy_texture_handle;    // used to destroy a texture handle from `spritebatch_defrag`

	// initialize tinyspritebatch
	spritebatch_init(&sb, &config);

	void (*scenes[])() = {
		scene0,
		scene1,
		scene2,
		scene3,
	};
	int scene = 3;

	// game main loop
	int application_running = 1;
	float dt = 0;
	while (application_running)
	{
		dt += ttTime();

		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
			case SDL_QUIT:
				application_running = 0;
				break;

			case SDL_KEYDOWN:
			{
				SDL_Keycode key = event.key.keysym.sym;
				printf("swap scene\n");
				if (key == SDLK_SPACE) scene = (scene + 1) % (sizeof(scenes) / sizeof(*scenes));
			}	break;
			}
		}

		// render 60fps
		if (dt < (1.0f / 60.0f)) continue;
		dt = 0;

		static int tick = 0;
		printf("tick %d\n", tick++);

		// push some sprites to tinyspritebatch
		scenes[scene]();

		// Run tinyspritebatch to find sprite batches.
		// This is the most basic usage of tinypsritebatch, one defrag, tick and flush per game loop.
		// It is also possible to only use defrag once every N frames.
		// tick can also be called at different time intervals (for example, once per game update
		// but not necessarily once per screen render).
		spritebatch_defrag(&sb);
		spritebatch_tick(&sb);
		spritebatch_flush(&sb);
		sprite_verts_count = 0;

		// sprite batches have been submit to tinygl, go ahead and flush to screen
		tgFlush(ctx_tg, swap_buffers, 0, 640, 480);
		TG_PRINT_GL_ERRORS();
	}

	return 0;
}
