// aoi

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <assert.h>


#define DEFAULT_OBJ_SIZE 32
#define OBJ_MASK 0xffffff

#define AOI_MARKER  0x01
#define AOI_WATCHER 0x02

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

typedef void (*aoi_free)(void *ud);

typedef struct aoi_node aoi_node;
typedef struct aoi_tower aoi_tower;

typedef struct aoi_set {
	int cap;
	int len;
	void **slot; // 里面的元素指针由外部创建好后传入
} aoi_set;

typedef struct aoi_node {
	int id;
	aoi_tower *tower;
	aoi_node *prev;
	aoi_node *next;
} aoi_node;

typedef struct aoi_list {
	aoi_node *head;
	aoi_node *tail;
} aoi_list;

typedef struct aoi_obj {
	int id;
	int pos[3];
	uint8_t mode;
	bool inuse;
	aoi_node *marker_node;
	aoi_set *watcher_nodes;
} aoi_obj;

typedef struct aoi_tower {
	int idx;
	int pos[3];
	aoi_list marker_list;
	aoi_list watcher_list;
} aoi_tower;

typedef struct aoi_result {
	aoi_set *leave_send;
	aoi_set *enter_send;
	aoi_set *leave_recv;
	aoi_set *enter_recv;
} aoi_result;

typedef struct aoi_t {
	int map_size[3];
	int tower_size[3];
	int tower_limit[3];
	int radius_limit[3];
	int around_cap;

	/* obj */
	aoi_set *obj_set;
	aoi_set *rcv_set;

	/* tower */
	aoi_tower *tower_list;
	aoi_set *tmp_set;
	aoi_set *enter_set;
	aoi_set *leave_set;

	/* result */
	aoi_result result;
} aoi_t;


/************* aoi_private *************/
/* aoi set */
static aoi_set *
_aoi_set_new(int cap) {
	aoi_set *set = malloc(sizeof(*set));
	set->cap = cap;
	set->len = 0;
	set->slot = malloc(sizeof(void *) * set->cap);
	memset(set->slot, 0, sizeof(void *) * set->cap);
	return set;
}

static int
_aoi_set_add(aoi_set *set, void *elem) {
	if (set->len >= set->cap) {
		int new_cap = set->cap * 2;
		assert((new_cap - 1) < OBJ_MASK);

		set->slot = realloc(set->slot, new_cap);
		set->cap = new_cap;
	}
	set->slot[set->len++] = elem;
	return set->len;
}

static void
_aoi_set_del(aoi_set *set, void *elem) {
	for (int i = 0; i < set->len; ++i) {
		if (set->slot[i] == elem) {
			if (i < (set->len - 1)) {
				memmove(set->slot+i, set->slot+i+1, (set->len-i-1)*sizeof(void *));
			}
			set->slot[--set->len] = NULL;
			break;
		}
	}
}

static void *
_aoi_set_pop(aoi_set *set) {
	if (set->len > 0) {
		int idx = --set->len;
		void* elem = set->slot[idx];
		set->slot[idx] = NULL;
		return elem;
	}
	return NULL;
}

static void
_aoi_set_clean(aoi_set *set) {
	int len = set->len;
	for (int i = 0; i < len; ++i) {
		set->slot[--set->len] = NULL;
	}
}

static void
_aoi_set_free(aoi_set *set) {
	set->cap = set->len = 0;
	free(set->slot);
	free(set);
}

static void
_aoi_set_destory(aoi_set *set, aoi_free cb) {
	for (int i = 0; i < set->len; ++i) {
		if (cb) {
			cb(set->slot[i]);
		} else {
			free(set->slot[i]);
		}
	}

	_aoi_set_free(set);
}

static void
_aoi_set_dump(const char *prefix, aoi_set *set) {
	if (set->len > 0) {
		printf("%s set = ", prefix);
		for (int i = 0; i < set->len; ++i) {
			printf("%p ", set->slot[i]);
		}
		printf("\n");
	}
}

/* aoi tower */
static int
_aoi_tower_idx(aoi_t *a, int tower_x, int tower_y, int tower_z) {
	int tidx = tower_z*(a->tower_limit[0]*a->tower_limit[1]) + tower_y*a->tower_limit[0] + tower_x;
	return tidx;
}

static int
_aoi_fix_pos(int v, int maxv) {
	v = MAX(v, 0);
	v = MIN(v, maxv);
	return v;
}

