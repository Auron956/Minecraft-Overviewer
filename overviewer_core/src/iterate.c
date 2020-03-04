/* 
 * This file is part of the Minecraft Overviewer.
 *
 * Minecraft Overviewer is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or (at
 * your option) any later version.
 *
 * Minecraft Overviewer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with the Overviewer.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "block_class.h"
#include "mc_id.h"
#include "overviewer.h"

static PyObject* textures = NULL;

uint32_t max_blockid = 0;
uint32_t max_data = 0;
uint8_t* block_properties = NULL;

static PyObject* known_blocks = NULL;
static PyObject* transparent_blocks = NULL;
static PyObject* solid_blocks = NULL;
static PyObject* fluid_blocks = NULL;
static PyObject* nospawn_blocks = NULL;
static PyObject* nodata_blocks = NULL;

PyObject* init_chunk_render(void) {

    PyObject* tmp = NULL;
    uint32_t i;

    /* this function only needs to be called once, anything more should be
     * ignored */
    if (textures) {
        Py_RETURN_NONE;
    }
    textures = PyImport_ImportModule("overviewer_core.textures");
    /* ensure none of these pointers are NULL */
    if ((!textures)) {
        return NULL;
    }

    tmp = PyObject_GetAttrString(textures, "max_blockid");
    if (!tmp)
        return NULL;
    max_blockid = PyLong_AsLong(tmp);
    Py_DECREF(tmp);

    tmp = PyObject_GetAttrString(textures, "max_data");
    if (!tmp)
        return NULL;
    max_data = PyLong_AsLong(tmp);
    Py_DECREF(tmp);

    /* assemble the property table */
    known_blocks = PyObject_GetAttrString(textures, "known_blocks");
    if (!known_blocks)
        return NULL;
    transparent_blocks = PyObject_GetAttrString(textures, "transparent_blocks");
    if (!transparent_blocks)
        return NULL;
    solid_blocks = PyObject_GetAttrString(textures, "solid_blocks");
    if (!solid_blocks)
        return NULL;
    fluid_blocks = PyObject_GetAttrString(textures, "fluid_blocks");
    if (!fluid_blocks)
        return NULL;
    nospawn_blocks = PyObject_GetAttrString(textures, "nospawn_blocks");
    if (!nospawn_blocks)
        return NULL;
    nodata_blocks = PyObject_GetAttrString(textures, "nodata_blocks");
    if (!nodata_blocks)
        return NULL;

    block_properties = calloc(max_blockid, sizeof(uint8_t));
    for (i = 0; i < max_blockid; i++) {
        PyObject* block = PyLong_FromLong(i);

        if (PySequence_Contains(known_blocks, block))
            block_properties[i] |= 1 << KNOWN;
        if (PySequence_Contains(transparent_blocks, block))
            block_properties[i] |= 1 << TRANSPARENT;
        if (PySequence_Contains(solid_blocks, block))
            block_properties[i] |= 1 << SOLID;
        if (PySequence_Contains(fluid_blocks, block))
            block_properties[i] |= 1 << FLUID;
        if (PySequence_Contains(nospawn_blocks, block))
            block_properties[i] |= 1 << NOSPAWN;
        if (PySequence_Contains(nodata_blocks, block))
            block_properties[i] |= 1 << NODATA;

        Py_DECREF(block);
    }

    Py_RETURN_NONE;
}

/* helper for load_chunk, loads a section into a chunk */
static inline void load_chunk_section(ChunkData* dest, int32_t i, PyObject* section) {
    dest->sections[i].blocks = (PyArrayObject*)PyDict_GetItemString(section, "Blocks");
    dest->sections[i].data = (PyArrayObject*)PyDict_GetItemString(section, "Data");
    dest->sections[i].skylight = (PyArrayObject*)PyDict_GetItemString(section, "SkyLight");
    dest->sections[i].blocklight = (PyArrayObject*)PyDict_GetItemString(section, "BlockLight");
    Py_INCREF(dest->sections[i].blocks);
    Py_INCREF(dest->sections[i].data);
    Py_INCREF(dest->sections[i].skylight);
    Py_INCREF(dest->sections[i].blocklight);
}

/* loads the given chunk into the chunks[] array in the state
 * returns true on error
 *
 * if required is true, failure to load the chunk will raise a python
 * exception and return true.
 */
