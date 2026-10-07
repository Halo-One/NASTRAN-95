/* HALO: an integer-keyed map for the front end's model tables.
 *
 * Grid to coordinates, property id to its fields, grid to which of its
 * six dofs anything is attached to, old id to renumbered id. All of
 * them are "look this integer up, often, in a table built once", and a
 * linear scan over a 1,400 grid model inside a loop over 800 rigid
 * elements is a second of wall clock for no reason.
 *
 * Open addressing, power-of-two capacity, linear probing, no deletion.
 * The value is a small fixed payload so that one map serves every use.
 */
#include "msc.h"
#include <stdlib.h>
#include <string.h>

void msc_map_init(msc_map *m, int hint)
{
    int cap = 16;
    while (cap < hint * 2) cap <<= 1;
    m->s = (msc_slot *) msc_alloc((size_t) cap * sizeof(msc_slot));
    m->cap = cap;
    m->n = 0;
}

void msc_map_free(msc_map *m)
{
    free(m->s);
    memset(m, 0, sizeof(*m));
}

static unsigned hash_int(int k)
{
    unsigned x = (unsigned) k;
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static void map_grow(msc_map *m);

/* find the slot for `key`, creating it when `make` is set */
msc_slot *msc_map_slot(msc_map *m, int key, int make)
{
    unsigned h;
    int      i;
    if (!m->s) msc_map_init(m, 64);
    if (make && (m->n + 1) * 4 >= m->cap * 3) map_grow(m);
    h = hash_int(key);
    i = (int) (h & (unsigned) (m->cap - 1));
    for (;;) {
        if (!m->s[i].used) {
            if (!make) return NULL;
            m->s[i].used = 1;
            m->s[i].key = key;
            m->n++;
            return &m->s[i];
        }
        if (m->s[i].key == key) return &m->s[i];
        i = (i + 1) & (m->cap - 1);
    }
}

static void map_grow(msc_map *m)
{
    msc_slot *old = m->s;
    int       oldcap = m->cap, i;
    m->cap = oldcap * 2;
    m->s = (msc_slot *) msc_alloc((size_t) m->cap * sizeof(msc_slot));
    m->n = 0;
    for (i = 0; i < oldcap; i++) {
        if (old[i].used) {
            msc_slot *t = msc_map_slot(m, old[i].key, 1);
            t->iv = old[i].iv;
            memcpy(t->dv, old[i].dv, sizeof(t->dv));
        }
    }
    free(old);
}

int msc_map_get(msc_map *m, int key, int dflt)
{
    msc_slot *s = msc_map_slot(m, key, 0);
    return s ? s->iv : dflt;
}

void msc_map_put(msc_map *m, int key, int val)
{
    msc_map_slot(m, key, 1)->iv = val;
}

int msc_map_has(msc_map *m, int key)
{
    return msc_map_slot(m, key, 0) != NULL;
}

void msc_map_add(msc_map *m, int key, int inc)
{
    msc_map_slot(m, key, 1)->iv += inc;
}

double *msc_map_vec(msc_map *m, int key, int make)
{
    msc_slot *s = msc_map_slot(m, key, make);
    return s ? s->dv : NULL;
}

int msc_map_count(msc_map *m) { return m->n; }

/* iterate: k from 0 to capacity, returns 0 when the slot is empty */
int msc_map_next(msc_map *m, int *iter, int *key, int *val)
{
    while (m->s && *iter < m->cap) {
        int i = (*iter)++;
        if (m->s[i].used) {
            if (key) *key = m->s[i].key;
            if (val) *val = m->s[i].iv;
            return 1;
        }
    }
    return 0;
}