static int
_offset_tower(aoi_t *a, aoi_tower *t, int idx, int offset) {
	int val = t->pos[idx] + offset;
	return _aoi_fix_pos(val, a->tower_limit[idx] - 1);
}

static aoi_set *
_aoi_around_towers(aoi_t *a, aoi_tower *t, aoi_set *set) {
	int x1 = _offset_tower(a, t, 0, -a->radius_limit[0]);
	int x2 = _offset_tower(a, t, 0, a->radius_limit[0]);
	int y1 = _offset_tower(a, t, 1, -a->radius_limit[1]);
	int y2 = _offset_tower(a, t, 1, a->radius_limit[1]);
	int z1 = _offset_tower(a, t, 2, -a->radius_limit[2]);
	int z2 = _offset_tower(a, t, 2, a->radius_limit[2]);

	_aoi_set_clean(set);
	for (int z = z1; z <= z2; ++z) {
		for (int y = y1; y <= y2; ++y) {
			for (int x = x1; x <= x2; ++x) {
				int tidx = _aoi_tower_idx(a, x, y, z);
				aoi_tower *t = &a->tower_list[tidx];
				_aoi_set_add(set, t);
			}
		}
	}
	return set;
}

static aoi_tower *
_aoi_locate_tower(aoi_t *a, int x, int y, int z) {
	int tower_x = (int)ceil((x+1)*1.0 / a->tower_size[0]) - 1;
	int tower_y = (int)ceil((y+1)*1.0 / a->tower_size[1]) - 1;
	int tower_z = (int)ceil((z+1)*1.0 / a->tower_size[2]) - 1;
	int tidx = _aoi_tower_idx(a, tower_x, tower_y, tower_z);
	aoi_tower *t = &a->tower_list[tidx];
	return t;
}

static void
_aoi_list_add(aoi_list *list, aoi_node *node) {
	if (list->tail){
		list->tail->next = node;
		node->prev = list->tail;
		list->tail = node;
	} else {
		list->head = list->tail = node;
	}
}

static void
_aoi_list_del(aoi_list *list, aoi_node *node) {
	if (node == list->head) {
		list->head = node->next;
		if (node->next) {
			node->next->prev = NULL;
		} else {
			list->head = list->tail = NULL;
		}
	} else if (node == list->tail) {
		list->tail = node->prev;
		if (node->prev) {
			node->prev->next = NULL;
		} else {
			list->head = list->tail = NULL;
		}
	} else {
		node->prev->next = node->next;
		node->next->prev = node->prev;
	}

	node->next = node->prev = NULL;
	node->tower = NULL;
}

static void
_aoi_result_add(aoi_tower *t, int self, uint8_t mode, aoi_result *result, bool isenter) {
	if (result == NULL){
		return;
	}

	aoi_node *node = NULL;
	if (mode & AOI_MARKER) {
		// 进入/离开后广播消息给观察者
		aoi_set *sends = isenter ? result->enter_send : result->leave_send;

		node = t->watcher_list.head;
		while(node != NULL) {
			if (node->id != self) {
				_aoi_set_add(sends, (void *)(uintptr_t)node->id);
			}
			node = node->next;
		}
	}

	if (mode & AOI_WATCHER) {
		// 离开/进入后更新被观察者列表
		aoi_set *recvs = isenter ? result->enter_recv : result->leave_recv;

		node = t->marker_list.head;
		while(node != NULL) {
			if (node->id != self) {
				_aoi_set_add(recvs, (void *)(uintptr_t)node->id);
			}
			node = node->next;
		}
	}
}

static void
_aoi_result_clear(aoi_result *result) {
	_aoi_set_clean(result->enter_send);
	_aoi_set_clean(result->enter_recv);
	_aoi_set_clean(result->leave_send);
	_aoi_set_clean(result->leave_recv);
}