bool load_chunk(RenderState* state, int32_t x, int32_t z, uint8_t required) {
    ChunkData* dest = &(state->chunks[1 + x][1 + z]);
    int32_t i;
    PyObject* chunk = NULL;
    PyObject* sections = NULL;

    if (dest->loaded)
        return false;

    /* set reasonable defaults */
    dest->biomes = NULL;
    for (i = 0; i < SECTIONS_PER_CHUNK; i++) {
        dest->sections[i].blocks = NULL;
        dest->sections[i].data = NULL;
        dest->sections[i].skylight = NULL;
        dest->sections[i].blocklight = NULL;
    }
    dest->loaded = 1;

    x += state->chunkx;
    z += state->chunkz;

    chunk = PyObject_CallMethod(state->regionset, "get_chunk", "ii", x, z);
    if (chunk == NULL) {
        // An exception is already set. RegionSet.get_chunk sets
        // ChunkDoesntExist
        if (!required) {
            PyErr_Clear();
        }
        return true;
    }

    sections = PyDict_GetItemString(chunk, "Sections");
    if (sections) {
        sections = PySequence_Fast(sections, "Sections tag was not a list!");
    }
    if (sections == NULL) {
        // exception set, again
        Py_DECREF(chunk);
        if (!required) {
            PyErr_Clear();
        }
        return true;
    }

    dest->biomes = (PyArrayObject*)PyDict_GetItemString(chunk, "Biomes");
    Py_INCREF(dest->biomes);
    dest->new_biomes = PyObject_IsTrue(PyDict_GetItemString(chunk, "NewBiomes"));

    for (i = 0; i < PySequence_Fast_GET_SIZE(sections); i++) {
        PyObject* ycoord = NULL;
        int32_t sectiony = 0;
        PyObject* section = PySequence_Fast_GET_ITEM(sections, i);
        ycoord = PyDict_GetItemString(section, "Y");
        if (!ycoord)
            continue;

        sectiony = PyLong_AsLong(ycoord);
        if (sectiony >= 0 && sectiony < SECTIONS_PER_CHUNK)
            load_chunk_section(dest, sectiony, section);
    }
    Py_DECREF(sections);
    Py_DECREF(chunk);

    return false;
}

/* helper to unload all loaded chunks */
static void
unload_all_chunks(RenderState* state) {
    uint32_t i, j, k;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            if (state->chunks[i][j].loaded) {
                Py_XDECREF(state->chunks[i][j].biomes);
                for (k = 0; k < SECTIONS_PER_CHUNK; k++) {
                    Py_XDECREF(state->chunks[i][j].sections[k].blocks);
                    Py_XDECREF(state->chunks[i][j].sections[k].data);
                    Py_XDECREF(state->chunks[i][j].sections[k].skylight);
                    Py_XDECREF(state->chunks[i][j].sections[k].blocklight);
                }
                state->chunks[i][j].loaded = 0;
            }
        }
    }
}

uint16_t
check_adjacent_blocks(RenderState* state, int32_t x, int32_t y, int32_t z, mc_block_t blockid) {
    /*
     * Generates a pseudo ancillary data for blocks that depend of 
     * what are surrounded and don't have ancillary data. This 
     * function is used through generate_pseudo_data.
     *
     * This uses a binary number of 4 digits to encode the info:
     *
     * 0b1234:
     * Bit:   1   2   3   4
     * Side: +x  +z  -x  -z
     * Values: bit = 0 -> The corresponding side block has different blockid
     *         bit = 1 -> The corresponding side block has same blockid
     * Example: if the bit1 is 1 that means that there is a block with 
     * blockid in the side of the +x direction.
     */

    uint8_t pdata = 0;

    if (get_data(state, BLOCKS, x + 1, y, z) == blockid) {
        pdata = pdata | (1 << 3);
    }
    if (get_data(state, BLOCKS, x, y, z + 1) == blockid) {
        pdata = pdata | (1 << 2);
    }
    if (get_data(state, BLOCKS, x - 1, y, z) == blockid) {
        pdata = pdata | (1 << 1);
    }
    if (get_data(state, BLOCKS, x, y, z - 1) == blockid) {
        pdata = pdata | (1 << 0);
    }

    return pdata;
}

