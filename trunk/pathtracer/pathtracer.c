#include "../quakedef.h"
#include "pathtracer_private.h"
#include "wishdir.h"

extern cvar_t show_speed_x;
extern cvar_t show_speed_y;

static cvar_t scr_printbunnyhop = { "scr_printbunnyhop", "1" };
static cvar_t scr_recordbunnyhop = { "scr_recordbunnyhop", "1" };

pathtracer_movement_t pathtracer_movement_samples[PATHTRACER_MOVEMENT_BUFFER_MAX];
int pathtracer_movement_write_head = 0;
int pathtracer_movement_read_head = 0;

void PathTracer_Draw(void)
{
	if (sv_player == NULL) return;
	if (!sv.active && !cls.demoplayback) return;
	if (cls.demoplayback) return;

	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_CULL_FACE);

	// Trace path
	glBegin(GL_LINES);
	int fadeCounter = 0;
	for (int buffer_index = 0;buffer_index < PATHTRACER_MOVEMENT_BUFFER_MAX; buffer_index++) {
		int i = (pathtracer_movement_read_head + buffer_index) % PATHTRACER_MOVEMENT_BUFFER_MAX;
		pathtracer_movement_t* pms_prev = &pathtracer_movement_samples[i++];
		if (i >= PATHTRACER_MOVEMENT_BUFFER_MAX)
			i = 0;
		pathtracer_movement_t* pms_cur = &pathtracer_movement_samples[i];

		// Reached write head?
		if (i == pathtracer_movement_write_head)
			continue;

		// No data in yet?
		if (!(pms_prev->holdsData && pms_cur->holdsData))
			continue;

		// If we are in the range of 200 samples before the write head
		qboolean isFadingOut = (pathtracer_movement_write_head == pathtracer_movement_read_head) ? fadeCounter < PATHTRACER_MOVEMENT_BUFFER_MAX - PATHTRACER_MOVEMENT_BUFFER_FADEOUT : fadeCounter < pathtracer_movement_write_head - pathtracer_movement_read_head - PATHTRACER_MOVEMENT_BUFFER_FADEOUT;
		fadeCounter++;

		// then grey
		if (isFadingOut) {
			glColor3f(.1f, .1f, .1f);
		}
		else
		if (pms_cur->onground && pms_prev->onground) {
			glColor3f(.3f, 0.f, 0.f);
		}
		else if (pms_cur->onground || pms_prev->onground) {
			glColor3f(.1f, .1f, .1f);
		}
		else {
			glColor3f(1.f, 1.f, 1.f);
		}

		GLfloat startPos[3];
		startPos[0] = pms_prev->pos[0];
		startPos[1] = pms_prev->pos[1];
		startPos[2] = pms_prev->pos[2] - 20.f; // on the ground
		glVertex3fv(startPos);

		GLfloat startPos2[3];
		startPos2[0] = pms_cur->pos[0];
		startPos2[1] = pms_cur->pos[1];
		startPos2[2] = pms_cur->pos[2] - 20.f; // on the ground
		glVertex3fv(startPos2);
	}
	glEnd();

	// Back to normal rendering
	glColor3f(1, 1, 1);
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_CULL_FACE);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void PathTracer_Sample_Each_Frame(void) {

	if (scr_printbunnyhop.value != 1.f) return;
	if (!sv.active && !cls.demoplayback) return;
	if (!sv.active) return;
	if (cls.demoplayback) return;
	if (sv_player == NULL) return;

	extern usercmd_t cmd;
	extern entity_t ghost_entity;

	static double prevcltime = -1;

	// demo playback only has cl.onground which is different from onground in normal play
	qboolean onground0;
	if (sv.active) {
		extern qboolean onground;
		onground0 = onground;
	}
	else {
		onground0 = cl.onground;
	}

	// Restart tracer
	if (cl.time < prevcltime) {
		memset(pathtracer_movement_samples, 0, sizeof(pathtracer_movement_samples));
		pathtracer_movement_write_head = 0;

		prevcltime = cl.time;
	}
	else {
		prevcltime = cl.time;
	}

	// original code: https://github.com/shalrathy/quakespasm-shalrathy
	// Find best wishdir vector (largest speed increase in the ground plane)
	int angles = 90;
	float bestspeed = 0.f;
	float bestangle = 0;
	bestangle = sv_player->v.angles[1];
	bestspeed = Get_Wishdir_Speed_Delta(bestangle);
	float playerspeed = Get_Wishdir_Speed_Delta(bestangle);
	for (int i = 1; i < angles; i++) {
		for (int neg = -1; neg <= 1; neg += 2) {
			float curangle = 0.0f;
			curangle = sv_player->v.angles[1] + i * neg;
			float curspeed = Get_Wishdir_Speed_Delta(curangle);
			if (curspeed > bestspeed) {
				bestspeed = curspeed;
				bestangle = curangle;
			}
		}
	}

	// 100th of a degree
	for (int i = 1; i < 100; i++) {
		for (int neg = -1; neg <= 1; neg += 2) {
			float curangle = bestangle + neg * i / 100.0f;
			float curspeed = Get_Wishdir_Speed_Delta(curangle);
			if (curspeed > (bestspeed)) {
				bestspeed = curspeed;
				bestangle = curangle;
			}
		}
	}

	if (bestangle < -180) bestangle += 360;
	if (bestangle > 180) bestangle -= 360;

	float speed = sqrt(cl.velocity[0] * cl.velocity[0] + cl.velocity[1] * cl.velocity[1]);

	// Determine if we should sample
	boolean track = false;
	vec3_t *origin = &sv_player->v.origin;
	if (ghost_entity.model != NULL)
		origin = &ghost_entity.origin;
			
	pathtracer_movement_t* pms_prev;
	if (pathtracer_movement_write_head > 0)
		pms_prev = &pathtracer_movement_samples[pathtracer_movement_write_head - 1];
	else
		// just in case we hit that zero
		pms_prev = &pathtracer_movement_samples[PATHTRACER_BUNNHOP_BUFFER_MAX - 1];
		
	if(pms_prev->holdsData) {
		float dx = fabs(pms_prev->pos[0] - *origin[0]);
		float dy = fabs(pms_prev->pos[1] - *origin[1]);
		float dz = fabs(pms_prev->pos[2] - *origin[2]);
		if (dx > .1f || dy > .1f || dz > .1f) {
			track = true;
		}
	}
	else {
		track = true;
	}

	// Sample movement
	if (track && scr_recordbunnyhop.value == 1.f) {
		pathtracer_movement_t* pms_new = &pathtracer_movement_samples[pathtracer_movement_write_head];
		// Array element has data, ring buffer wrapped around
		if (pms_new->holdsData) {
			pathtracer_movement_read_head = pathtracer_movement_write_head + 1;
			if (pathtracer_movement_read_head >= PATHTRACER_MOVEMENT_BUFFER_MAX) {
				pathtracer_movement_read_head = 0;
			}
		}
		pms_new->holdsData = true;

		pms_new->pos[0] = sv_player->v.origin[0];
		pms_new->pos[1] = sv_player->v.origin[1];
		pms_new->pos[2] = sv_player->v.origin[2];
		pms_new->velocity[0] = sv_player->v.velocity[0];
		pms_new->velocity[1] = sv_player->v.velocity[1];
		pms_new->velocity[2] = sv_player->v.velocity[2];
		pms_new->angle = sv_player->v.angles[1];

		if (ghost_entity.model != NULL) {
			pms_new->pos[0] = ghost_entity.origin[0];
			pms_new->pos[1] = ghost_entity.origin[1];
			pms_new->pos[2] = ghost_entity.origin[2];
			pms_new->velocity[0] = ghost_entity.origin[0] - ghost_entity.previousorigin[0];
			pms_new->velocity[1] = ghost_entity.origin[1] - ghost_entity.previousorigin[1];
			pms_new->velocity[2] = ghost_entity.origin[2] - ghost_entity.previousorigin[2];
			pms_new->velocity[0] *= 200.f;
			pms_new->velocity[1] *= 200.f;
			pms_new->velocity[2] *= 200.f;
			pms_new->angle = ghost_entity.angles[1];
		}

		pms_new->bestangle = bestangle;
		pms_new->speed = speed;
		pms_new->bestspeed = bestspeed;
		pms_new->onground = onground0;
		pathtracer_movement_write_head++;
		if (pathtracer_movement_write_head >= PATHTRACER_MOVEMENT_BUFFER_MAX) {
			pathtracer_movement_write_head = 0;
		}
	}
}

static void PathTracer_Debug_f (void)
{
    if (cmd_source != src_command) {
        return;
    }

	Con_Printf("pathtracker_debug : Just a test\n");
}

void PathTracer_Init (void)
{
    Cmd_AddCommand ("pathtracer_debug", PathTracer_Debug_f);

    Cvar_Register (&scr_printbunnyhop);
    Cvar_Register (&scr_recordbunnyhop);
}


void PathTracer_Shutdown (void)
{
}