static void
_aoi_towerlist_add(aoi_tower *t, aoi_obj *obj, uint8_t mode, aoi_result *result) {
	int id = obj->id;

	if (mode & AOI_MARKER) {
		aoi_node *mnode = obj->marker_node;
		if (mnode) {
			assert(mnode->tower == NULL);
			mnode->tower = t;
		} else {
			mnode = malloc(sizeof(*mnode));
			mnode->id = id;
			mnode->tower = t;
			mnode->prev = NULL;
			mnode->next = NULL;
			obj->marker_node = mnode;
		}

		_aoi_list_add(&t->marker_list, mnode);
		_aoi_result_add(t, id, mode, result, true);
	}

	if (mode & AOI_WATCHER) {
		aoi_node *wnode = NULL;
		for (int i = 0; i < obj->watcher_nodes->len; ++i) {
			aoi_node *wn = (aoi_node *)obj->watcher_nodes->slot[i];
			if (wn->tower == t) {
				assert(0);
				return;
			} else if (wn->tower == NULL) {
				wnode = wn;
				break;
			}
		}

		if (wnode) {
			wnode->tower = t;
		} else {
			wnode = malloc(sizeof(*wnode));
			wnode->id = id;
			wnode->tower = t;
			wnode->prev = NULL;
			wnode->next = NULL;
			_aoi_set_add(obj->watcher_nodes, wnode);
		}

		_aoi_list_add(&t->watcher_list, wnode);
		_aoi_result_add(t, id, mode, result, true);
	}
}

static void
_aoi_towerlist_del(aoi_tower *t, aoi_obj *obj, uint8_t mode, aoi_result *result) {
	if (mode & AOI_MARKER) {
		aoi_node *mnode = obj->marker_node;
		assert(mnode->tower == t);

		_aoi_list_del(&t->marker_list, mnode);
		_aoi_result_add(t, obj->id, mode, result, false);
	}

	if (mode & AOI_WATCHER) {
		for (int i = 0; i < obj->watcher_nodes->len; ++i) {
			aoi_node *wnode = (aoi_node *)obj->watcher_nodes->slot[i];
			if (wnode->tower == t) {
				_aoi_list_del(&t->watcher_list, wnode);
				_aoi_result_add(t, obj->id, mode, result, false);
				break;
			}
		}
	}
}

static void
_aoi_tower_diff(aoi_t *a, aoi_tower *ot, aoi_tower *nt) {
	// clear
	_aoi_set_clean(a->enter_set);
	_aoi_set_clean(a->leave_set);

	int dx = nt->pos[0] - ot->pos[0];
	int dy = nt->pos[1] - ot->pos[1];
	int dz = nt->pos[2] - ot->pos[2];

	int dx_abs = abs(dx);
	int dy_abs = abs(dy);
	int dz_abs = abs(dz);

	if (dx_abs <= 2*a->radius_limit[0]
		 && dy_abs <= 2*a->radius_limit[1]
		 && dz_abs <= 2*a->radius_limit[2]) {
		int maxv;

		int range1[3][2] = {0};
		for (int i = 0; i < 3; ++i) {
			maxv = a->tower_limit[i] - 1;
			range1[i][0] = _aoi_fix_pos(ot->pos[i] - a->radius_limit[i], maxv);
			range1[i][1] = _aoi_fix_pos(ot->pos[i] + a->radius_limit[i], maxv);
		}

		int range2[3][2] = {0};
		for (int i = 0; i < 3; ++i) {
			maxv = a->tower_limit[0] - 1;
			range2[i][0] = _aoi_fix_pos(nt->pos[i] - a->radius_limit[i], maxv);
			range2[i][1] = _aoi_fix_pos(nt->pos[i] + a->radius_limit[i], maxv);
		}

		int dir = 0;
		int tidx = -1;

		// x 轴
		dir = dx/dx_abs;
		for (int i = 0; i < dx_abs; ++i) {
			if (a->radius_limit[0] > 0) {
				int x0 = ot->pos[0] + i*dir;
				int x1 = x0 - dir*a->radius_limit[0];
				int x2 = x0 + dir*(a->radius_limit[0]+1);

				for (int y = range1[1][0]; y <= range1[1][1]; ++y) {
					for (int z = range1[2][0]; z <= range1[2][1]; ++z) {
						// del
						if (x1 >= 0 && x1 < a->tower_limit[0]) {
							tidx = _aoi_tower_idx(a, x1, y, z);
							aoi_tower *t = &a->tower_list[tidx];
							_aoi_set_add(a->leave_set, t);
						}

						// add
						if (x2 >= 0 && x2 < a->tower_limit[0]) {
							if (y >= range2[1][0] && y <= range2[1][1]
								&& z >= range2[2][0] && z <= range2[2][1]) {
								tidx = _aoi_tower_idx(a, x2, y, z);
								aoi_tower *t = &a->tower_list[tidx];
								_aoi_set_add(a->enter_set, t);
							}
						}
					}
				}
			}
		}

		// y 轴
		dir = dy/dy_abs;
		for (int i = 0; i < dy_abs; ++i) {
			if (a->radius_limit[1] > 0) {
				int y0 = ot->pos[1] + i*dir;
				int y1 = y0 - dir*a->radius_limit[1];
				int y2 = y0 + dir*(a->radius_limit[1]+1);

				for (int x = range2[1][0]; x <= range2[1][1]; ++x) {
					for (int z = range1[2][0]; z <= range1[2][1]; ++z) {
						// del
						if (y1 >= 0 && y1 < a->tower_limit[1]) {
							if (x >= range1[0][0] && x <= range1[0][1]) {
								tidx = _aoi_tower_idx(a, x, y1, z);
								aoi_tower *t = &a->tower_list[tidx];
								_aoi_set_add(a->leave_set, t);
							}
						}

						// add
						if (y2 >= 0 && y2 < a->tower_limit[1]) {
							if (z >= range2[2][0] && z <= range2[2][1]) {
								tidx = _aoi_tower_idx(a, x, y2, z);
								aoi_tower *t = &a->tower_list[tidx];
								_aoi_set_add(a->enter_set, t);
							}
						}
					}
				}
			}
		}

		// z 轴
		dir = dz/dz_abs;
		for (int i = 0; i < dz_abs; ++i) {
			if (a->radius_limit[2] > 0) {
				int z0 = ot->pos[2] + i*dir;
				int z1 = z0 - dir*a->radius_limit[2];
				int z2 = z0 + dir*(a->radius_limit[2]+1);

				for (int x = range2[1][0]; x <= range2[1][1]; ++x) {
					for (int y = range2[2][0]; y <= range2[2][1]; ++y) {
						// del
						if (z1 >= 0 && z1 < a->tower_limit[2]) {
							if (x >= range1[0][0] && x <= range1[0][1]
								 && y >= range1[1][0] && y <= range1[1][1]) {
								tidx = _aoi_tower_idx(a, x, y, z1);
								aoi_tower *t = &a->tower_list[tidx];
								_aoi_set_add(a->leave_set, t);
							}
						}

						// add
						if (z1 >= 0 && z1 < a->tower_limit[2]) {
							tidx = _aoi_tower_idx(a, x, y, z2);
							aoi_tower *t = &a->tower_list[tidx];
							_aoi_set_add(a->enter_set, t);
						}
					}
				}
			}
		}
	} else {
		_aoi_around_towers(a, ot, a->leave_set);
		_aoi_around_towers(a, nt, a->enter_set);
	}
}


