#include "renderbuffer.h"
#include "spritepack.h"
#include "sprite.h"
#include "screen.h"
#include "texture.h"
#include "array.h"
#include "opengl.h"

#include <assert.h>
#include <string.h>

int
renderbuffer_add(struct render_buffer *rb, const struct vertex_pack vb[4], uint32_t color) {
	if (rb->object >= MAX_COMMBINE) {
		return 1;
	}

	struct quad *q = rb->vb + rb->object;
	int i;
	for (i=0;i<4;i++) {
		q->p[i].vp = vb[i];
		q->p[i].rgba[0] = (color >> 16) & 0xff;
		q->p[i].rgba[1] = (color >> 8) & 0xff;
		q->p[i].rgba[2] = (color) & 0xff;
		q->p[i].rgba[3] = (color >> 24) & 0xff;
	}
	if (++rb->object >= MAX_COMMBINE) {
		return 1;
	}
	return 0;

}

static int
update_tex(struct render_buffer *rb, int id) {
	if (rb->object == 0) {
		rb->texid = id;
	} else if (rb->texid != id) {
		return 1;
	}
	return 0;
}

static int
drawquad(struct render_buffer *rb, struct pack_picture *picture, const struct sprite_trans *arg) {
	struct matrix tmp;
	struct vertex_pack vb[4];
	int i,j;
	if (arg->mat == NULL) {
		matrix_identity(&tmp);
	} else {
		tmp = *arg->mat;
	}
	int *m = tmp.m;
	int object = rb->object;
	for (i=0;i<picture->n;i++) {
		struct pack_quad *q = &picture->rect[i];
		if (update_tex(rb, q->texid)) {
			rb->object = object;
			return -1;
		}
		for (j=0;j<4;j++) {
			int xx = q->screen_coord[j*2+0];
			int yy = q->screen_coord[j*2+1];
			vb[j].vx = (xx * m[0] + yy * m[2]) / 1024 + m[4];
			vb[j].vy = (xx * m[1] + yy * m[3]) / 1024 + m[5];
			vb[j].tx = q->texture_coord[j*2+0];
			vb[j].ty = q->texture_coord[j*2+1];
		}
		if (renderbuffer_add(rb, vb, arg->color)) {
			return 1;
		}
	}
	return 0;
}

static int
polygon_quad(struct render_buffer *rb, const struct vertex_pack *vbp, uint32_t color, int max, int index) {
	struct vertex_pack vb[4];
	int i;
	vb[0] = vbp[0]; // first point
	for (i=1;i<4;i++) {
		int j = i + index;
		int n = (j <= max) ? j : max;
		vb[i] = vbp[n];
	}
	return renderbuffer_add(rb, vb, color);
}

static int
add_polygon(struct render_buffer *rb, int n, const struct vertex_pack *vb, uint32_t color) {
	int i = 0;
	--n;
	do {
		if (polygon_quad(rb, vb, color, n, i)) {
			return 1;
		}
		i+=2;
	} while (i<n-1);
	return 0;
}

static int
drawpolygon(struct render_buffer *rb, struct pack_polygon *poly, const struct sprite_trans *arg) {
	struct matrix tmp;
	int i,j;
	if (arg->mat == NULL) {
		matrix_identity(&tmp);
	} else {
		tmp = *arg->mat;
	}
	int *m = tmp.m;
	int object = rb->object;
	for (i=0;i<poly->n;i++) {
		struct pack_poly *p = &poly->poly[i];
		if (update_tex(rb, p->texid)) {
			rb->object = object;
			return -1;
		}

		int pn = p->n;

		ARRAY(struct vertex_pack, vb, pn);

		for (j=0;j<pn;j++) {
			int xx = p->screen_coord[j*2+0];
			int yy = p->screen_coord[j*2+1];
		
			vb[j].vx = (xx * m[0] + yy * m[2]) / 1024 + m[4];
			vb[j].vy = (xx * m[1] + yy * m[3]) / 1024 + m[5];
			vb[j].tx = p->texture_coord[j*2+0];
			vb[j].ty = p->texture_coord[j*2+1];
		}
		if (add_polygon(rb, pn, vb, arg->color)) {
			rb->object = object;
			return 1;
		}
	}
	return 0;
}

static void
anchor_update(struct sprite *s, struct sprite_trans *arg) {
	struct matrix *r = s->s.mat;
	if (arg->mat == NULL) {
		matrix_identity(r);
	} else {
		*r = *arg->mat;
	}
}

int 
drawsprite(struct render_buffer *rb, struct sprite *s, struct sprite_trans * ts) {
	struct sprite_trans temp;
	struct matrix temp_matrix;
	struct sprite_trans *t = sprite_trans_mul(&s->t, ts, &temp, &temp_matrix);
	switch (s->type) {
	case TYPE_PICTURE:
		return drawquad(rb, s->s.pic, t);
	case TYPE_POLYGON:
		return drawpolygon(rb, s->s.poly, t);
	case TYPE_ANIMATION: {
		struct pack_animation *ani = s->s.ani;
		int frame = s->frame % s->total_frame;
		if (frame < 0) {
			frame += s->total_frame;
		}
		frame += s->start_frame;
		struct pack_frame * pf = &ani->frame[frame];
		int i;
		for (i=0;i<pf->n;i++) {
			struct pack_part *pp = &pf->part[i];
			int index = pp->component_id;
			struct sprite * child = s->data.children[index];
			if (child == NULL || child->visible == false) {
				continue;
			}
			struct sprite_trans temp2;
			struct matrix temp_matrix2;
			struct sprite_trans *ct = sprite_trans_mul(&pp->t, t, &temp2, &temp_matrix2);
			int r = drawsprite(rb, child, ct);
			if (r)
				return r;
		}
		return 0;
	}
	case TYPE_LABEL:
		if (s->data.rich_text) {
			// don't support draw label to renderbuffer
			return -1;
		}
		return 0;
	case TYPE_ANCHOR:
		if (s->data.anchor->ps){
			return -1;
		}
		anchor_update(s, t);
		return 0;
	case TYPE_PANNEL:
		if (s->data.scissor) {
			return -1;
		} else {
			return 0;
		}
	}
	return -1;
}

int 
renderbuffer_drawsprite(struct render_buffer *rb, struct sprite *s) {
	if (s->visible) {
		return drawsprite(rb, s, NULL);
	}
	return 0;
}

void
renderbuffer_upload(struct render_buffer *rb) {
	if (rb->vbid == 0) {
		GLuint id = 0;
		glGenBuffers(1, &id);
		rb->vbid = (unsigned int)id;
	}

	GLuint id = rb->vbid;
	glBindBuffer(GL_ARRAY_BUFFER, id);
	glBufferData(GL_ARRAY_BUFFER, sizeof(struct quad) * rb->object, rb->vb, GL_STATIC_DRAW);
}

void 
renderbuffer_unload(struct render_buffer *rb) {
	if (rb->vbid) {
		GLuint id = rb->vbid;
		glDeleteBuffers(1,&id);
		rb->vbid = 0;
	}
}

void 
renderbuffer_init(struct render_buffer *rb) {
	rb->object = 0;
	rb->texid = 0;
	rb->vbid = 0;
}
