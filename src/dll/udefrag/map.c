/*
 *  UltraDefrag - a powerful defragmentation tool for Windows NT.
 *  Copyright (c) 2007-2018 Dmitri Arkhangelski (dmitriar@gmail.com).
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/**
 * @if INTERNAL
 * @file map.c
 * @brief Cluster map.
 * @addtogroup ClusterMap
 * @{
 */
     
/*
* Revised by Stefan Pendl, 2011
* <stefanpe@users.sourceforge.net>
*/

#include "udefrag-internals.h"

/*
* Contemporary hard drives may contain
* huge number of clusters. To draw each
* cluster on the map we need at least
* 4 bits of memory. Therefore, to draw
* 1 billion of clusters, we'll need at
* least 500 MB of memory.
*
* That's definitely too much, so the program
* never tries to draw individual clusters on
* the map. Instead, it splits the disk into
* cells and draws them on the map afterwards.
*
* Each cell keeps information on how much
* clusters of each kind belongs to it.
*/

/**
 * @internal
 * @brief Allocates cluster map.
 * @param[in] map_size the number of cells.
 * @param[in,out] jp the job parameters.
 * @return Zero for success, a negative value otherwise.
 */
int allocate_map(int map_size,udefrag_job_parameters *jp)
{
    int array_size;
    ULONGLONG used_cells;
    
    /* reset all internal data */
    jp->pi.cluster_map = NULL;
    jp->pi.cluster_map_size = 0;
    memset(&jp->cluster_map,0,sizeof(cmap));
    
    itrace("map size = %u",map_size);
    if(map_size == 0)
        return 0;
    
    /* get volume information */
    if(winx_get_volume_information(jp->volume_letter,&jp->v_info) < 0)
        return (-1);
    if(jp->v_info.total_clusters == 0)
        return (-1);

    /* allocate memory */
    jp->pi.cluster_map = winx_tmalloc(map_size);
    if(jp->pi.cluster_map == NULL){
        etrace("cannot allocate %u bytes of memory",map_size);
        return UDEFRAG_NO_MEM;
    }
    array_size = map_size * SPACE_STATES * sizeof(ULONGLONG);
    jp->cluster_map.array = winx_tmalloc(array_size);
    if(jp->cluster_map.array == NULL){
        etrace("cannot allocate %u bytes of memory",
            array_size);
        winx_free(jp->pi.cluster_map);
        jp->pi.cluster_map = NULL;
        return UDEFRAG_NO_MEM;
    }
    
    /* set internal data */
    jp->pi.cluster_map_size = map_size;
    jp->cluster_map.map_size = map_size;
    jp->cluster_map.n_colors = SPACE_STATES;
    jp->cluster_map.field_size = jp->v_info.total_clusters;
    
    jp->cluster_map.clusters_per_cell = jp->cluster_map.field_size / jp->cluster_map.map_size;
    if(jp->cluster_map.clusters_per_cell){
        jp->cluster_map.opposite_order = 0;
        if(jp->cluster_map.clusters_per_cell * jp->cluster_map.map_size != jp->cluster_map.field_size)
            jp->cluster_map.clusters_per_cell ++; /* ensure that the map will cover entire disk */
        used_cells = jp->cluster_map.field_size / jp->cluster_map.clusters_per_cell;
        if(jp->cluster_map.clusters_per_cell * used_cells != jp->cluster_map.field_size) used_cells ++;
        jp->cluster_map.unused_cells = jp->cluster_map.map_size - used_cells;
        jp->cluster_map.clusters_per_last_cell = jp->cluster_map.field_size - \
           jp->cluster_map.clusters_per_cell * (used_cells - 1);
        itrace("normal order %I64u : %I64u : %I64u: %I64u", \
            jp->cluster_map.field_size,jp->cluster_map.clusters_per_cell,
            jp->cluster_map.clusters_per_last_cell,jp->cluster_map.unused_cells);
    } else {
        jp->cluster_map.opposite_order = 1;
        jp->cluster_map.cells_per_cluster = jp->cluster_map.map_size / jp->cluster_map.field_size;
        jp->cluster_map.unused_cells = jp->cluster_map.map_size - \
            jp->cluster_map.cells_per_cluster * jp->cluster_map.field_size;
        itrace("opposite order %I64u : %I64u : %I64u", \
            jp->cluster_map.field_size,jp->cluster_map.cells_per_cluster,jp->cluster_map.unused_cells);
    }

    /* reset the map */
    reset_cluster_map(jp);
    return 0;
}

/**
 * @internal
 * @brief Fills the map by the default color.
 */
void reset_cluster_map(udefrag_job_parameters *jp)
{
    ULONGLONG i, j;
    
    if(jp->cluster_map.array == NULL)
        return;

    memset(jp->cluster_map.array,0,jp->cluster_map.map_size * jp->cluster_map.n_colors * sizeof(ULONGLONG));
    if(jp->cluster_map.opposite_order == 0){
        for(i = 0; i < jp->cluster_map.map_size - jp->cluster_map.unused_cells - 1; i++)
            jp->cluster_map.array[i][DEFAULT_COLOR] = jp->cluster_map.clusters_per_cell;
        jp->cluster_map.array[i][DEFAULT_COLOR] = jp->cluster_map.clusters_per_last_cell;
        for(j = 0, i++; j < jp->cluster_map.unused_cells; j++)
            jp->cluster_map.array[i+j][UNUSED_MAP_SPACE] = jp->cluster_map.clusters_per_cell;
    } else {
        for(i = 0; i < jp->cluster_map.map_size - jp->cluster_map.unused_cells; i++)
            jp->cluster_map.array[i][DEFAULT_COLOR] = 1;
        for(j = 0; j < jp->cluster_map.unused_cells; j++)
            jp->cluster_map.array[i+j][UNUSED_MAP_SPACE] = 1;
    }
}