/* aoi object */
static aoi_obj *
_aoi_obj_new(aoi_t *a, uint8_t mode, int x, int y, int z) {
	aoi_obj *obj = _aoi_set_pop(a->rcv_set);

	if (obj == NULL){
		obj = (aoi_obj *)malloc(sizeof(*obj));
		obj->id = _aoi_set_add(a->obj_set, obj);
		obj->marker_node = NULL;
		obj->watcher_nodes = NULL;
	} else {
		assert(!obj->inuse);
	}

	obj->pos[0] = x;
	obj->pos[1] = y;
	obj->pos[2] = z;
	obj->mode = mode;
	obj->inuse = true;

	if (!obj->watcher_nodes && (mode & AOI_WATCHER)) {
		obj->watcher_nodes = _aoi_set_new(a->around_cap);
	}

	return obj;
}

static void
_aoi_obj_free(void *ud) {
	aoi_obj *obj = (aoi_obj *)ud;
	aoi_node *mnode = obj->marker_node;
	if (mnode) {
		free(mnode);
		obj->marker_node = NULL;
	}

	aoi_set *wnodes = obj->watcher_nodes;
	if (wnodes) {
		_aoi_set_destory(wnodes, NULL);
	}

	free(obj);
}



/************* aoi_public *************/
static aoi_t *
aoi_create(int *map_size, int *tower_size) {
	aoi_t *a = malloc(sizeof(*a));
	memset(a, 0, sizeof(*a));

	memcpy(a->map_size, map_size, 3*sizeof(int));
	memcpy(a->tower_size, tower_size, 3*sizeof(int));
	a->radius_limit[0] = 1;
	a->radius_limit[1] = 1;
	a->radius_limit[2] = 0;
	a->around_cap = (2*a->radius_limit[0]+1) * (2*a->radius_limit[1]+1) * (2*a->radius_limit[2]+1);

	// init object slot
	a->obj_set = _aoi_set_new(DEFAULT_OBJ_SIZE);
	a->rcv_set = _aoi_set_new(DEFAULT_OBJ_SIZE);

	// new tower list
	int x_limit = (int)ceil(a->map_size[0]*1.0 / a->tower_size[0]);
	int y_limit = (int)ceil(a->map_size[1]*1.0 / a->tower_size[1]);
	int z_limit = (int)ceil(a->map_size[2]*1.0 / a->tower_size[2]);
	a->tower_limit[0] = x_limit;
	a->tower_limit[1] = y_limit;
	a->tower_limit[2] = z_limit;

	int tower_amt = x_limit * y_limit * z_limit;
	assert(tower_amt > 0);
	printf("%d %d %d, tower_amt=%d\n", x_limit, y_limit, z_limit, tower_amt);
	a->tower_list = malloc(tower_amt * sizeof(aoi_tower));

	// init tower_list
	int idx = 0;
	for (int z = 0; z < z_limit; ++z) {
		for (int y = 0; y < y_limit; ++y) {
			for (int x = 0; x < x_limit; ++x) {
				aoi_tower *t = &a->tower_list[idx];
				int tmp = _aoi_tower_idx(a, x, y, z);
				assert(tmp == idx);
				printf("%d %d %d, idx=%d\n", x, y, z, idx);

				t->idx = idx;
				t->pos[0] = x;
				t->pos[1] = y;
				t->pos[2] = z;
				t->marker_list.head = t->marker_list.tail = NULL;
				t->watcher_list.head = t->watcher_list.tail = NULL;

				++idx;
			}
		}
	}

	// init set
	a->tmp_set = _aoi_set_new(a->around_cap);
	a->enter_set = _aoi_set_new(a->around_cap);
	a->leave_set = _aoi_set_new(a->around_cap);

	// ids
	a->result.leave_send = _aoi_set_new(4);
	a->result.enter_send = _aoi_set_new(4);
	a->result.leave_recv = _aoi_set_new(4);
	a->result.enter_recv = _aoi_set_new(4);

	return a;
}

