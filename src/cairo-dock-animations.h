
#ifndef __CAIRO_DOCK_ANIMATIONS__
#define  __CAIRO_DOCK_ANIMATIONS__

#include <glib.h>

#include "cairo-dock-struct.h"
G_BEGIN_DECLS


gboolean cairo_dock_move_up (CairoDock *pDock);

gboolean cairo_dock_move_down (CairoDock *pDock);

gboolean cairo_dock_pop_up (CairoDock *pDock);

gboolean cairo_dock_pop_down (CairoDock *pDock);


gfloat cairo_dock_calculate_magnitude (gint iMagnitudeIndex);

gboolean cairo_dock_grow_up (CairoDock *pDock);

gboolean cairo_dock_shrink_down (CairoDock *pDock);


/**
*Arme l'animation d'une icone 
*@param icon l'icone dont on veut preparer l'animation.
*@param iAnimationType le type d'animation voulu, ou -1 pour utiliser l'animtion correspondante au type de l'icone.
*@param iNbRounds le nombre de fois ou l'animation sera jouee, ou -1 pour utiliser la valeur correspondante au type de l'icone.
*/
void cairo_dock_arm_animation (Icon *icon, CairoDockAnimationType iAnimationType, int iNbRounds);
/**
*Arme l'animation d'une icone correspondant a un type donne.
*@param icon l'icone a animer.
*@param iType le type d'icone qui sera considere.
*/
void cairo_dock_arm_animation_by_type (Icon *icon, CairoDockIconType iType);
/**
*Lance l'animation de l'icone. Ne fait rien si l'icone ne sera pas animee.
*@param icon l'icone a animer.
*@param pDock le dock contenant l'icone.
*/
void cairo_dock_start_animation (Icon *icon, CairoDock *pDock);

/**
*Definit s'il est utile de lancer l'animation d'un dock (il est inutile de la lancer s'il est manifestement invisible).
*@param pDock le dock a animer.
*/
#define cairo_dock_animation_will_be_visible(pDock) ((pDock)->bInside || (! (pDock)->bAutoHide && (pDock)->iRefCount == 0) || ! (pDock)->bAtBottom)


void cairo_dock_launch_animation (CairoDock *pDock);

void cairo_dock_start_shrinking (CairoDock *pDock);

void cairo_dock_start_growing (CairoDock *pDock);


void cairo_dock_mark_icon_animation_as (Icon *pIcon, CairoDockAnimationState iAnimationState);
void cairo_dock_stop_marking_icon_animation_as (Icon *pIcon, CairoDockAnimationState iAnimationState);

#define cairo_dock_mark_icon_as_inserting_removing(pIcon) cairo_dock_mark_icon_animation_as (pIcon, CAIRO_DOCK_STATE_REMOVE_INSERT)
#define cairo_dock_stop_marking_icon_as_inserting_removing(pIcon) cairo_dock_mark_icon_animation_as (pIcon, CAIRO_DOCK_STATE_REMOVE_INSERT)

#define cairo_dock_mark_icon_as_hovered_by_mouse(pIcon) cairo_dock_mark_icon_animation_as (pIcon, CAIRO_DOCK_STATE_MOUSE_HOVERED)
#define cairo_dock_stop_marking_icon_as_hovered_by_mouse(pIcon) cairo_dock_mark_icon_animation_as (pIcon, CAIRO_DOCK_STATE_MOUSE_HOVERED)

#define cairo_dock_mark_icon_as_clicked(pIcon) cairo_dock_mark_icon_animation_as (pIcon, CAIRO_DOCK_STATE_CLICKED)
#define cairo_dock_stop_marking_icon_as_clicked(pIcon) cairo_dock_mark_icon_animation_as (pIcon, CAIRO_DOCK_STATE_CLICKED)


gboolean cairo_dock_update_inserting_removing_icon_notification (gpointer pUserData, Icon *pIcon, CairoDock *pDock, gboolean *bContinueAnimation);
gboolean cairo_dock_on_insert_remove_icon_notification (gpointer pUserData, Icon *pIcon, CairoDock *pDock);


G_END_DECLS
#endif