uint16_t
generate_pseudo_data(RenderState* state, uint16_t ancilData) {
    /*
     * Generates a fake ancillary data for blocks that are drawn 
     * depending on what are surrounded.
     */
    int32_t x = state->x, y = state->y, z = state->z;
    uint16_t data = 0;

    if (block_class_is_subset(state->block, (mc_block_t[]){block_flowing_water, block_water}, 2)) { /* water */
        data = check_adjacent_blocks(state, x, y, z, state->block) ^ 0x0f;
        /* an aditional bit for top is added to the 4 bits of check_adjacent_blocks */
        if (get_data(state, BLOCKS, x, y + 1, z) != state->block)
            data |= 0x10;
        return data;
    } else if (block_class_is_subset(state->block, (mc_block_t[]){block_glass, block_ice, block_stained_glass}, 3)) { /* glass and ice and stained glass*/
        /* an aditional bit for top is added to the 4 bits of check_adjacent_blocks
         * Note that stained glass encodes 16 colors using 4 bits.  this pushes us over the 8-bits of an uint8_t, 
         * forcing us to use an uint16_t to hold 16 bits of pseudo ancil data
         * */
        if ((get_data(state, BLOCKS, x, y + 1, z) == 20) || (get_data(state, BLOCKS, x, y + 1, z) == 95)) {
            data = 0;
        } else {
            data = 16;
        }
        data = (check_adjacent_blocks(state, x, y, z, state->block) ^ 0x0f) | data;
        return (data << 4) | (ancilData & 0x0f);

    } else if (state->block == block_portal) {
        /* portal  */
        return check_adjacent_blocks(state, x, y, z, state->block);

    } else if (state->block == block_waterlily) {
        int32_t wx, wz, wy, rotation;
        int64_t pr;
        /* calculate the global block coordinates of this position */
        wx = (state->chunkx * 16) + x;
        wz = (state->chunkz * 16) + z;
        wy = (state->chunky * 16) + y;
        /* lilypads orientation is obtained with these magic numbers */
        /* magic numbers obtained from: */
        /* http://llbit.se/?p=1537 */
        pr = (wx * 3129871) ^ (wz * 116129781) ^ (wy);
        pr = pr * pr * 42317861 + pr * 11;
        rotation = 3 & (pr >> 16);
        return rotation;
    } else if (state->block == block_double_plant) { /* doublePlants */
        /* use bottom block data format plus one bit for top
         * block (0x8)
         */
        if (get_data(state, BLOCKS, x, y - 1, z) == block_double_plant) {
            data = get_data(state, DATA, x, y - 1, z) | 0x8;
        } else {
            data = ancilData;
        }

        return data;
    }

    return 0;
}