static void
aoi_destroy(aoi_t *a) {
	// free tower
	free(a->tower_list);
	a->tower_list = NULL;

	// free objs
	_aoi_set_free(a->rcv_set);
	_aoi_set_destory(a->obj_set, _aoi_obj_free);

	// free set
	_aoi_set_free(a->tmp_set);
	_aoi_set_free(a->enter_set);
	_aoi_set_free(a->leave_set);

	// free result
	_aoi_set_free(a->result.leave_send);
	_aoi_set_free(a->result.enter_send);
	_aoi_set_free(a->result.leave_recv);
	_aoi_set_free(a->result.enter_recv);
	free(a);
}

static int
aoi_enter(aoi_t *a, uint8_t mode, int x, int y, int z) {
	assert(x>=0 && x < a->map_size[0]);
	assert(y>=0 && y < a->map_size[1]);
	assert(z>=0 && z < a->map_size[2]);

	// obj new
	aoi_obj *obj = _aoi_obj_new(a, mode, x, y, z);
	aoi_tower *lt = _aoi_locate_tower(a, x, y, z);
	printf("[%d] enter aoi, mode = %d, tower idx = %d\n", obj->id, mode, lt->idx);

	// aoi result
	aoi_result *result = &a->result;
	_aoi_result_clear(result);

	if (obj->mode & AOI_MARKER) {
		_aoi_towerlist_add(lt, obj, AOI_MARKER, result);
	}

	if (obj->mode & AOI_WATCHER) {
		aoi_set *set = _aoi_around_towers(a, lt, a->tmp_set);
		for (int i = 0; i < set->len; ++i) {
			aoi_tower *nt = set->slot[i];
			_aoi_towerlist_add(nt, obj, AOI_WATCHER, result);
		}
	}

	return obj->id;
}

static void
aoi_leave(aoi_t *a, int id) {
	if (id <= 0 || id > a->obj_set->cap) {
		printf("[leave] obj id error, id=%d\n", id);
		return;
	}

	aoi_obj *obj = (aoi_obj *)a->obj_set->slot[id-1];
	assert(obj->inuse);

	// aoi result
	aoi_result *result = &a->result;
	_aoi_result_clear(result);

	if (obj->mode & AOI_MARKER) {
		aoi_node *mnode = obj->marker_node;
		aoi_tower *lt = mnode->tower;

		_aoi_towerlist_del(lt, obj, AOI_MARKER, result);
	}

	if (obj->mode & AOI_WATCHER) {
		aoi_set *wnodes = obj->watcher_nodes;
		for (int i = 0; i < wnodes->len; ++i) {
			aoi_node *wn = (aoi_node*)wnodes->slot[i];
			_aoi_list_del(&wn->tower->watcher_list, wn);
		}
	}

	// push obj to recv set
	obj->inuse = false;
	_aoi_set_add(a->rcv_set, obj);
}