/**
 * @internal
 * @brief Colorizes the specified range of clusters.
 * @note If the new color is equal to MFT_ZONE_SPACE,
 * the old color is ignored.
 */
void colorize_map_region(udefrag_job_parameters *jp,
        ULONGLONG lcn, ULONGLONG length, int new_color, int old_color)
{
    ULONGLONG i, j, n, cell, offset, ncells;
    ULONGLONG *c;
    
    /* validate parameters */
    if(jp->cluster_map.array == NULL)
        return;
    if(!check_region(jp,lcn,length))
        return;
    if(length == 0)
        return;
    
    /* validate colors */
    if(new_color < 0 || new_color >= jp->cluster_map.n_colors)
        return;
    if(new_color != MFT_ZONE_SPACE){
        if(old_color < 0 || old_color >= jp->cluster_map.n_colors)
            return;
    }
    
    if(new_color == old_color)
        return;
    
    if(jp->cluster_map.opposite_order == 0){
        cell = lcn / jp->cluster_map.clusters_per_cell;
        offset = lcn % jp->cluster_map.clusters_per_cell;
        if(cell >= jp->cluster_map.map_size) return;
        while(cell < (jp->cluster_map.map_size - 1) && length){
            n = min(length,jp->cluster_map.clusters_per_cell - offset);
            jp->cluster_map.array[cell][new_color] += n;
            if(new_color != MFT_ZONE_SPACE){
                c = &jp->cluster_map.array[cell][old_color];
                if(*c >= n) *c -= n; else *c = 0;
            }
            length -= n;
            cell ++;
            offset = 0;
        }
        if(length){
            n = min(length,jp->cluster_map.clusters_per_last_cell - offset);
            jp->cluster_map.array[cell][new_color] += n;
            if(new_color != MFT_ZONE_SPACE){
                c = &jp->cluster_map.array[cell][old_color];
                if(*c >= n) *c -= n; else *c = 0;
            }
        }
    } else {
        /* clusters < cells */
        cell = lcn * jp->cluster_map.cells_per_cluster;
        ncells = length * jp->cluster_map.cells_per_cluster;
        for(i = 0; i < ncells; i++){
            if(new_color != MFT_ZONE_SPACE){
                for(j = 0; j < jp->cluster_map.n_colors; j++)
                    jp->cluster_map.array[cell + i][j] = 0;
            }
            jp->cluster_map.array[cell + i][new_color] = 1;
        }
    }
}

/**
 * @internal
 * @brief Defines whether a file is $Mft or not.
 * @return A nonzero value if the file is $Mft.
 */
int is_mft(winx_file_info *f,udefrag_job_parameters *jp)
{
    int length;
    wchar_t mft_name[] = L"$Mft";

    if(f == NULL)
        return 0;
    
    if(jp->fs_type != FS_NTFS)
        return 0;
    
    if(is_not_mft_file(f)) return 0;
    if(is_mft_file(f)) return 1;
    
    length = (int)wcslen(f->path);
    if(length == 11){
        if(winx_wcsistr(f->name,mft_name)){
            f->user_defined_flags |= UD_FILE_MFT_FILE;
            return 1;
        }
    }
    
    f->user_defined_flags |= UD_FILE_NOT_MFT_FILE;
    return 0;
}

/**
 * @internal
 * @brief Defines file's color.
 */
int get_file_color(udefrag_job_parameters *jp, winx_file_info *f)
{
    /* show the $MFT file in dark magenta color */
    if(is_mft(f,jp))
        return MFT_SPACE;
    
    if(is_locked(f))
        return is_over_limit(f) ? SYSTEM_OVER_LIMIT_SPACE : SYSTEM_SPACE;

    /* show excluded files as not fragmented */
    if(is_fragmented(f) && !is_excluded(f))
        return is_over_limit(f) ? FRAGM_OVER_LIMIT_SPACE : FRAGM_SPACE;

    if(is_directory(f)){
        if(is_over_limit(f))
            return DIR_OVER_LIMIT_SPACE;
        else
            return DIR_SPACE;
    } else if(is_compressed(f)){
        if(is_over_limit(f))
            return COMPRESSED_OVER_LIMIT_SPACE;
        else
            return COMPRESSED_SPACE;
    } else {
        if(is_over_limit(f))
            return UNFRAGM_OVER_LIMIT_SPACE;
        else
            return UNFRAGM_SPACE;
    }
    return UNFRAGM_SPACE; /* this point will never be reached */
}

/**
 * @internal
 * @brief Colorizes space belonging to a file.
 */
void colorize_file(udefrag_job_parameters *jp, winx_file_info *f, int old_color)
{
    winx_blockmap *block;
    int new_color;
    
    if(f == NULL)
        return;
    
    new_color = get_file_color(jp,f);
    for(block = f->disp.blockmap; block; block = block->next){
        colorize_map_region(jp,block->lcn,block->length,new_color,old_color);
        if(block->next == f->disp.blockmap) break;
    }
}

/**
 * @internal
 * @brief Releases resources allocated
 * by the allocate_map() routine.
 */
void free_map(udefrag_job_parameters *jp)
{
    winx_free(jp->pi.cluster_map);
    winx_free(jp->cluster_map.array);
    jp->pi.cluster_map = NULL;
    jp->pi.cluster_map_size = 0;
    memset(&jp->cluster_map,0,sizeof(cmap));
}

/**
 * @}
 * @endif
 */