/* TODO triple check this to make sure reference counting is correct */
PyObject*
chunk_render(PyObject* self, PyObject* args) {
    RenderState state;
    PyObject* modeobj;
    PyObject* blockmap;

    int32_t xoff, yoff;

    PyObject *imgsize, *imgsize0_py, *imgsize1_py;
    int32_t imgsize0, imgsize1;

    PyArrayObject* blocks_py;
    PyArrayObject* left_blocks_py;
    PyArrayObject* right_blocks_py;
    PyArrayObject* up_left_blocks_py;
    PyArrayObject* up_right_blocks_py;

    RenderMode* rendermode;

    int32_t i, j;

    PyObject* t = NULL;

    if (!PyArg_ParseTuple(args, "OOiiiOiiOO", &state.world, &state.regionset, &state.chunkx, &state.chunky, &state.chunkz, &state.img, &xoff, &yoff, &modeobj, &state.textures))
        return NULL;

    /* set up the render mode */
    state.rendermode = rendermode = render_mode_create(modeobj, &state);
    if (rendermode == NULL) {
        return NULL; // note that render_mode_create will
                     // set PyErr.  No need to set it here
    }

    /* get the blockmap from the textures object */
    blockmap = PyObject_GetAttrString(state.textures, "blockmap");
    if (blockmap == NULL) {
        render_mode_destroy(rendermode);
        return NULL;
    }
    if (blockmap == Py_None) {
        render_mode_destroy(rendermode);
        PyErr_SetString(PyExc_RuntimeError, "you must call Textures.generate()");
        return NULL;
    }

    /* get the image size */
    imgsize = PyObject_GetAttrString(state.img, "size");

    imgsize0_py = PySequence_GetItem(imgsize, 0);
    imgsize1_py = PySequence_GetItem(imgsize, 1);
    Py_DECREF(imgsize);

    imgsize0 = PyLong_AsLong(imgsize0_py);
    imgsize1 = PyLong_AsLong(imgsize1_py);
    Py_DECREF(imgsize0_py);
    Py_DECREF(imgsize1_py);

    /* set all block data to unloaded */
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            state.chunks[i][j].loaded = 0;
        }
    }

    /* get the block data for the center column, erroring out if needed */
    if (load_chunk(&state, 0, 0, 1)) {
        render_mode_destroy(rendermode);
        Py_DECREF(blockmap);
        return NULL;
    }
    if (state.chunks[1][1].sections[state.chunky].blocks == NULL) {
        /* this section doesn't exist, let's skeddadle */
        render_mode_destroy(rendermode);
        Py_DECREF(blockmap);
        unload_all_chunks(&state);
        Py_RETURN_NONE;
    }

    /* set blocks_py, state.blocks, and state.blockdatas as convenience */
    blocks_py = state.blocks = state.chunks[1][1].sections[state.chunky].blocks;
    state.blockdatas = state.chunks[1][1].sections[state.chunky].data;

    /* set up the random number generator again for each chunk
       so tallgrass is in the same place, no matter what mode is used */
    srand(1);

    for (state.x = 15; state.x > -1; state.x--) {
        for (state.z = 0; state.z < 16; state.z++) {

            /* set up the render coordinates */
            state.imgx = xoff + state.x * 12 + state.z * 12;
            /* 16*12 -- offset for y direction, 15*6 -- offset for x */
            state.imgy = yoff - state.x * 6 + state.z * 6 + 16 * 12 + 15 * 6;

            for (state.y = 0; state.y < 16; state.y++) {
                uint16_t ancilData;

                state.imgy -= 12;
                /* get blockid */
                state.block = getArrayShort3D(blocks_py, state.x, state.y, state.z);
                if (state.block == block_air || render_mode_hidden(rendermode, state.x, state.y, state.z)) {
                    continue;
                }

                /* make sure we're rendering inside the image boundaries */
                if ((state.imgx >= imgsize0 + 24) || (state.imgx <= -24)) {
                    continue;
                }
                if ((state.imgy >= imgsize1 + 24) || (state.imgy <= -24)) {
                    continue;
                }

                /* check for occlusion */
                if (render_mode_occluded(rendermode, state.x, state.y, state.z)) {
                    continue;
                }

                /* everything stored here will be a borrowed ref */

                if (block_has_property(state.block, NODATA)) {
                    /* block shouldn't have data associated with it, set it to 0 */
                    ancilData = 0;
                    state.block_data = 0;
                    state.block_pdata = 0;
                } else {
                    /* block has associated data, use it */
                    ancilData = getArrayByte3D(state.blockdatas, state.x, state.y, state.z);
                    state.block_data = ancilData;
                    /* block that need pseudo ancildata:
                     * water, glass, redstone wire,
                     * ice, portal, stairs */
                    if (block_class_is_subset(state.block, block_class_ancil, block_class_ancil_len)) {
                        ancilData = generate_pseudo_data(&state, ancilData);
                        state.block_pdata = ancilData;
                    } else {
                        state.block_pdata = 0;
                    }
                }

                /* make sure our block info is in-bounds */
                if (state.block >= max_blockid || ancilData >= max_data)
                    continue;

                /* get the texture */
                t = PyList_GET_ITEM(blockmap, max_data * state.block + ancilData);
                /* if we don't get a texture, try it again with 0 data */
                if ((t == NULL || t == Py_None) && ancilData != 0)
                    t = PyList_GET_ITEM(blockmap, max_data * state.block);

                /* if we found a proper texture, render it! */
                if (t != NULL && t != Py_None) {
                    PyObject *src, *mask, *mask_light;
                    int32_t do_rand = (state.block == block_tallgrass /*|| state.block == block_red_flower || state.block == block_double_plant*/);
                    int32_t randx = 0, randy = 0;
                    src = PyTuple_GetItem(t, 0);
                    mask = PyTuple_GetItem(t, 0);
                    mask_light = PyTuple_GetItem(t, 1);

                    if (mask == Py_None)
                        mask = src;

                    if (do_rand) {
                        /* add a random offset to the postion of the tall grass to make it more wild */
                        randx = rand() % 6 + 1 - 3;
                        randy = rand() % 6 + 1 - 3;
                        state.imgx += randx;
                        state.imgy += randy;
                    }

                    render_mode_draw(rendermode, src, mask, mask_light);

                    if (do_rand) {
                        /* undo the random offsets */
                        state.imgx -= randx;
                        state.imgy -= randy;
                    }
                }
            }
        }
    }

    /* free up the rendermode info */
    render_mode_destroy(rendermode);

    Py_DECREF(blockmap);
    unload_all_chunks(&state);

    Py_RETURN_NONE;
}