static void
aoi_move(aoi_t *a, int id, int x, int y, int z) {
	if (id <= 0 || id > a->obj_set->cap) {
		printf("[move] obj id error, id=%d\n", id);
		return;
	}

	aoi_obj *obj = a->obj_set->slot[id-1];
	int ox = obj->pos[0];
	int oy = obj->pos[1];
	int oz = obj->pos[2];
	if (ox == x && oy == y && oz == z) {
		printf("[move] obj same pos, id=%d\n", id);
		return;
	}

	// update object postion
	obj->pos[0] = x;
	obj->pos[1] = y;
	obj->pos[2] = z;

	aoi_tower *nt = _aoi_locate_tower(a, x, y, z);
	aoi_tower *ot = _aoi_locate_tower(a, ox, oy, oz);
	assert(ot == obj->marker_node->tower);
	if (ot->idx == nt->idx) {
		return;
	}

	// aoi result
	aoi_result *result = &a->result;
	_aoi_result_clear(result);

	if (obj->mode & AOI_MARKER) {
		_aoi_towerlist_del(ot, obj, AOI_MARKER, result);
		_aoi_towerlist_add(nt, obj, AOI_MARKER, result);
	}

	if (obj->mode & AOI_WATCHER) {
		// tower diff
		_aoi_tower_diff(a, ot, nt);

		for (int i = 0; i < obj->watcher_nodes->len; ++i) {
			aoi_node *wn = (aoi_node *)obj->watcher_nodes->slot[i];

			// del from leave tower set
			for (int j = 0; j < a->leave_set->len; ++j) {
				aoi_tower *lt = a->leave_set->slot[j];
				if (wn->tower && wn->tower == lt) {
					_aoi_set_del(a->leave_set, lt);

					_aoi_list_del(&lt->watcher_list, wn);
					_aoi_result_add(lt, id, AOI_WATCHER, result, false);

					// pop 并且复用
					aoi_tower *et = _aoi_set_pop(a->enter_set);
					if (et) {
						_aoi_list_add(&et->watcher_list, wn);
						_aoi_result_add(et, id, AOI_WATCHER, result, true);
					}
					break;
				}
			}
		}
		assert(a->leave_set->len == 0);

		for (int i = 0; i < a->enter_set->len; ++i) {
			aoi_tower *et = a->enter_set->slot[i];
			_aoi_towerlist_add(et, obj, AOI_WATCHER, result);
		}
	}
}

int main(int argc, char const *argv[]){
	printf("%f\n", 1*1.0/3);

	int maps[3] = {10, 10, 1};
	int towers[3] = {3, 3, 1};
	aoi_t *aoi_ptr = aoi_create(maps, towers);

	printf("\n----------- enter test -----------\n");
	int id1 = aoi_enter(aoi_ptr, AOI_MARKER|AOI_WATCHER, 0, 0, 0);
	int id2 = aoi_enter(aoi_ptr, AOI_MARKER|AOI_WATCHER, 3, 0, 0);
	int id3 = aoi_enter(aoi_ptr, AOI_WATCHER, 0, 3, 0);
	int id4 = aoi_enter(aoi_ptr, AOI_MARKER, 3, 3, 0);
	int id5 = aoi_enter(aoi_ptr, AOI_MARKER|AOI_WATCHER, 9, 3, 0);

	printf("\n----------- leave test -----------\n");
	//aoi_leave(aoi_ptr, id1);

	printf("\n----------- move test -----------\n");
	aoi_move(aoi_ptr, id1, 9, 0, 0);

	printf("\n----------- dump -----------\n");
	_aoi_set_dump("leave_send", aoi_ptr->result.leave_send);
	_aoi_set_dump("enter_send", aoi_ptr->result.enter_send);
	_aoi_set_dump("leave_recv", aoi_ptr->result.leave_recv);
	_aoi_set_dump("enter_recv", aoi_ptr->result.enter_recv);

	aoi_destroy(aoi_ptr);
	return 0;
}