//====================================================================
// bsg_set_tile_x_y.h
// 03/02/2020, Lin Cheng (lc873@cornell.edu)
//====================================================================
// This is an emulation of bsg_set_tile_x_y which does nothing

#ifndef _BSG_SET_TILE_X_Y_H
#define _BSG_SET_TILE_X_Y_H

extern thread_local int __bsg_x;               //The X Cord inside a tile group
extern thread_local int __bsg_y;               //The Y Cord inside a tile group
extern thread_local int __bsg_id;              //The ID of a tile in tile group
extern thread_local int __bsg_grp_org_x;       //The X Cord of the tile group origin
extern thread_local int __bsg_grp_org_y;       //The Y Cord of the tile group origin
extern thread_local int __bsg_grid_dim_x;      //The X Dimensions of the grid of tile groups
extern thread_local int __bsg_grid_dim_y;      //The Y Dimensions of the grid of tile groups
extern thread_local int __bsg_tile_group_id_x; //The X Cord of the tile group within the grid
extern thread_local int __bsg_tile_group_id_y; //The Y Cord of the tile group within the grid
extern thread_local int __bsg_tile_group_id;   //The flat ID of the tile group within the grid
extern thread_local void* __bsg_frame;

//----------------------------------------------------------
//bsg_x and bsg_y is going to be deprecated.
//We define the bsg_x/bsg_y only for compatibility purpose
#define bsg_x __bsg_x
#define bsg_y __bsg_y
#define bsg_id __bsg_id

void bsg_set_tile_x_y();

#endif
