/**
* This file is a part of the Cairo-Dock project
*
* Copyright : (C) see the 'copyright' file.
* E-mail    : see the 'copyright' file.
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 3
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <math.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h> 
#include <cairo.h>
#include <gtk/gtk.h>

#include <GL/gl.h>
#include <GL/glu.h>

#include "cairo-dock-draw.h"
#include "cairo-dock-animations.h"
#include "cairo-dock-image-buffer.h"
#include "cairo-dock-module-manager.h"  // CAIRO_DOCK_MODULE_CAN_DESKLET
#include "cairo-dock-module-instance-manager.h"  // gldi_module_instance_reload
#include "cairo-dock-icon-factory.h"
#include "cairo-dock-icon-facility.h"
#include "cairo-dock-applications-manager.h"
#include "cairo-dock-application-facility.h"
#include "cairo-dock-desktop-file-factory.h"
#include "cairo-dock-launcher-manager.h"
#include "cairo-dock-config.h"
#include "cairo-dock-container.h"
#include "cairo-dock-dock-facility.h"
#include "cairo-dock-dialog-manager.h"
#include "cairo-dock-log.h"
#include "cairo-dock-dock-manager.h"
#include "cairo-dock-dock-visibility.h"
#include "cairo-dock-draw-opengl.h"
#include "cairo-dock-flying-container.h"
#include "cairo-dock-animations.h"
#include "cairo-dock-backends-manager.h"
#include "cairo-dock-class-manager.h"
#include "cairo-dock-desktop-manager.h"
#include "cairo-dock-windows-manager.h"  // gldi_windows_get_active
#include "cairo-dock-data-renderer.h"  // cairo_dock_refresh_data_renderer
#include "cairo-dock-callbacks.h"

// dependencies
extern CairoDockHidingEffect *g_pHidingBackend;
extern CairoDockHidingEffect *g_pKeepingBelowBackend;
extern gboolean g_bUseOpenGL;

// private
static Icon *s_pIconClicked = NULL;  // pour savoir quand on deplace une icone a la souris. Dangereux si l'icone se fait effacer en cours ...
static int s_iClickX, s_iClickY;  // coordonnees du clic dans le dock, pour pouvoir initialiser le deplacement apres un seuil.
static int s_iSidShowSubDockDemand = 0;
static int s_iSidActionOnDragHover = 0;
static CairoDock *s_pDockShowingSubDock = NULL;  // on n'accede pas a son contenu, seulement l'adresse.
static CairoDock *s_pSubDockShowing = NULL;  // on n'accede pas a son contenu, seulement l'adresse.
static CairoFlyingContainer *s_pFlyingContainer = NULL;
static int s_iFirstClickX=0, s_iFirstClickY=0;  // for double-click.
static gboolean s_bFrozenDock = FALSE;
static gboolean s_bIconDragged = FALSE;
static gboolean _check_mouse_outside (CairoDock *pDock);
static void cairo_dock_stop_icon_glide (CairoDock *pDock);
#define CD_CLICK_ZONE 5

static gboolean _mouse_is_really_outside (CairoDock *pDock)
{
	/**return (pDock->container.iMouseX <= 0
		|| pDock->container.iMouseX >= pDock->container.iWidth
		|| (pDock->container.bDirectionUp ?
			(pDock->container.iMouseY > pDock->container.iHeight
			|| pDock->container.iMouseY <= (pDock->fMagnitudeMax != 0 ? 0 : pDock->container.iHeight - pDock->iMinDockHeight))
			: (pDock->container.iMouseY < 0
			||
		pDock->container.iMouseY >= (pDock->fMagnitudeMax != 0 ? pDock->container.iHeight : pDock->iMinDockHeight))));*/
	int x1, x2, y1, y2;
	if (pDock->iInputState == CAIRO_DOCK_INPUT_ACTIVE)
	{
		///x1 = 0;
		///x2 = pDock->container.iWidth;
		x1 = (pDock->container.iWidth - pDock->iActiveWidth) * pDock->fAlign;
		x2 = x1 + pDock->iActiveWidth;
		if (pDock->container.bDirectionUp)
		{
			///y1 = (pDock->fMagnitudeMax != 0 ? 0 : pDock->container.iHeight - pDock->iMinDockHeight);
			y1 = pDock->container.iHeight - pDock->iActiveHeight + 1;
			y2 = pDock->container.iHeight;
		}
		else
		{
			y1 = 0;
			///y2 = (pDock->fMagnitudeMax != 0 ? pDock->container.iHeight : pDock->iMinDockHeight);
			y2 = pDock->iActiveHeight - 1;
		}
	}
	else if (pDock->iInputState == CAIRO_DOCK_INPUT_AT_REST)
	{
		x1 = (pDock->container.iWidth - pDock->iMinDockWidth) * pDock->fAlign;
		x2 = x1 + pDock->iMinDockWidth;
		if (pDock->container.bDirectionUp)
		{
			y1 = pDock->container.iHeight - pDock->iMinDockHeight + 1;
			y2 = pDock->container.iHeight;
		}
		else
		{
			y1 = 0;
			y2 = pDock->iMinDockHeight - 1;
		}		
	}
	else  // hidden
		return TRUE;
	if (pDock->container.iMouseX <= x1
	|| pDock->container.iMouseX >= x2)
		return TRUE;
	if (pDock->container.iMouseY < y1
	|| pDock->container.iMouseY > y2)  // Note: Compiz has a bug: when using the "cube rotation" plug-in, it will reserve 2 pixels for itself on the left and right edges of the screen. So the mouse is not inside the dock when it's at x=0 or x=Ws-1 (no 'enter' event is sent; it's as if the x=0 or x=Ws-1 vertical line of pixels is out of the screen).
		return TRUE;	
	
	return FALSE;
}

void cairo_dock_freeze_docks (gboolean bFreeze)
{
	s_bFrozenDock = bFreeze;  /// instead, try to connect to the motion-event and intercept it ...
}

static gboolean _on_expose (G_GNUC_UNUSED GtkWidget *pWidget,
#if (GTK_MAJOR_VERSION < 3)
	GdkEventExpose *pExpose,
#else
	cairo_t *ctx,
#endif
	CairoDock *pDock)
{
	GdkRectangle area;
	#if (GTK_MAJOR_VERSION < 3)
	memcpy (&area, &pExpose->area, sizeof (GdkRectangle));
	#else
	double x1, x2, y1, y2;
	cairo_clip_extents (ctx, &x1, &y1, &x2, &y2);
	area.x = x1;
	area.y = y1;
	area.width = x2 - x1;
	area.height = y2 - y1;  /// or the opposite ?...
	#endif
	//g_print ("%s ((%d;%d) %dx%d)\n", __func__, area.x, area.y, area.width, area.height);
	
	//\________________ OpenGL rendering
	if (g_bUseOpenGL && pDock->pRenderer->render_opengl != NULL)
	{
		if (! gldi_glx_begin_draw_container_full (CAIRO_CONTAINER (pDock), FALSE))  // FALSE to keep the color buffer (motion-blur).
			return FALSE;
		
		if (area.x + area.y != 0)
		{
			glEnable (GL_SCISSOR_TEST);  // ou comment diviser par 4 l'occupation CPU !
			glScissor ((int) area.x,
				(int) (pDock->container.bIsHorizontal ? pDock->container.iHeight : pDock->container.iWidth) -
					area.y - area.height,  // lower left corner of the scissor box.
				(int) area.width,
				(int) area.height);
		}
		
		if (cairo_dock_is_loading ())
		{
			// don't draw anything, just let it transparent
		}
		else if (cairo_dock_is_hidden (pDock) && (g_pHidingBackend == NULL || !g_pHidingBackend->bCanDisplayHiddenDock))
		{
			cairo_dock_render_hidden_dock_opengl (pDock);
		}
		else
		{
			gldi_object_notify (pDock, NOTIFICATION_RENDER, pDock, NULL);
		}
		glDisable (GL_SCISSOR_TEST);
		
		gldi_glx_end_draw_container (CAIRO_CONTAINER (pDock));
		
		return FALSE ;
	}
	else if (! g_bUseOpenGL && pDock->pRenderer->render != NULL)
	{
		cairo_t *pCairoContext;
		if (area.x + area.y != 0)
			pCairoContext = cairo_dock_create_drawing_context_on_area (CAIRO_CONTAINER (pDock), &area, NULL);
		else
			pCairoContext = cairo_dock_create_drawing_context_on_container (CAIRO_CONTAINER (pDock));
		
		if (cairo_dock_is_loading ())
		{
			// don't draw anything, just let it transparent
		}
		else if (cairo_dock_is_hidden (pDock) && (g_pHidingBackend == NULL || !g_pHidingBackend->bCanDisplayHiddenDock))
		{
			cairo_dock_render_hidden_dock (pCairoContext, pDock);
		}
		else
		{
			gldi_object_notify (pDock, NOTIFICATION_RENDER, pDock, pCairoContext);
		}
	
		cairo_destroy (pCairoContext);
		return FALSE ;
	}
	
	/// TODO: check that it works without the code below...
	
	//\________________ Cairo optimized rendering
	if (area.x + area.y != 0)  // x et/ou y sont > 0.
	{
		if (! cairo_dock_is_hidden (pDock) || (g_pHidingBackend != NULL && g_pHidingBackend->bCanDisplayHiddenDock))  // if the dock is invisible, we don't use the optimized rendering (for always-visible icons for instance)
		{
			cairo_t *pCairoContext = cairo_dock_create_drawing_context_on_area (CAIRO_CONTAINER (pDock), &area, NULL);
			
			if (pDock->fHideOffset != 0 && g_pHidingBackend != NULL && g_pHidingBackend->pre_render)
				g_pHidingBackend->pre_render (pDock, pDock->fHideOffset, pCairoContext);
			
			if (pDock->iFadeCounter != 0 && g_pKeepingBelowBackend != NULL && g_pKeepingBelowBackend->pre_render)
				g_pKeepingBelowBackend->pre_render (pDock, (double) pDock->iFadeCounter / myBackendsParam.iHideNbSteps, pCairoContext);
			
			if (pDock->pRenderer->render_optimized != NULL)
				pDock->pRenderer->render_optimized (pCairoContext, pDock, &area);
			else
				pDock->pRenderer->render (pCairoContext, pDock);
			
			if (pDock->fHideOffset != 0 && g_pHidingBackend != NULL && g_pHidingBackend->post_render)
				g_pHidingBackend->post_render (pDock, pDock->fHideOffset, pCairoContext);
		
			if (pDock->iFadeCounter != 0 && g_pKeepingBelowBackend != NULL && g_pKeepingBelowBackend->post_render)
				g_pKeepingBelowBackend->post_render (pDock, (double) pDock->iFadeCounter / myBackendsParam.iHideNbSteps, pCairoContext);
			
			gldi_object_notify (pDock, NOTIFICATION_RENDER, pDock, pCairoContext);
			
			cairo_destroy (pCairoContext);
			return FALSE;
		}
		
	}
	
	//\________________ Cairo rendering
	cairo_t *pCairoContext = cairo_dock_create_drawing_context_on_container (CAIRO_CONTAINER (pDock));
	
	if (cairo_dock_is_loading ())  // transparent pendant le chargement.
	{
		
	}
	else if (cairo_dock_is_hidden (pDock) && (g_pHidingBackend == NULL || !g_pHidingBackend->bCanDisplayHiddenDock))
	{
		cairo_dock_render_hidden_dock (pCairoContext, pDock);
	}
	else
	{
		if (pDock->fHideOffset != 0 && g_pHidingBackend != NULL && g_pHidingBackend->pre_render)
			g_pHidingBackend->pre_render (pDock, pDock->fHideOffset, pCairoContext);
		
		if (pDock->iFadeCounter != 0 && g_pKeepingBelowBackend != NULL && g_pKeepingBelowBackend->pre_render)
			g_pKeepingBelowBackend->pre_render (pDock, (double) pDock->iFadeCounter / myBackendsParam.iHideNbSteps, pCairoContext);
		
		pDock->pRenderer->render (pCairoContext, pDock);
		
		if (pDock->fHideOffset != 0 && g_pHidingBackend != NULL && g_pHidingBackend->post_render)
			g_pHidingBackend->post_render (pDock, pDock->fHideOffset, pCairoContext);
		
		if (pDock->iFadeCounter != 0 && g_pKeepingBelowBackend != NULL && g_pKeepingBelowBackend->post_render)
			g_pKeepingBelowBackend->post_render (pDock, (double) pDock->iFadeCounter / myBackendsParam.iHideNbSteps, pCairoContext);
		
		gldi_object_notify (pDock, NOTIFICATION_RENDER, pDock, pCairoContext);
	}
	
	cairo_destroy (pCairoContext);
	return FALSE;
}


static gboolean _emit_leave_signal_delayed (CairoDock *pDock)
{
	cairo_dock_emit_leave_signal (CAIRO_CONTAINER (pDock));
	pDock->iSidLeaveDemand = 0;
	return FALSE;
}
static gboolean _cairo_dock_show_sub_dock_delayed (CairoDock *pDock)
{
	s_iSidShowSubDockDemand = 0;
	s_pDockShowingSubDock = NULL;
	s_pSubDockShowing = NULL;
	Icon *icon = cairo_dock_get_pointed_icon (pDock->icons);
	//g_print ("%s (%x, %x)", __func__, icon, icon ? icon->pSubDock:0);
	if (icon != NULL && icon->pSubDock != NULL)
		cairo_dock_show_subdock (icon, pDock);

	return FALSE;
}
static void _search_icon (Icon *icon, G_GNUC_UNUSED GldiContainer *pContainer, gpointer *data)
{
	if (icon == data[0])
		data[1] = icon;
}
static gboolean _cairo_dock_action_on_drag_hover (Icon *pIcon)
{
	gpointer data[2] = {pIcon, NULL};
	cairo_dock_foreach_icons_in_docks ((CairoDockForeachIconFunc)_search_icon, data);  // on verifie que l'icone ne s'est pas faite effacee entre-temps.
	pIcon = data[1];
	if (pIcon && pIcon->iface.action_on_drag_hover)
		pIcon->iface.action_on_drag_hover (pIcon);
	s_iSidActionOnDragHover = 0;
	return FALSE;
}
void cairo_dock_on_change_icon (Icon *pLastPointedIcon, Icon *pPointedIcon, CairoDock *pDock)
{
	//g_print ("%s (%s -> %s)\n", __func__, pLastPointedIcon?pLastPointedIcon->cName:"none", pPointedIcon?pPointedIcon->cName:"none");
	//cd_debug ("on change d'icone dans %x (-> %s)", pDock, (pPointedIcon != NULL ? pPointedIcon->cName : "rien"));
	if (s_iSidShowSubDockDemand != 0 && pDock == s_pDockShowingSubDock)
	{
		//cd_debug ("on annule la demande de montrage de sous-dock");
		g_source_remove (s_iSidShowSubDockDemand);
		s_iSidShowSubDockDemand = 0;
		s_pDockShowingSubDock = NULL;
		s_pSubDockShowing = NULL;
	}
	
	// take action when dragging something onto an icon
	if (s_iSidActionOnDragHover != 0)
	{
		//cd_debug ("on annule la demande de montrage d'appli");
		g_source_remove (s_iSidActionOnDragHover);
		s_iSidActionOnDragHover = 0;
	}
	
	if (pDock->bIsDragging && pPointedIcon && pPointedIcon->iface.action_on_drag_hover)
	{
		s_iSidActionOnDragHover = g_timeout_add (600, (GSourceFunc) _cairo_dock_action_on_drag_hover, pPointedIcon);
	}
	
	// replace dialogs
	gldi_dialogs_refresh_all ();
	
	// hide the sub-dock of the previous pointed icon
	if (pLastPointedIcon != NULL && pLastPointedIcon->pSubDock != NULL)  // on a quitte une icone ayant un sous-dock.
	{
		CairoDock *pSubDock = pLastPointedIcon->pSubDock;
		if (gldi_container_is_visible (CAIRO_CONTAINER (pSubDock)))  // le sous-dock est visible, on retarde son cachage.
		{
			//g_print ("on cache %s en changeant d'icone\n", pLastPointedIcon->cName);
			if (pSubDock->iSidLeaveDemand == 0)
			{
				//g_print ("  on retarde le cachage du dock de %dms\n", MAX (myDocksParam.iLeaveSubDockDelay, 330));
				pSubDock->iSidLeaveDemand = g_timeout_add (MAX (myDocksParam.iLeaveSubDockDelay, 300), (GSourceFunc) _emit_leave_signal_delayed, (gpointer) pSubDock);  // on force le retard meme si iLeaveSubDockDelay est a 0, car lorsqu'on entre dans un sous-dock, il arrive frequemment qu'on glisse hors de l'icone qui pointe dessus, et c'est tres desagreable d'avoir le dock qui se ferme avant d'avoir pu entre dedans.
			}
		}
	}
	
	// show the sub-dock of the current pointed icon
	if (pPointedIcon != NULL && pPointedIcon->pSubDock != NULL && (! myDocksParam.bShowSubDockOnClick || CAIRO_DOCK_IS_APPLI (pPointedIcon) || pDock->bIsDragging))  // on entre sur une icone ayant un sous-dock.
	{
		// if we were leaving the sub-dock, cancel that.
		if (pPointedIcon->pSubDock->iSidLeaveDemand != 0)
		{
			g_source_remove (pPointedIcon->pSubDock->iSidLeaveDemand);
			pPointedIcon->pSubDock->iSidLeaveDemand = 0;
		}
		// and show the sub-dock, possibly with a delay.
		if (myDocksParam.iShowSubDockDelay > 0)
		{
			if (s_iSidShowSubDockDemand != 0)
				g_source_remove (s_iSidShowSubDockDemand);
			s_iSidShowSubDockDemand = g_timeout_add (myDocksParam.iShowSubDockDelay, (GSourceFunc) _cairo_dock_show_sub_dock_delayed, pDock);  // we can't be showing more than 1 sub-dock, so this timeout can be global to all docks.
			s_pDockShowingSubDock = pDock;
			s_pSubDockShowing = pPointedIcon->pSubDock;
		}
		else
			cairo_dock_show_subdock (pPointedIcon, pDock);
	}
	
	// notify everybody
	if (pPointedIcon != NULL && ! CAIRO_DOCK_ICON_TYPE_IS_SEPARATOR (pPointedIcon))
	{
		gboolean bStartAnimation = FALSE;
		gldi_object_notify (pDock, NOTIFICATION_ENTER_ICON, pPointedIcon, pDock, &bStartAnimation);
		
		if (bStartAnimation)
		{
			///pPointedIcon->iAnimationState = CAIRO_DOCK_STATE_MOUSE_HOVERED;
			cairo_dock_mark_icon_as_hovered_by_mouse (pPointedIcon);  // mark the animation as 'hover' if it's not already in another state (clicked, etc).
			cairo_dock_launch_animation (CAIRO_CONTAINER (pDock));
		}
	}
}


gboolean cairo_dock_on_leave_dock_notification (G_GNUC_UNUSED gpointer data, CairoDock *pDock, G_GNUC_UNUSED gboolean *bStartAnimation)
{
	//g_print ("%s (%d, %d)\n", __func__, pDock->iRefCount, pDock->bHasModalWindow);
	
	//\_______________ If a modal window is raised, we discard the 'leave-event' to stay in the up position.
	if (pDock->bHasModalWindow)
		return GLDI_NOTIFICATION_INTERCEPT;
	
	//\_______________ On gere le drag d'une icone hors du dock.
	if (s_pIconClicked != NULL
	&& (CAIRO_DOCK_ICON_TYPE_IS_LAUNCHER (s_pIconClicked)
		|| CAIRO_DOCK_ICON_TYPE_IS_CONTAINER (s_pIconClicked)
		|| (CAIRO_DOCK_ICON_TYPE_IS_SEPARATOR (s_pIconClicked) && s_pIconClicked->cDesktopFileName && pDock->iMaxDockHeight > 30)  // if the dock is narrow (like a panel), prevent from dragging separators outside of the dock. TODO: maybe we need a parameter in the view...
		|| CAIRO_DOCK_IS_DETACHABLE_APPLET (s_pIconClicked))
	&& s_pFlyingContainer == NULL
	&& ! myDocksParam.bLockIcons
	&& ! myDocksParam.bLockAll
	&& ! pDock->bPreventDraggingIcons)
	{
		cd_debug ("on a sorti %s du dock (%d;%d) / %dx%d", s_pIconClicked->cName, pDock->container.iMouseX, pDock->container.iMouseY, pDock->container.iWidth, pDock->container.iHeight);
		
		//if (! cairo_dock_hide_child_docks (pDock))  // on quitte si on entre dans un sous-dock, pour rester en position "haute".
		//	return ;
		
		CairoDock *pOriginDock = gldi_dock_get (s_pIconClicked->cParentDockName);
		g_return_val_if_fail (pOriginDock != NULL, TRUE);
		if (pOriginDock == pDock && _mouse_is_really_outside (pDock))  // ce test est la pour parer aux WM deficients mentaux comme KWin qui nous font sortir/rentrer lors d'un clic.
		{
			cd_debug (" on detache l'icone");
			pOriginDock->bIconIsFlyingAway = TRUE;
			/**gchar *cParentDockName = s_pIconClicked->cParentDockName;
			s_pIconClicked->cParentDockName = NULL;*/
			cairo_dock_detach_icon_from_dock (s_pIconClicked, pOriginDock);
			/**s_pIconClicked->cParentDockName = cParentDockName;  // we keep the parent dock name, to be able to re-insert it. we'll have to remove it when the icon is dropped.
			cairo_dock_update_dock_size (pOriginDock);*/
			cairo_dock_stop_icon_glide (pOriginDock);
			
			s_pFlyingContainer = gldi_flying_container_new (s_pIconClicked, pOriginDock);
			//g_print ("- s_pIconClicked <- NULL\n");
			s_pIconClicked = NULL;
			if (pDock->iRefCount > 0 || pDock->bAutoHide)  // pour garder le dock visible.
			{
				return GLDI_NOTIFICATION_INTERCEPT;
			}
		}
	}
	/**else if (s_pFlyingContainer != NULL && s_pFlyingContainer->pIcon != NULL && pDock->iRefCount > 0)  // on evite les bouclages.
	{
		CairoDock *pOriginDock = gldi_dock_get (s_pFlyingContainer->pIcon->cParentDockName);
		if (pOriginDock == pDock)
			return GLDI_NOTIFICATION_INTERCEPT;
	}*/
	
	//\_______________ On lance l'animation du dock.
	if (pDock->iRefCount == 0)
	{
		//g_print ("%s (auto-hide:%d)\n", __func__, pDock->bAutoHide);
		if (pDock->bAutoHide)
		{
			///pDock->fFoldingFactor = (myBackendsParam.bAnimateOnAutoHide ? 0.001 : 0.);
			cairo_dock_start_hiding (pDock);
		}
	}
	else if (pDock->icons != NULL)
	{
		pDock->fFoldingFactor = (myDocksParam.bAnimateSubDock ? 0.001 : 0.);
		Icon *pIcon = cairo_dock_search_icon_pointing_on_dock (pDock, NULL);
		//g_print ("'%s' se replie\n", pIcon?pIcon->cName:"none");
		gldi_object_notify (pIcon, NOTIFICATION_UNFOLD_SUBDOCK, pIcon);
	}
	//g_print ("start shrinking\n");
	cairo_dock_start_shrinking (pDock);  // on commence a faire diminuer la taille des icones.
	return GLDI_NOTIFICATION_LET_PASS;
}

static void cairo_dock_stop_icon_glide (CairoDock *pDock)
{
	Icon *icon;
	GList *ic;
	for (ic = pDock->icons; ic != NULL; ic = ic->next)
	{
		icon = ic->data;
		icon->fGlideOffset = 0;
		icon->iGlideDirection = 0;
	}
}
static void _cairo_dock_make_icon_glide (Icon *pPointedIcon, Icon *pMovingicon, CairoDock *pDock)
{
	Icon *icon;
	GList *ic;
	for (ic = pDock->icons; ic != NULL; ic = ic->next)
	{
		icon = ic->data;
		if (icon == pMovingicon)
			continue;
		//if (pDock->container.iMouseX > s_pMovingicon->fDrawXAtRest + s_pMovingicon->fWidth * s_pMovingicon->fScale /2)  // on a deplace l'icone a droite.  // fDrawXAtRest
		if (pMovingicon->fXAtRest < pPointedIcon->fXAtRest)  // on a deplace l'icone a droite.
		{
			//g_print ("%s : %.2f / %.2f ; %.2f / %d (%.2f)\n", icon->cName, icon->fXAtRest, pMovingicon->fXAtRest, icon->fDrawX, pDock->container.iMouseX, icon->fGlideOffset);
			if (icon->fXAtRest > pMovingicon->fXAtRest && icon->fDrawX < pDock->container.iMouseX + 5 && icon->fGlideOffset == 0)  // icone entre l'icone deplacee et le curseur.
			{
				//g_print ("  %s glisse vers la gauche\n", icon->cName);
				icon->iGlideDirection = -1;
			}
			else if (icon->fXAtRest > pMovingicon->fXAtRest && icon->fDrawX > pDock->container.iMouseX && icon->fGlideOffset != 0)
			{
				//g_print ("  %s glisse vers la droite\n", icon->cName);
				icon->iGlideDirection = 1;
			}
			else if (icon->fXAtRest < pMovingicon->fXAtRest && icon->fGlideOffset > 0)
			{
				//g_print ("  %s glisse en sens inverse vers la gauche\n", icon->cName);
				icon->iGlideDirection = -1;
			}
		}
		else
		{
			//g_print ("deplacement de %s vers la gauche (%.2f / %d)\n", icon->cName, icon->fDrawX + icon->fWidth * fMaxScale + myIconsParam.iIconGap, pDock->container.iMouseX);
			if (icon->fXAtRest < pMovingicon->fXAtRest && icon->fDrawX + icon->image.iWidth + myIconsParam.iIconGap >= pDock->container.iMouseX && icon->fGlideOffset == 0)  // icone entre l'icone deplacee et le curseur.
			{
				//g_print ("  %s glisse vers la droite\n", icon->cName);
				icon->iGlideDirection = 1;
			}
			else if (icon->fXAtRest < pMovingicon->fXAtRest && icon->fDrawX + icon->image.iWidth + myIconsParam.iIconGap <= pDock->container.iMouseX && icon->fGlideOffset != 0)
			{
				//g_print ("  %s glisse vers la gauche\n", icon->cName);
				icon->iGlideDirection = -1;
			}
			else if (icon->fXAtRest > pMovingicon->fXAtRest && icon->fGlideOffset < 0)
			{
				//g_print ("  %s glisse en sens inverse vers la droite\n", icon->cName);
				icon->iGlideDirection = 1;
			}
		}
	}
}
static gboolean _on_motion_notify (GtkWidget* pWidget,
	GdkEventMotion* pMotion,
	CairoDock *pDock)
{
	static double fLastTime = 0;
	if (s_bFrozenDock && pMotion != NULL && pMotion->time != 0)
		return FALSE;
	Icon *pPointedIcon=NULL, *pLastPointedIcon = cairo_dock_get_pointed_icon (pDock->icons);
	//g_print ("%s (%.2f;%.2f, %d)\n", __func__, pMotion->x, pMotion->y, pDock->iInputState);
	
	if (pMotion != NULL)
	{
		//g_print ("%s (%d,%d) (%d, %.2fms, bAtBottom:%d; bIsShrinkingDown:%d)\n", __func__, (int) pMotion->x, (int) pMotion->y, pMotion->is_hint, pMotion->time - fLastTime, pDock->bAtBottom, pDock->bIsShrinkingDown);
		//\_______________ On deplace le dock si ALT est enfoncee.
		if ((pMotion->state & GDK_MOD1_MASK) && (pMotion->state & GDK_BUTTON1_MASK))
		{
			if (pDock->container.bIsHorizontal)
			{
				pDock->container.iWindowPositionX = pMotion->x_root - pDock->container.iMouseX;
				pDock->container.iWindowPositionY = pMotion->y_root - pDock->container.iMouseY;
				gtk_window_move (GTK_WINDOW (pWidget),
					pDock->container.iWindowPositionX,
					pDock->container.iWindowPositionY);
			}
			else
			{
				pDock->container.iWindowPositionX = pMotion->y_root - pDock->container.iMouseX;
				pDock->container.iWindowPositionY = pMotion->x_root - pDock->container.iMouseY;
				gtk_window_move (GTK_WINDOW (pWidget),
					pDock->container.iWindowPositionY,
					pDock->container.iWindowPositionX);
			}
			gdk_device_get_state (pMotion->device, pMotion->window, NULL, NULL);
			return FALSE;
		}
		
		//\_______________ On recupere la position de la souris.
		if (pDock->container.bIsHorizontal)
		{
			pDock->container.iMouseX = (int) pMotion->x;
			pDock->container.iMouseY = (int) pMotion->y;
		}
		else
		{
			pDock->container.iMouseX = (int) pMotion->y;
			pDock->container.iMouseY = (int) pMotion->x;
		}
		
		//\_______________ On tire l'icone volante.
		if (s_pFlyingContainer != NULL && ! pDock->container.bInside)
		{
			gldi_flying_container_drag (s_pFlyingContainer, pDock);
		}
		
		//\_______________ On elague le flux des MotionNotify, sinon X en envoie autant que le permet le CPU !
		if (pMotion->time != 0 && pMotion->time - fLastTime < myBackendsParam.fRefreshInterval && s_pIconClicked == NULL)
		{
			gdk_device_get_state (pMotion->device, pMotion->window, NULL, NULL);
			return FALSE;
		}
		
		//\_______________ On recalcule toutes les icones et on redessine.
		pPointedIcon = cairo_dock_calculate_dock_icons (pDock);
		//g_print ("pPointedIcon: %s\n", pPointedIcon?pPointedIcon->cName:"none");
		gtk_widget_queue_draw (pWidget);
		fLastTime = pMotion->time;
		
		//\_______________ On tire l'icone cliquee.
		if (s_pIconClicked != NULL && s_pIconClicked->iAnimationState != CAIRO_DOCK_STATE_REMOVE_INSERT && ! myDocksParam.bLockIcons && ! myDocksParam.bLockAll && (fabs (pMotion->x - s_iClickX) > CD_CLICK_ZONE || fabs (pMotion->y - s_iClickY) > CD_CLICK_ZONE) && ! pDock->bPreventDraggingIcons)
		{
			s_bIconDragged = TRUE;
			///s_pIconClicked->iAnimationState = CAIRO_DOCK_STATE_FOLLOW_MOUSE;
			cairo_dock_mark_icon_as_following_mouse (s_pIconClicked);
			//pDock->fAvoidingMouseMargin = .5;
			pDock->iAvoidingMouseIconType = s_pIconClicked->iGroup;  // on pourrait le faire lors du clic aussi.
			s_pIconClicked->fScale = cairo_dock_get_icon_max_scale (s_pIconClicked);
			s_pIconClicked->fDrawX = pDock->container.iMouseX  - s_pIconClicked->fWidth * s_pIconClicked->fScale / 2;
			s_pIconClicked->fDrawY = pDock->container.iMouseY - s_pIconClicked->fHeight * s_pIconClicked->fScale / 2 ;
			s_pIconClicked->fAlpha = 0.75;
		}

		//gdk_event_request_motions (pMotion);  // ce sera pour GDK 2.12.
		gdk_device_get_state (pMotion->device, pMotion->window, NULL, NULL);  // pour recevoir d'autres MotionNotify.
	}
	else  // cas d'un drag and drop.
	{
		//g_print ("motion on drag\n");
		//\_______________ On recupere la position de la souris.
		gldi_container_update_mouse_position (CAIRO_CONTAINER (pDock));
		
		//\_______________ On recalcule toutes les icones et on redessine.
		pPointedIcon = cairo_dock_calculate_dock_icons (pDock);
		gtk_widget_queue_draw (pWidget);
		
		pDock->fAvoidingMouseMargin = .25;  // on peut dropper entre 2 icones ...
		pDock->iAvoidingMouseIconType = CAIRO_DOCK_LAUNCHER;  // ... seulement entre 2 icones du groupe "lanceurs".
	}
	
	//\_______________ On gere le changement d'icone.
	gboolean bStartAnimation = FALSE;
	if (pPointedIcon != pLastPointedIcon)
	{
		cairo_dock_on_change_icon (pLastPointedIcon, pPointedIcon, pDock);
		
		if (pPointedIcon != NULL && s_pIconClicked != NULL && s_pIconClicked->iGroup == pPointedIcon->iGroup && ! myDocksParam.bLockIcons && ! myDocksParam.bLockAll && ! pDock->bPreventDraggingIcons)
		{
			_cairo_dock_make_icon_glide (pPointedIcon, s_pIconClicked, pDock);
			bStartAnimation = TRUE;
		}
	}
	
	//\_______________ On notifie tout le monde.
	gldi_object_notify (pDock, NOTIFICATION_MOUSE_MOVED, pDock, &bStartAnimation);
	if (bStartAnimation)
		cairo_dock_launch_animation (CAIRO_CONTAINER (pDock));
	
	return FALSE;
}

/*static gboolean _on_leave_dock_notification2 (G_GNUC_UNUSED gpointer data, CairoDock *pDock, G_GNUC_UNUSED gboolean *bStartAnimation)
{
	//\_______________ On gere le drag d'une icone hors du dock.
	if (s_pIconClicked != NULL
	&& (CAIRO_DOCK_ICON_TYPE_IS_LAUNCHER (s_pIconClicked)
		|| CAIRO_DOCK_ICON_TYPE_IS_CONTAINER (s_pIconClicked)
		|| (CAIRO_DOCK_ICON_TYPE_IS_SEPARATOR (s_pIconClicked) && s_pIconClicked->cDesktopFileName)
		|| CAIRO_DOCK_IS_DETACHABLE_APPLET (s_pIconClicked))
	&& s_pFlyingContainer == NULL
	&& ! myDocksParam.bLockIcons
	&& ! myDocksParam.bLockAll
	&& ! pDock->bPreventDraggingIcons)
	{
		cd_debug ("on a sorti %s du dock (%d;%d) / %dx%d", s_pIconClicked->cName, pDock->container.iMouseX, pDock->container.iMouseY, pDock->container.iWidth, pDock->container.iHeight);
		
		//if (! cairo_dock_hide_child_docks (pDock))  // on quitte si on entre dans un sous-dock, pour rester en position "haute".
		//	return ;
		
		CairoDock *pOriginDock = gldi_dock_get (s_pIconClicked->cParentDockName);
		g_return_val_if_fail (pOriginDock != NULL, TRUE);
		if (pOriginDock == pDock && _mouse_is_really_outside (pDock))  // ce test est la pour parer aux WM deficients mentaux comme KWin qui nous font sortir/rentrer lors d'un clic.
		{
			cd_debug (" on detache l'icone");
			pOriginDock->bIconIsFlyingAway = TRUE;
			cairo_dock_detach_icon_from_dock (s_pIconClicked, pOriginDock);
			///cairo_dock_update_dock_size (pOriginDock);
			cairo_dock_stop_icon_glide (pOriginDock);
			
			s_pFlyingContainer = gldi_flying_container_new (s_pIconClicked, pOriginDock);
			//g_print ("- s_pIconClicked <- NULL\n");
			s_pIconClicked = NULL;
			if (pDock->iRefCount > 0 || pDock->bAutoHide)  // pour garder le dock visible.
			{
				return GLDI_NOTIFICATION_INTERCEPT;
			}
		}
	}
	else if (s_pFlyingContainer != NULL && s_pFlyingContainer->pIcon != NULL && pDock->iRefCount > 0)  // on evite les bouclages.
	{
		CairoDock *pOriginDock = gldi_dock_get (s_pFlyingContainer->pIcon->cParentDockName);
		if (pOriginDock == pDock)
			return GLDI_NOTIFICATION_INTERCEPT;
	}
	return GLDI_NOTIFICATION_LET_PASS;
}*/

static gboolean _on_leave_notify (G_GNUC_UNUSED GtkWidget* pWidget, GdkEventCrossing* pEvent, CairoDock *pDock)
{
	//g_print ("%s (bInside:%d; iState:%d; iRefCount:%d)\n", __func__, pDock->container.bInside, pDock->iInputState, pDock->iRefCount);
	//\_______________ On tire le dock => on ignore le signal.
	if (pEvent != NULL && (pEvent->state & GDK_MOD1_MASK) && (pEvent->state & GDK_BUTTON1_MASK))
	{
		return FALSE;
	}
	
	//\_______________ On ignore les signaux errones venant d'un WM buggue (Kwin) ou meme de X (changement de bureau).
	//if (pEvent)
		//g_print ("leave event: %d;%d; %d;%d; %d; %d\n", (int)pEvent->x, (int)pEvent->y, (int)pEvent->x_root, (int)pEvent->y_root, pEvent->mode, pEvent->detail);
	if (pEvent && (pEvent->x != 0 ||  pEvent->y != 0 || pEvent->x_root != 0 || pEvent->y_root != 0))  // strange leave events occur (detail = GDK_NOTIFY_NONLINEAR, nil coordinates); let's ignore them!
	{
		if (pDock->container.bIsHorizontal)
		{
			pDock->container.iMouseX = pEvent->x;
			pDock->container.iMouseY = pEvent->y;
		}
		else
		{
			pDock->container.iMouseX = pEvent->y;
			pDock->container.iMouseY = pEvent->x;
		}
	}
	else
	{
		//g_print ("forced leave event: %d;%d\n", pDock->container.iMouseX, pDock->container.iMouseY);
	}
	if (/**pEvent && */!_mouse_is_really_outside(pDock))  // check that the mouse is really outside (the request might not come from the Window Manager, for instance if we deactivate the menu; this also works around buggy WM like KWin).
	{
		//g_print ("not really outside (%d;%d ; %d/%d)\n", pDock->container.iMouseX, pDock->container.iMouseY, pDock->iMaxDockHeight, pDock->iMinDockHeight);
		if (pDock->iSidTestMouseOutside == 0 && pEvent && ! pDock->bHasModalWindow)  // si l'action induit un changement de bureau, ou une appli qui bloque le focus (gksu), X envoit un signal de sortie alors qu'on est encore dans le dock, et donc n'en n'envoit plus lorsqu'on en sort reellement. On teste donc pendant qques secondes apres l'evenement. C'est ausi vrai pour l'affichage d'un menu/dialogue interactif, mais comme on envoie nous-meme un signal de sortie lorsque le menu disparait, il est inutile de le faire ici.
		{
			//g_print ("start checking mouse\n");
			pDock->iSidTestMouseOutside = g_timeout_add (500, (GSourceFunc)_check_mouse_outside, pDock);
		}
		//g_print ("mouse: %d;%d\n", pDock->container.iMouseX, pDock->container.iMouseY);
		return FALSE;
	}
	
	//\_______________ On retarde la sortie.
	if (pEvent != NULL)  // sortie naturelle.
	{
		if (pDock->iSidLeaveDemand == 0)  // pas encore de demande de sortie.
		{
			if (pDock->iRefCount == 0)  // cas du main dock : on retarde si on pointe sur un sous-dock (pour laisser le temps au signal d'entree dans le sous-dock d'etre traite) ou si l'on a l'auto-hide.
			{
				//g_print (" leave event : %.1f;%.1f (%dx%d)\n", pEvent->x, pEvent->y, pDock->container.iWidth, pDock->container.iHeight);
				Icon *pPointedIcon = cairo_dock_get_pointed_icon (pDock->icons);
				if (pPointedIcon != NULL && pPointedIcon->pSubDock != NULL && gldi_container_is_visible (CAIRO_CONTAINER (pPointedIcon->pSubDock)))
				{
					//g_print ("  on retarde la sortie du dock de %dms\n", MAX (myDocksParam.iLeaveSubDockDelay, 330));
					pDock->iSidLeaveDemand = g_timeout_add (MAX (myDocksParam.iLeaveSubDockDelay, 250), (GSourceFunc) _emit_leave_signal_delayed, (gpointer) pDock);
					return TRUE;
				}
				else if (pDock->bAutoHide)
				{
					const int delay = 0;  // 250
					if (delay != 0)  /// maybe try to se if we leaved the dock frankly, or just by a few pixels...
					{
						//g_print (" delay the leave event by %dms\n", delay);
						pDock->iSidLeaveDemand = g_timeout_add (250, (GSourceFunc) _emit_leave_signal_delayed, (gpointer) pDock);
						return TRUE;
					}
				}
			}
			else/** if (myDocksParam.iLeaveSubDockDelay != 0)*/  // cas d'un sous-dock : on retarde le cachage.
			{
				//g_print ("  on retarde la sortie du sous-dock de %dms\n", myDocksParam.iLeaveSubDockDelay);
				pDock->iSidLeaveDemand = g_timeout_add (MAX (myDocksParam.iLeaveSubDockDelay, 50), (GSourceFunc) _emit_leave_signal_delayed, (gpointer) pDock);
				return TRUE;
			}
		}
		else  // deja une sortie en attente.
		{
			//g_print ("une sortie est deja programmee\n");
			return TRUE;
		}
	}  // sinon c'est nous qui avons explicitement demande cette sortie, donc on continue.
	
	if (pDock->iSidTestMouseOutside != 0)
	{
		//g_print ("stop checking mouse (leave)\n");
		g_source_remove (pDock->iSidTestMouseOutside);
		pDock->iSidTestMouseOutside = 0;
	}
	
	
	//\_______________ Arrive ici, on est sorti du dock.
	pDock->container.bInside = FALSE;
	pDock->iAvoidingMouseIconType = -1;
	pDock->fAvoidingMouseMargin = 0;
	
	//\_______________ On cache ses sous-docks.
	if (! cairo_dock_hide_child_docks (pDock))  // on quitte si l'un des sous-docks reste visible (on est entre dedans), pour rester en position "haute".
		return TRUE;
	
	if (s_iSidShowSubDockDemand != 0 && (pDock->iRefCount == 0 || s_pSubDockShowing == pDock))  // si ce dock ou l'un des sous-docks etait programme pour se montrer, on annule.
	{
		g_source_remove (s_iSidShowSubDockDemand);
		s_iSidShowSubDockDemand = 0;
		s_pDockShowingSubDock = NULL;
		s_pSubDockShowing = NULL;
	}
	
	
	gboolean bStartAnimation = FALSE;
	gldi_object_notify (pDock, NOTIFICATION_LEAVE_DOCK, pDock, &bStartAnimation);
	if (bStartAnimation)
		cairo_dock_launch_animation (CAIRO_CONTAINER (pDock));
	
	return TRUE;
}

/**static gboolean _on_enter_notification (G_GNUC_UNUSED gpointer pData, CairoDock *pDock, G_GNUC_UNUSED gboolean *bStartAnimation)
{
	// si on rentre avec une icone volante, on la met dedans.
	if (s_pFlyingContainer != NULL)
	{
		Icon *pFlyingIcon = s_pFlyingContainer->pIcon;
		if (pDock != pFlyingIcon->pSubDock)  // on evite les boucles.
		{
			struct timeval tv;
			int r = gettimeofday (&tv, NULL);
			double t = 0.;
			if (r == 0)
				t = tv.tv_sec + tv.tv_usec * 1e-6;
			if (t - s_pFlyingContainer->fCreationTime > 1)  // on empeche le cas ou enlever l'icone fait augmenter le ratio du dock, et donc sa hauteur, et nous fait rentrer dedans des qu'on sort l'icone.
			{
				cd_debug ("on remet l'icone volante dans un dock (dock d'origine : %s)", pFlyingIcon->cParentDockName);
				gldi_object_unref (GLDI_OBJECT(s_pFlyingContainer));
				cairo_dock_stop_icon_animation (pFlyingIcon);
				cairo_dock_insert_icon_in_dock (pFlyingIcon, pDock, CAIRO_DOCK_ANIMATE_ICON);
				s_pFlyingContainer = NULL;
				pDock->bIconIsFlyingAway = FALSE;
			}
		}
	}
	
	return GLDI_NOTIFICATION_LET_PASS;
}*/
static gboolean _on_enter_notify (G_GNUC_UNUSED GtkWidget* pWidget, GdkEventCrossing* pEvent, CairoDock *pDock)
{
	//g_print ("%s (bIsMainDock : %d; bInside:%d; state:%d; iMagnitudeIndex:%d; input shape:%x; event:%p)\n", __func__, pDock->bIsMainDock, pDock->container.bInside, pDock->iInputState, pDock->iMagnitudeIndex, pDock->pShapeBitmap, pEvent);
	if (! cairo_dock_entrance_is_allowed (pDock))
	{
		cd_message ("* entree non autorisee");
		return FALSE;
	}
	
	// stop les timers.
	if (pDock->iSidLeaveDemand != 0)
	{
		g_source_remove (pDock->iSidLeaveDemand);
		pDock->iSidLeaveDemand = 0;
	}
	if (s_iSidShowSubDockDemand != 0)  // gere un cas tordu mais bien reel.
	{
		g_source_remove (s_iSidShowSubDockDemand);
		s_iSidShowSubDockDemand = 0;
	}
	if (pDock->iSidHideBack != 0)
	{
		//g_print ("remove hide back timeout\n");
		g_source_remove (pDock->iSidHideBack);
		pDock->iSidHideBack = 0;
	}
	if (pDock->iSidTestMouseOutside != 0)
	{
		//g_print ("stop checking mouse (enter)\n");
		g_source_remove (pDock->iSidTestMouseOutside);
		pDock->iSidTestMouseOutside = 0;
	}
	
	// input shape desactivee, le dock devient actif.
	if ((pDock->pShapeBitmap || pDock->pHiddenShapeBitmap) && pDock->iInputState != CAIRO_DOCK_INPUT_ACTIVE)
	{
		//g_print ("+++ input shape active on enter\n");
		cairo_dock_set_input_shape_active (pDock);
	}
	pDock->iInputState = CAIRO_DOCK_INPUT_ACTIVE;
	
	// si on etait deja dedans, ou qu'on etait cense l'etre, on relance juste le grossissement.
	/**if (pDock->container.bInside || pDock->bIsHiding)
	{
		pDock->container.bInside = TRUE;
		cairo_dock_start_growing (pDock);
		if (pDock->bIsHiding || cairo_dock_is_hidden (pDock))  // on (re)monte.
		{
			cd_debug ("  on etait deja dedans\n");
			cairo_dock_start_showing (pDock);
		}
		return FALSE;
	}*/
	
	gboolean bWasInside = pDock->container.bInside;
	pDock->container.bInside = TRUE;
	
	// animation d'entree.
	gboolean bStartAnimation = FALSE;
	gldi_object_notify (pDock, NOTIFICATION_ENTER_DOCK, pDock, &bStartAnimation);
	if (bStartAnimation)
		cairo_dock_launch_animation (CAIRO_CONTAINER (pDock));
	
	pDock->fDecorationsOffsetX = 0;
	cairo_dock_stop_quick_hide ();
	
	if (s_pIconClicked != NULL)  // on pourrait le faire a chaque motion aussi.
	{
		pDock->iAvoidingMouseIconType = s_pIconClicked->iGroup;
		pDock->fAvoidingMouseMargin = .5;  /// inutile il me semble ...
	}
	
	// si on rentre avec une icone volante, on la met dedans.
	if (s_pFlyingContainer != NULL)
	{
		Icon *pFlyingIcon = s_pFlyingContainer->pIcon;
		if (pDock != pFlyingIcon->pSubDock)  // on evite les boucles.
		{
			struct timeval tv;
			int r = gettimeofday (&tv, NULL);
			double t = 0.;
			if (r == 0)
				t = tv.tv_sec + tv.tv_usec * 1e-6;
			if (t - s_pFlyingContainer->fCreationTime > 1)  // on empeche le cas ou enlever l'icone fait augmenter le ratio du dock, et donc sa hauteur, et nous fait rentrer dedans des qu'on sort l'icone.
			{
				//g_print ("on remet l'icone volante dans un dock (dock d'origine : %s)\n", pFlyingIcon->cParentDockName);
				gldi_object_unref (GLDI_OBJECT(s_pFlyingContainer));
				cairo_dock_stop_icon_animation (pFlyingIcon);
				// reinsert the icon where it was dropped, not at its original position.
				Icon *icon = cairo_dock_get_pointed_icon (pDock->icons);  // get the pointed icon before we insert the icon, since the inserted icon will be the pointed one!
				//g_print (" pointed icon: %s\n", icon?icon->cName:"none");
				cairo_dock_insert_icon_in_dock (pFlyingIcon, pDock, CAIRO_DOCK_ANIMATE_ICON);
				if (icon != NULL && cairo_dock_get_icon_order (icon) == cairo_dock_get_icon_order (pFlyingIcon))
				{
					cairo_dock_move_icon_after_icon (pDock, pFlyingIcon, icon);
				}
				s_pFlyingContainer = NULL;
				pDock->bIconIsFlyingAway = FALSE;
			}
		}
	}
	
	// si on etait derriere, on repasse au premier plan.
	if (pDock->iVisibility == CAIRO_DOCK_VISI_KEEP_BELOW && pDock->bIsBelow && pDock->iRefCount == 0)
	{
		cairo_dock_pop_up (pDock);
	}
	
	// si on etait cache (entierement ou partiellement), on montre.
	if ((pDock->bIsHiding || cairo_dock_is_hidden (pDock)) && pDock->iRefCount == 0)
	{
		//g_print ("  on commence a monter\n");
		cairo_dock_start_showing (pDock);  // on a mis a jour la zone d'input avant, sinon la fonction le ferait, ce qui serait inutile.
	}
	
	// start growing up (do it before calculating icons, so that we don't seem to be in an anormal state, where we're inside a dock that doesn't grow).
	cairo_dock_start_growing (pDock);
	
	// since we've just entered the dock, the pointed icon has changed from none to the current one.
	if (pEvent != NULL && ! bWasInside)
	{
		// update the mouse coordinates
		if (pDock->container.bIsHorizontal)
		{
			pDock->container.iMouseX = (int) pEvent->x;
			pDock->container.iMouseY = (int) pEvent->y;
		}
		else
		{
			pDock->container.iMouseX = (int) pEvent->y;
			pDock->container.iMouseY = (int) pEvent->x;
		}
		// then compute the icons (especially the pointed one).
		Icon *icon = cairo_dock_calculate_dock_icons (pDock);  // returns the pointed icon
		// trigger the change to trigger the animation and sub-dock popup
		if (icon != NULL)
		{
			cairo_dock_on_change_icon (NULL, icon, pDock);  // we were out of the dock, so there is no previous pointed icon.
		}
	}
	
	return TRUE;
}


static gboolean _on_key_release (G_GNUC_UNUSED GtkWidget *pWidget,
	GdkEventKey *pKey,
	CairoDock *pDock)
{
	cd_debug ("on a appuye sur une touche (%d/%d)", pKey->keyval, pKey->hardware_keycode);
	if (pKey->type == GDK_KEY_PRESS)
	{
		gldi_object_notify (pDock, NOTIFICATION_KEY_PRESSED, pDock, pKey->keyval, pKey->state, pKey->string, pKey->hardware_keycode);
	}
	else if (pKey->type == GDK_KEY_RELEASE)
	{
		//g_print ("release : pKey->keyval = %d\n", pKey->keyval);
		if ((pKey->state & GDK_MOD1_MASK) && pKey->keyval == 0)  // On relache la touche ALT, typiquement apres avoir fait un ALT + clique gauche + deplacement.
		{
			if (pDock->iRefCount == 0 && pDock->iVisibility != CAIRO_DOCK_VISI_SHORTKEY)
				cairo_dock_write_root_dock_gaps (pDock);
		}
	}
	return TRUE;
}


static gboolean _double_click_delay_over (Icon *icon)
{
	CairoDock *pDock = gldi_dock_get (icon->cParentDockName);
	if (pDock)
	{
		cairo_dock_stop_icon_attention (icon, pDock);  // we consider that clicking on the icon is an acknowledge of the demand of attention.
		pDock->container.iMouseX = s_iFirstClickX;
		pDock->container.iMouseY = s_iFirstClickY;
		gldi_object_notify (pDock, NOTIFICATION_CLICK_ICON, icon, pDock, GDK_BUTTON1_MASK);
		
		cairo_dock_start_icon_animation (icon, pDock);
	}
	icon->iSidDoubleClickDelay = 0;
	return FALSE;
}
static gboolean _check_mouse_outside (CairoDock *pDock)  // ce test est principalement fait pour detecter les cas ou X nous envoit un signal leave errone alors qu'on est dedans (=> sortie refusee, bInside reste a TRUE), puis du coup ne nous en envoit pas de leave lorsqu'on quitte reellement le dock.
{
	//g_print ("%s (%d, %d, %d)\n", __func__, pDock->bIsShrinkingDown, pDock->iMagnitudeIndex, pDock->container.bInside);
	if (pDock->bIsShrinkingDown || pDock->iMagnitudeIndex == 0 || ! pDock->container.bInside)  // trivial cases : if the dock has already shrunk, or we're not inside any more, we can quit the loop.
	{
		pDock->iSidTestMouseOutside = 0;
		return FALSE;
	}
	
	gldi_container_update_mouse_position (CAIRO_CONTAINER (pDock));
	//g_print (" -> (%d, %d)\n", pDock->container.iMouseX, pDock->container.iMouseY);
	
	cairo_dock_calculate_dock_icons (pDock);  // pour faire retrecir le dock si on n'est pas dedans, merci X de nous faire sortir du dock alors que la souris est toujours dedans :-/
	return TRUE;
}
static gboolean _on_button_press (G_GNUC_UNUSED GtkWidget* pWidget, GdkEventButton* pButton, CairoDock *pDock)
{
	//g_print ("+ %s (%d/%d, %x)\n", __func__, pButton->type, pButton->button, pWidget);
	if (pDock->container.bIsHorizontal)  // utile ?
	{
		pDock->container.iMouseX = (int) pButton->x;
		pDock->container.iMouseY = (int) pButton->y;
	}
	else
	{
		pDock->container.iMouseX = (int) pButton->y;
		pDock->container.iMouseY = (int) pButton->x;
	}

	Icon *icon = cairo_dock_get_pointed_icon (pDock->icons);
	if (pButton->button == 1)  // clic gauche.
	{
		//g_print ("+ left click\n");
		switch (pButton->type)
		{
			case GDK_BUTTON_RELEASE :
				//g_print ("+ GDK_BUTTON_RELEASE (%d/%d sur %s/%s)\n", pButton->state, GDK_CONTROL_MASK | GDK_MOD1_MASK, icon ? icon->cName : "personne", icon ? icon->cCommand : "");  // 272 = 100010000
				if (pDock->container.bIgnoreNextReleaseEvent)
				{
					pDock->container.bIgnoreNextReleaseEvent = FALSE;
					s_pIconClicked = NULL;
					s_bIconDragged = FALSE;
					return TRUE;
				}
				
				if ( ! (pButton->state & GDK_MOD1_MASK))
				{
					if (s_pIconClicked != NULL)
					{
						cd_debug ("activate %s (%s)", s_pIconClicked->cName, icon ? icon->cName : "none");
						s_pIconClicked->iAnimationState = CAIRO_DOCK_STATE_REST;  // stoppe les animations de suivi du curseur.
						pDock->iAvoidingMouseIconType = -1;
						cairo_dock_stop_icon_glide (pDock);
					}
					if (icon != NULL && ! CAIRO_DOCK_ICON_TYPE_IS_SEPARATOR (icon) && icon == s_pIconClicked)  // released the button on the clicked icon => trigger the CLICK signal.
					{
						s_pIconClicked = NULL;  // il faut le faire ici au cas ou le clic induirait un dialogue bloquant qui nous ferait sortir du dock par exemple.
						//g_print ("+ click on '%s' (%s)\n", icon->cName, icon->cCommand);
						if (! s_bIconDragged)  // on ignore le drag'n'drop sur elle-meme.
						{
							if (icon->iNbDoubleClickListeners > 0)
							{
								if (icon->iSidDoubleClickDelay == 0)  // 1er release.
								{
									icon->iSidDoubleClickDelay = g_timeout_add (CD_DOUBLE_CLICK_DELAY, (GSourceFunc)_double_click_delay_over, icon);
									s_iFirstClickX = pDock->container.iMouseX;  // the mouse can move between the first and the second clicks; since the event is triggered when the second click occurs, the coordinates may be wrong -> we have to remember the position of the first click.
									s_iFirstClickY = pDock->container.iMouseY;
								}
							}
							else
							{
								cairo_dock_stop_icon_attention (icon, pDock);  // we consider that clicking on the icon is an acknowledge of the demand of attention.
								
								gldi_object_notify (pDock, NOTIFICATION_CLICK_ICON, icon, pDock, pButton->state);
								
								cairo_dock_start_icon_animation (icon, pDock);
							}
						}
					}
					else if (s_pIconClicked != NULL && icon != NULL && icon != s_pIconClicked && ! myDocksParam.bLockIcons && ! myDocksParam.bLockAll && ! pDock->bPreventDraggingIcons)  // released the icon on another one.
					{
						//g_print ("deplacement de %s\n", s_pIconClicked->cName);
						CairoDock *pOriginDock = CAIRO_DOCK (cairo_dock_get_icon_container (s_pIconClicked));
						if (pOriginDock != NULL && pDock != pOriginDock)
						{
							cairo_dock_detach_icon_from_dock (s_pIconClicked, pOriginDock);
							///cairo_dock_update_dock_size (pOriginDock);
							
							cairo_dock_update_icon_s_container_name (s_pIconClicked, icon->cParentDockName);
							
							cairo_dock_insert_icon_in_dock (s_pIconClicked, pDock, CAIRO_DOCK_ANIMATE_ICON);
						}
						
						Icon *prev_icon, *next_icon;
						if (icon->fXAtRest > s_pIconClicked->fXAtRest)
						{
							prev_icon = icon;
							next_icon = cairo_dock_get_next_icon (pDock->icons, icon);
						}
						else
						{
							prev_icon = cairo_dock_get_previous_icon (pDock->icons, icon);
							next_icon = icon;
						}
						if (icon->iGroup != s_pIconClicked->iGroup
						&& (prev_icon == NULL || prev_icon->iGroup != s_pIconClicked->iGroup)
						&& (next_icon == NULL || next_icon->iGroup != s_pIconClicked->iGroup))
						{
							s_pIconClicked = NULL;
							return FALSE;
						}
						//g_print ("deplacement de %s\n", s_pIconClicked->cName);
						///if (prev_icon != NULL && prev_icon->iGroup != s_pIconClicked->iGroup)  // the previous icon is in a different group -> we'll be at the beginning of our group.
						///	prev_icon = NULL;  // => move to the beginning of the group/dock
						cairo_dock_move_icon_after_icon (pDock, s_pIconClicked, prev_icon);
						
						cairo_dock_calculate_dock_icons (pDock);
						
						if (! CAIRO_DOCK_ICON_TYPE_IS_SEPARATOR (s_pIconClicked))
						{
							cairo_dock_request_icon_animation (s_pIconClicked, CAIRO_CONTAINER (pDock), "bounce", 2);
						}
						gtk_widget_queue_draw (pDock->container.pWidget);
					}
					
					if (s_pFlyingContainer != NULL)  // the user released the flying icon -> detach/destroy it, or insert it
					{
						cd_debug ("on relache l'icone volante");
						if (pDock->container.bInside)
						{
							//g_print ("  on la remet dans son dock d'origine\n");
							Icon *pFlyingIcon = s_pFlyingContainer->pIcon;
							gldi_object_unref (GLDI_OBJECT(s_pFlyingContainer));
							cairo_dock_stop_marking_icon_as_following_mouse (pFlyingIcon);
							// reinsert the icon where it was dropped, not at its original position.
							Icon *icon = cairo_dock_get_pointed_icon (pDock->icons);  // get the pointed icon before we insert the icon, since the inserted icon will be the pointed one!
							cairo_dock_insert_icon_in_dock (pFlyingIcon, pDock, CAIRO_DOCK_ANIMATE_ICON);
							if (icon != NULL && cairo_dock_get_icon_order (icon) == cairo_dock_get_icon_order (pFlyingIcon))
							{
								cairo_dock_move_icon_after_icon (pDock, pFlyingIcon, icon);
							}
						}
						else
						{
							gldi_flying_container_terminate (s_pFlyingContainer);  // supprime ou detache l'icone, l'animation se terminera toute seule.
						}
						s_pFlyingContainer = NULL;
						pDock->bIconIsFlyingAway = FALSE;
						cairo_dock_stop_icon_glide (pDock);
					}
					/// a implementer ...
					///gldi_object_notify (CAIRO_CONTAINER (pDock), CAIRO_DOCK_RELEASE_ICON, icon, pDock);
				}
				else
				{
					if (pDock->iRefCount == 0 && pDock->iVisibility != CAIRO_DOCK_VISI_SHORTKEY)
						cairo_dock_write_root_dock_gaps (pDock);
				}
				//g_print ("- apres clic : s_pIconClicked <- NULL\n");
				s_pIconClicked = NULL;
				s_bIconDragged = FALSE;
			break ;
			
			case GDK_BUTTON_PRESS :
				if ( ! (pButton->state & GDK_MOD1_MASK))
				{
					//g_print ("+ clic sur %s (%.2f)!\n", icon ? icon->cName : "rien", icon ? icon->fInsertRemoveFactor : 0.);
					s_iClickX = pButton->x;
					s_iClickY = pButton->y;
					if (icon && ! cairo_dock_icon_is_being_removed (icon) && ! CAIRO_DOCK_IS_AUTOMATIC_SEPARATOR (icon))
					{
						s_pIconClicked = icon;  // on ne definit pas l'animation FOLLOW_MOUSE ici , on le fera apres le 1er mouvement, pour eviter que l'icone soit dessinee comme tel quand on clique dessus alors que le dock est en train de jouer une animation (ca provoque un flash desagreable).
						cd_debug ("clicked on %s", icon->cName);
					}
					else
						s_pIconClicked = NULL;
				}
			break ;

			case GDK_2BUTTON_PRESS :
				if (icon && ! cairo_dock_icon_is_being_removed (icon))
				{
					if (icon->iSidDoubleClickDelay != 0)
					{
						g_source_remove (icon->iSidDoubleClickDelay);
						icon->iSidDoubleClickDelay = 0;
					}
					gldi_object_notify (pDock, NOTIFICATION_DOUBLE_CLICK_ICON, icon, pDock);
					if (icon->iNbDoubleClickListeners > 0)
						pDock->container.bIgnoreNextReleaseEvent = TRUE;
				}
			break ;

			default :
			break ;
		}
	}
	else if (pButton->button == 3 && pButton->type == GDK_BUTTON_PRESS)  // clique droit.
	{
		GtkWidget *menu = cairo_dock_build_menu (icon, CAIRO_CONTAINER (pDock));  // genere un CAIRO_DOCK_BUILD_CONTAINER_MENU et CAIRO_DOCK_BUILD_ICON_MENU.
		
		cairo_dock_popup_menu_on_icon (menu, icon, CAIRO_CONTAINER (pDock));
	}
	else if (pButton->button == 2 && pButton->type == GDK_BUTTON_PRESS)  // clique milieu.
	{
		if (icon && ! cairo_dock_icon_is_being_removed (icon))
		{
			gldi_object_notify (pDock, NOTIFICATION_MIDDLE_CLICK_ICON, icon, pDock);
		}
	}

	return FALSE;
}


static gboolean _on_scroll (G_GNUC_UNUSED GtkWidget* pWidget, GdkEventScroll* pScroll, CairoDock *pDock)
{
	if (pScroll->direction != GDK_SCROLL_UP && pScroll->direction != GDK_SCROLL_DOWN)  // on degage les scrolls horizontaux.
	{
		return FALSE;
	}
	Icon *icon = cairo_dock_get_pointed_icon (pDock->icons);  // can be NULL
	gldi_object_notify (pDock, NOTIFICATION_SCROLL_ICON, icon, pDock, pScroll->direction);
	
	return FALSE;
}


static gboolean _on_configure (GtkWidget* pWidget, GdkEventConfigure* pEvent, CairoDock *pDock)
{
	//g_print ("%s (%p, main dock : %d) : (%d;%d) (%dx%d)\n", __func__, pDock, pDock->bIsMainDock, pEvent->x, pEvent->y, pEvent->width, pEvent->height);
	// set the new actual size of the container
	gint iNewWidth, iNewHeight, iNewX, iNewY;
	if (pDock->container.bIsHorizontal)
	{
		iNewWidth = pEvent->width;
		iNewHeight = pEvent->height;
		
		iNewX = pEvent->x;
		iNewY = pEvent->y;
	}
	else
	{
		iNewWidth = pEvent->height;
		iNewHeight = pEvent->width;
		
		iNewX = pEvent->y;
		iNewY = pEvent->x;
	}
	
	gboolean bSizeUpdated = (iNewWidth != pDock->container.iWidth || iNewHeight != pDock->container.iHeight);
	gboolean bIsNowSized = (pDock->container.iWidth == 1 && pDock->container.iHeight == 1 && bSizeUpdated);
	gboolean bPositionUpdated = (pDock->container.iWindowPositionX != iNewX || pDock->container.iWindowPositionY != iNewY);
	pDock->container.iWidth = iNewWidth;
	pDock->container.iHeight = iNewHeight;
	pDock->container.iWindowPositionX = iNewX;
	pDock->container.iWindowPositionY = iNewY;
	
	if (pDock->container.iWidth == 1 && pDock->container.iHeight == 1)  // the X window has not yet reached its size.
	{
		return FALSE;
	}
	
	// if the size has changed, also update everything that depends on it.
	if (bSizeUpdated)  // changement de taille
	{
		// update mouse relative position inside the window
		gldi_container_update_mouse_position (CAIRO_CONTAINER (pDock));
		if (pDock->container.iMouseX < 0 || pDock->container.iMouseX > pDock->container.iWidth)  // utile ?
			pDock->container.iMouseX = 0;
		
		// update the input shape (it has been calculated in the function that made the resize)
		cairo_dock_update_input_shape (pDock);
		if (pDock->pHiddenShapeBitmap != NULL && pDock->iInputState == CAIRO_DOCK_INPUT_HIDDEN)
		{
			//g_print ("+++ input shape hidden on configure\n");
			cairo_dock_set_input_shape_hidden (pDock);
		}
		else if (pDock->pShapeBitmap != NULL && pDock->iInputState == CAIRO_DOCK_INPUT_AT_REST)
		{
			//g_print ("+++ input shape at rest on configure\n");
			cairo_dock_set_input_shape_at_rest (pDock);
		}
		else if (pDock->iInputState == CAIRO_DOCK_INPUT_ACTIVE)
		{
			//g_print ("+++ input shape active on configure\n");
			cairo_dock_set_input_shape_active (pDock);
		}
		
		// update the GL context
		if (g_bUseOpenGL)
		{
			if (! gldi_glx_make_current (CAIRO_CONTAINER (pDock)))
				return FALSE;
			
			cairo_dock_set_ortho_view (CAIRO_CONTAINER (pDock));
			
			glClearAccum (0., 0., 0., 0.);
			glClear (GL_ACCUM_BUFFER_BIT);
			
			if (pDock->iRedirectedTexture != 0)
			{
				_cairo_dock_delete_texture (pDock->iRedirectedTexture);
				pDock->iRedirectedTexture = cairo_dock_create_texture_from_raw_data (NULL, pEvent->width, pEvent->height);
			}
		}
		
		cairo_dock_calculate_dock_icons (pDock);
		//g_print ("configure size\n");
		cairo_dock_trigger_set_WM_icons_geometry (pDock);  // changement de position ou de taille du dock => on replace les icones.
		
		gldi_dialogs_replace_all ();
		
		if (bIsNowSized && g_bUseOpenGL)  // in OpenGL, the context is linked to the window; now that the window has a correct size, the context is ready -> draw things that couldn't be drawn until now.
		{
			Icon *icon;
			GList *ic;
			for (ic = pDock->icons; ic != NULL; ic = ic->next)
			{
				icon = ic->data;
				gboolean bDamaged = icon->bDamaged;  // if bNeedApplyBackground is also TRUE, applying the background will remove the 'damage' flag.
				if (icon->bNeedApplyBackground)  // if both are TRUE, we need to do both (for instance, the data-renderer might not redraw the icon (progressbar)). draw the bg first so that we don't draw it twice.
				{
					icon->bNeedApplyBackground = FALSE;  // set to FALSE, if it doesn't work here, it will probably never do.
					cairo_dock_apply_icon_background_opengl (icon);
				}
				if (bDamaged)
				{
					//g_print ("This icon %s is damaged\n", icon->cName);
					icon->bDamaged = FALSE;
					if (cairo_dock_get_icon_data_renderer (icon) != NULL)
					{
						cairo_dock_refresh_data_renderer (icon, CAIRO_CONTAINER (pDock));
					}
					else if (icon->iSubdockViewType != 0)
					{
						cairo_dock_draw_subdock_content_on_icon (icon, pDock);
					}
					else if (CAIRO_DOCK_IS_APPLET (icon))
					{
						gldi_module_instance_reload (icon->pModuleInstance, FALSE);  // easy but safe way to redraw the icon properly.
					}
					else  // if we don't know how the icon should be drawn, just reload it.
					{
						cairo_dock_load_icon_image (icon, CAIRO_CONTAINER (pDock));
					}
					if (pDock->iRefCount != 0)  // now that the icon image is correct, redraw the pointing icon if needed
						cairo_dock_trigger_redraw_subdock_content (pDock);
				}
			}
		}
	}
	else if (bPositionUpdated)  // changement de position.
	{
		//g_print ("configure x,y\n");
		cairo_dock_trigger_set_WM_icons_geometry (pDock);  // changement de position de la fenetre du dock => on replace les icones.
		
		gldi_dialogs_replace_all ();
	}
	
	if (pDock->iRefCount == 0 && (bSizeUpdated || bPositionUpdated))
	{
		if (pDock->iVisibility == CAIRO_DOCK_VISI_AUTO_HIDE_ON_OVERLAP)
		{
			GldiWindowActor *pActiveAppli = gldi_windows_get_active ();
			if (_cairo_dock_appli_is_on_our_way (pActiveAppli, pDock))  // la fenetre active nous gene.
			{
				if (!cairo_dock_is_temporary_hidden (pDock))
					cairo_dock_activate_temporary_auto_hide (pDock);
			}
			else
			{
				if (cairo_dock_is_temporary_hidden (pDock))
					cairo_dock_deactivate_temporary_auto_hide (pDock);
			}
		}
		else if (pDock->iVisibility == CAIRO_DOCK_VISI_AUTO_HIDE_ON_OVERLAP_ANY)
		{
			if (gldi_dock_search_overlapping_window (pDock) != NULL)
			{
				if (!cairo_dock_is_temporary_hidden (pDock))
					cairo_dock_activate_temporary_auto_hide (pDock);
			}
			else
			{
				if (cairo_dock_is_temporary_hidden (pDock))
					cairo_dock_deactivate_temporary_auto_hide (pDock);
			}
		}
	}
	
	gtk_widget_queue_draw (pWidget);
	
	return FALSE;
}



static gboolean s_bWaitForData = FALSE;
static gboolean s_bCouldDrop = FALSE;

void _on_drag_data_received (G_GNUC_UNUSED GtkWidget *pWidget, GdkDragContext *dc, gint x, gint y, GtkSelectionData *selection_data, G_GNUC_UNUSED guint info, guint time, CairoDock *pDock)
{
	cd_debug ("%s (%dx%d, %d, %d)", __func__, x, y, time, pDock->container.bInside);
	if (cairo_dock_is_hidden (pDock))  // X ne semble pas tenir compte de la zone d'input pour dropper les trucs...
		return ;
	//\_________________ On recupere l'URI.
	gchar *cReceivedData = (gchar *)gtk_selection_data_get_data (selection_data);
	g_return_if_fail (cReceivedData != NULL);
	int length = strlen (cReceivedData);
	if (cReceivedData[length-1] == '\n')
		cReceivedData[--length] = '\0';  // on vire le retour chariot final.
	if (cReceivedData[length-1] == '\r')
		cReceivedData[--length] = '\0';
	
	if (s_bWaitForData)
	{
		s_bWaitForData = FALSE;
		gdk_drag_status (dc, GDK_ACTION_COPY, time);
		cd_debug ("drag info : <%s>", cReceivedData);
		pDock->iAvoidingMouseIconType = CAIRO_DOCK_LAUNCHER;
		if (g_str_has_suffix (cReceivedData, ".desktop")/** || g_str_has_suffix (cReceivedData, ".sh")*/)
			pDock->fAvoidingMouseMargin = .5;  // on ne sera jamais dessus.
		else
			pDock->fAvoidingMouseMargin = .25;
		return ;
	}
	
	//\_________________ On arrete l'animation.
	//cairo_dock_stop_marking_icons (pDock);
	pDock->iAvoidingMouseIconType = -1;
	pDock->fAvoidingMouseMargin = 0;
	
	//\_________________ On arrete le timer.
	if (s_iSidActionOnDragHover != 0)
	{
		//cd_debug ("on annule la demande de montrage d'appli");
		g_source_remove (s_iSidActionOnDragHover);
		s_iSidActionOnDragHover = 0;
	}
	
	//\_________________ On calcule la position a laquelle on l'a lache.
	cd_debug (">>> cReceivedData : '%s' (%d/%d)", cReceivedData, s_bCouldDrop, pDock->bCanDrop);
	/* icon => drop on icon
	no icon => if order undefined: drop on dock; else: drop between 2 icons.*/
	Icon *pPointedIcon = NULL;
	double fOrder;
	if (s_bCouldDrop)  // can drop on the dock
	{
		cd_debug ("drop between icons");
		
		pPointedIcon = NULL;
		fOrder = 0;
		
		// try to guess where we dropped.
		int iDropX = (pDock->container.bIsHorizontal ? x : y);
		Icon *pNeighboorIcon;
		Icon *icon = NULL;
		GList *ic;
		for (ic = pDock->icons; ic != NULL; ic = ic->next)
		{
			icon = ic->data;
			if (icon->bPointed)
			{
				if (iDropX < icon->fDrawX + icon->fWidth * icon->fScale/2)  // on the left side of the icon
				{
					pNeighboorIcon = (ic->prev != NULL ? ic->prev->data : NULL);
					fOrder = (pNeighboorIcon != NULL ? (icon->fOrder + pNeighboorIcon->fOrder) / 2 : icon->fOrder - 1);
				}
				else  // on the right side of the icon
				{
					pNeighboorIcon = (ic->next != NULL ? ic->next->data : NULL);
					fOrder = (pNeighboorIcon != NULL ? (icon->fOrder + pNeighboorIcon->fOrder) / 2 : icon->fOrder + 1);
				}
				break;
			}
		}
		if (myDocksParam.bLockAll)  // locked, can't add anything.
		{
			gldi_dialog_show_temporary_with_default_icon (_("Sorry but the dock is locked"), icon, CAIRO_CONTAINER (pDock), 5000);
			gtk_drag_finish (dc, FALSE, FALSE, time);
			return ;
		}
	}
	else  // drop on an icon or nowhere.
	{
		pPointedIcon = cairo_dock_get_pointed_icon (pDock->icons);
		fOrder = CAIRO_DOCK_LAST_ORDER;
		if (pPointedIcon == NULL && ! g_str_has_suffix (cReceivedData, ".desktop"))  // no icon => abort, but .desktop are always added
		{
			cd_debug ("drop nowhere");
			gtk_drag_finish (dc, FALSE, FALSE, time);
			return;
		}
	}
	cd_debug ("drop on %s (%.2f)", pPointedIcon?pPointedIcon->cName:"dock", fOrder);
	
	cairo_dock_notify_drop_data (cReceivedData, pPointedIcon, fOrder, CAIRO_CONTAINER (pDock));
	
	gtk_drag_finish (dc, TRUE, FALSE, time);
}

/*static gboolean _on_drag_drop (GtkWidget *pWidget, GdkDragContext *dc, gint x, gint y, guint time, G_GNUC_UNUSED CairoDock *pDock)
{
	cd_message ("%s (%dx%d, %d)", __func__, x, y, time);
	GdkAtom target = gtk_drag_dest_find_target (pWidget, dc, NULL);
	gtk_drag_get_data (pWidget, dc, target, time);
	return TRUE;  // in a drop zone.
}*/


static gboolean _on_drag_motion (GtkWidget *pWidget, GdkDragContext *dc, gint x, gint y, guint time, CairoDock *pDock)
{
	cd_debug ("%s (%d;%d, %d)", __func__, x, y, time);
	
	//\_________________ On simule les evenements souris habituels.
	if (! pDock->bIsDragging)
	{
		cd_debug ("start dragging");
		pDock->bIsDragging = TRUE;
		
		/*GdkAtom gdkAtom = gdk_drag_get_selection (dc);
		Atom xAtom = gdk_x11_atom_to_xatom (gdkAtom);
		Window Xid = GDK_WINDOW_XID (dc->source_window);
		cd_debug (" <%s>", cairo_dock_get_property_name_on_xwindow (Xid, xAtom));*/
		
		gboolean bStartAnimation = FALSE;
		gldi_object_notify (pDock, NOTIFICATION_START_DRAG_DATA, pDock, &bStartAnimation);
		if (bStartAnimation)
			cairo_dock_launch_animation (CAIRO_CONTAINER (pDock));
		
		/*pDock->iAvoidingMouseIconType = -1;
		
		GdkAtom target = gtk_drag_dest_find_target (pWidget, dc, NULL);
		if (target == GDK_NONE)
			gdk_drag_status (dc, 0, time);
		else
		{
			gtk_drag_get_data (pWidget, dc, target, time);
			s_bWaitForData = TRUE;
			cd_debug ("get-data envoye\n");
		}*/
		
		_on_enter_notify (pWidget, NULL, pDock);  // ne sera effectif que la 1ere fois a chaque entree dans un dock.
	}
	else
	{
		//g_print ("move dragging\n");
		_on_motion_notify (pWidget, NULL, pDock);
	}
	
	int X, Y;
	if (pDock->container.bIsHorizontal)
	{
		X = x - pDock->container.iWidth/2;
		Y = y;
	}
	else
	{
		Y = x;
		X = y - pDock->container.iWidth/2;
	}
	int w, h;
	if (pDock->iInputState == CAIRO_DOCK_INPUT_AT_REST)
	{
		w = pDock->iMinDockWidth;
		h = pDock->iMinDockHeight;
		
		if (X <= -w/2 || X >= w/2)
			return FALSE;  // on n'accepte pas le drop.
		if (pDock->container.bDirectionUp)
		{
			if (Y < pDock->container.iHeight - h || Y >= pDock->container.iHeight)
				return FALSE;  // on n'accepte pas le drop.
		}
		else
		{
			if (Y < 0 || Y > h)
				return FALSE;  // on n'accepte pas le drop.
		}
	}
	else if (pDock->iInputState == CAIRO_DOCK_INPUT_HIDDEN)
	{
		return FALSE;  // on n'accepte pas le drop.
	}
	
	//g_print ("take the drop\n");
	gdk_drag_status (dc, GDK_ACTION_COPY, time);
	return TRUE;  // on accepte le drop.
}

void _on_drag_leave (GtkWidget *pWidget, G_GNUC_UNUSED GdkDragContext *dc, G_GNUC_UNUSED guint time, CairoDock *pDock)
{
	//g_print ("stop dragging 1\n");
	Icon *icon = cairo_dock_get_pointed_icon (pDock->icons);
	if ((icon && icon->pSubDock) || pDock->iRefCount > 0)  // on retarde l'evenement, car il arrive avant le leave-event, et donc le sous-dock se cache avant qu'on puisse y entrer.
	{
		cd_debug (">>> on attend...");
		while (gtk_events_pending ())  // on laisse le temps au signal d'entree dans le sous-dock d'etre traite, de facon a avoir un start-dragging avant de quitter cette fonction.
			gtk_main_iteration ();
		cd_debug (">>> pDock->container.bInside : %d", pDock->container.bInside);
	}
	//g_print ("stop dragging 2\n");
	s_bWaitForData = FALSE;
	pDock->bIsDragging = FALSE;
	s_bCouldDrop = pDock->bCanDrop;
	pDock->bCanDrop = FALSE;
	//cairo_dock_stop_marking_icons (pDock);
	pDock->iAvoidingMouseIconType = -1;
	
	// emit a leave-event signal, since we don't get one if we leave the window too quickly (!)
	if (pDock->iSidLeaveDemand == 0)
	{
		pDock->iSidLeaveDemand = g_timeout_add (MAX (myDocksParam.iLeaveSubDockDelay, 330), (GSourceFunc) _emit_leave_signal_delayed, (gpointer) pDock);  // emit with a delay, so that we can leave and enter the dock for a few ms without making it hide.
	}
	// emulate a motion event so that the mouse position is up-to-date (which is not the case if we leave the window too quickly).
	_on_motion_notify (pWidget, NULL, pDock);
}


///////////////////////////////////



static gboolean _cairo_dock_grow_up (CairoDock *pDock)
{
	//g_print ("%s (%d ; %2f ; bInside:%d)\n", __func__, pDock->iMagnitudeIndex, pDock->fFoldingFactor, pDock->container.bInside);
	
	pDock->iMagnitudeIndex += myBackendsParam.iGrowUpInterval;
	if (pDock->iMagnitudeIndex > CAIRO_DOCK_NB_MAX_ITERATIONS)
		pDock->iMagnitudeIndex = CAIRO_DOCK_NB_MAX_ITERATIONS;

	if (pDock->fFoldingFactor != 0)
	{
		int iAnimationDeltaT = cairo_dock_get_animation_delta_t (pDock);
		pDock->fFoldingFactor -= (double) iAnimationDeltaT / myBackendsParam.iUnfoldingDuration;
		if (pDock->fFoldingFactor < 0)
			pDock->fFoldingFactor = 0;
	}
	
	gldi_container_update_mouse_position (CAIRO_CONTAINER (pDock));
	
	Icon *pLastPointedIcon = cairo_dock_get_pointed_icon (pDock->icons);
	Icon *pPointedIcon = cairo_dock_calculate_dock_icons (pDock);
	if (! pDock->bIsGrowingUp)
		return FALSE;
	
	if (pLastPointedIcon != pPointedIcon && pDock->container.bInside)
		cairo_dock_on_change_icon (pLastPointedIcon, pPointedIcon, pDock);  /// probablement inutile...

	if (pDock->iMagnitudeIndex == CAIRO_DOCK_NB_MAX_ITERATIONS && pDock->fFoldingFactor == 0)  // fin de grossissement et de depliage.
	{
		gldi_dialogs_replace_all ();
		return FALSE;
	}
	else
		return TRUE;
}

static gboolean _cairo_dock_shrink_down (CairoDock *pDock)
{
	//g_print ("%s (%d, %f, %f)\n", __func__, pDock->iMagnitudeIndex, pDock->fFoldingFactor, pDock->fDecorationsOffsetX);
	//\_________________ On fait decroitre la magnitude du dock.
	pDock->iMagnitudeIndex -= myBackendsParam.iShrinkDownInterval;
	if (pDock->iMagnitudeIndex < 0)
		pDock->iMagnitudeIndex = 0;
	
	//\_________________ On replie le dock.
	if (pDock->fFoldingFactor != 0 && pDock->fFoldingFactor != 1)
	{
		int iAnimationDeltaT = cairo_dock_get_animation_delta_t (pDock);
		pDock->fFoldingFactor += (double) iAnimationDeltaT / myBackendsParam.iUnfoldingDuration;
		if (pDock->fFoldingFactor > 1)
			pDock->fFoldingFactor = 1;
	}
	
	//\_________________ On remet les decorations a l'equilibre.
	pDock->fDecorationsOffsetX *= .8;
	if (fabs (pDock->fDecorationsOffsetX) < 3)
		pDock->fDecorationsOffsetX = 0.;
	
	//\_________________ On recupere la position de la souris manuellement (car a priori on est hors du dock).
	gldi_container_update_mouse_position (CAIRO_CONTAINER (pDock));  // ce n'est pas le motion_notify qui va nous donner des coordonnees en dehors du dock, et donc le fait d'etre dedans va nous faire interrompre le shrink_down et re-grossir, du coup il faut le faire ici. L'inconvenient, c'est que quand on sort par les cotes, il n'y a soudain plus d'icone pointee, et donc le dock devient tout plat subitement au lieu de le faire doucement. Heureusement j'ai trouve une astuce. ^_^
	
	//\_________________ On recalcule les icones.
	///if (iPrevMagnitudeIndex != 0)
	{
		cairo_dock_calculate_dock_icons (pDock);
		if (! pDock->bIsShrinkingDown)
			return FALSE;
	}

	if (pDock->iMagnitudeIndex == 0 && (pDock->fFoldingFactor == 0 || pDock->fFoldingFactor == 1))  // on est arrive en bas.
	{
		//g_print ("equilibre atteint (%d)\n", pDock->container.bInside);
		if (! pDock->container.bInside)  // on peut etre hors des icones sans etre hors de la fenetre.
		{
			//g_print ("rideau !\n");
			
			//\__________________ On repasse derriere si on etait devant.
			if (pDock->iVisibility == CAIRO_DOCK_VISI_KEEP_BELOW && ! pDock->bIsBelow)
				cairo_dock_pop_down (pDock);
			
			//\__________________ On se redimensionne en taille normale.
			if (! pDock->bAutoHide && pDock->iRefCount == 0/** && ! pDock->bMenuVisible*/)  // fin de shrink sans auto-hide => taille normale.
			{
				//g_print ("taille normale (%x; %d)\n", pDock->pShapeBitmap , pDock->iInputState);
				if (pDock->pShapeBitmap && pDock->iInputState != CAIRO_DOCK_INPUT_AT_REST)
				{
					//g_print ("+++ input shape at rest on end shrinking\n");
					cairo_dock_set_input_shape_at_rest (pDock);
					pDock->iInputState = CAIRO_DOCK_INPUT_AT_REST;
				}
			}
			
			//\__________________ On se cache si sous-dock.
			if (pDock->iRefCount > 0)
			{
				//g_print ("on cache ce sous-dock en sortant par lui\n");
				gtk_widget_hide (pDock->container.pWidget);
				cairo_dock_hide_parent_dock (pDock);
			}
			///cairo_dock_hide_after_shortcut ();
			if (pDock->iVisibility == CAIRO_DOCK_VISI_SHORTKEY)  // hide at the end of the shrink animation
			{
				gtk_widget_hide (pDock->container.pWidget);
			}
		}
		else
		{
			cairo_dock_calculate_dock_icons (pDock);  // relance le grossissement si on est dedans.
		}
		if (!pDock->bIsGrowingUp)
			gldi_dialogs_replace_all ();
		return (!pDock->bIsGrowingUp && (pDock->fDecorationsOffsetX != 0 || (pDock->fFoldingFactor != 0 && pDock->fFoldingFactor != 1)));
	}
	else
	{
		return (!pDock->bIsGrowingUp);
	}
}

static gboolean _cairo_dock_hide (CairoDock *pDock)
{
	//g_print ("%s (%d, %.2f, %.2f)\n", __func__, pDock->iMagnitudeIndex, pDock->fHideOffset, pDock->fPostHideOffset);
	
	if (pDock->fHideOffset < 1)  // the hiding animation is running.
	{
		pDock->fHideOffset += 1./myBackendsParam.iHideNbSteps;
		if (pDock->fHideOffset > .99)  // fin d'anim.
		{
			pDock->fHideOffset = 1;
			
			//g_print ("on arrete le cachage\n");
			gboolean bVisibleIconsPresent = FALSE;
			Icon *pIcon;
			GList *ic;
			for (ic = pDock->icons; ic != NULL; ic = ic->next)
			{
				pIcon = ic->data;
				if (pIcon->fInsertRemoveFactor != 0)  // on accelere l'animation d'apparition/disparition.
				{
					if (pIcon->fInsertRemoveFactor > 0)
						pIcon->fInsertRemoveFactor = 0.05;
					else
						pIcon->fInsertRemoveFactor = - 0.05;
				}
				
				if (! pIcon->bIsDemandingAttention && ! pIcon->bAlwaysVisible)
					cairo_dock_stop_icon_animation (pIcon);  // s'il y'a une autre animation en cours, on l'arrete.
				else
					bVisibleIconsPresent = TRUE;
			}
			
			pDock->pRenderer->calculate_icons (pDock);
			///pDock->fFoldingFactor = (myBackendsParam.bAnimateOnAutoHide ? .99 : 0.);  // on arme le depliage.
			cairo_dock_allow_entrance (pDock);
			
			gldi_dialogs_replace_all ();
			
			if (bVisibleIconsPresent)  // il y'a des icones a montrer progressivement, on reste dans la boucle.
			{
				pDock->fPostHideOffset = 0.05;
				return TRUE;
			}
			else
			{
				pDock->fPostHideOffset = 1;  // pour que les icones demandant l'attention plus tard soient visibles.
				return FALSE;
			}
		}
	}
	else if (pDock->fPostHideOffset > 0 && pDock->fPostHideOffset < 1)  // the post-hiding animation is running.
	{
		pDock->fPostHideOffset += 1./myBackendsParam.iHideNbSteps;
		if (pDock->fPostHideOffset > .99)
		{
			pDock->fPostHideOffset = 1.;
			return FALSE;
		}
	}
	else  // else no hiding animation is running.
		return FALSE;
	return TRUE;
}

static gboolean _cairo_dock_show (CairoDock *pDock)
{
	pDock->fHideOffset -= 1./myBackendsParam.iUnhideNbSteps;
	if (pDock->fHideOffset < 0.01)
	{
		pDock->fHideOffset = 0;
		cairo_dock_allow_entrance (pDock);
		gldi_dialogs_replace_all ();  // we need it here so that a modal dialog is replaced when the dock unhides (else it would stay behind).
		return FALSE;
	}
	return TRUE;
}

static gboolean _cairo_dock_handle_inserting_removing_icons (CairoDock *pDock)
{
	gboolean bRecalculateIcons = FALSE;
	GList* ic = pDock->icons, *next_ic;
	Icon *pIcon;
	while (ic != NULL)
	{
		pIcon = ic->data;
		next_ic = ic->next;
		if (pIcon->fInsertRemoveFactor == (gdouble)0.05)
		{
			gboolean bIsAppli = CAIRO_DOCK_IS_NORMAL_APPLI (pIcon);
			if (bIsAppli)  // c'est une icone d'appli non vieille qui disparait, elle s'est probablement cachee => on la detache juste.
			{
				cd_message ("cette (%s) appli est toujours valide, on la detache juste", pIcon->cName);
				pIcon->fInsertRemoveFactor = 0.;  // on le fait avant le reload, sinon l'icone n'est pas rechargee.
				if (!pIcon->pAppli->bIsHidden && myTaskbarParam.bHideVisibleApplis)  // on lui remet l'image normale qui servira d'embleme lorsque l'icone sera inseree a nouveau dans le dock.
					cairo_dock_reload_icon_image (pIcon, CAIRO_CONTAINER (pDock));
				pDock = cairo_dock_detach_appli (pIcon);
				if (pDock == NULL)  // the dock has been destroyed (empty class sub-dock).
				{
					cairo_dock_free_icon (pIcon);
					return FALSE;
				}
			}
			else
			{
				cd_message (" - %s va etre supprimee", pIcon->cName);
				cairo_dock_remove_icon_from_dock (pDock, pIcon);  // enleve le separateur automatique avec; supprime le .desktop et le sous-dock des lanceurs; stoppe les applets; marque le theme.
				
				if (pIcon->cClass != NULL && pDock == cairo_dock_get_class_subdock (pIcon->cClass))  // appli icon in its class sub-dock => destroy the class sub-dock if it becomes empty (we don't want an empty sub-dock).
				{
					gboolean bEmptyClassSubDock = cairo_dock_check_class_subdock_is_empty (pDock, pIcon->cClass);
					if (bEmptyClassSubDock)
					{
						cairo_dock_free_icon (pIcon);
						return FALSE;
					}
				}
				
				cairo_dock_free_icon (pIcon);
			}
		}
		else if (pIcon->fInsertRemoveFactor == (gdouble)-0.05)
		{
			pIcon->fInsertRemoveFactor = 0;  // cela n'arrete pas l'animation, qui peut se poursuivre meme apres que l'icone ait atteint sa taille maximale.
			bRecalculateIcons = TRUE;
		}
		else if (pIcon->fInsertRemoveFactor != 0)
		{
			bRecalculateIcons = TRUE;
		}
		ic = next_ic;
	}
	
	if (bRecalculateIcons)
		cairo_dock_calculate_dock_icons (pDock);
	return TRUE;
}

static gboolean _cairo_dock_dock_animation_loop (GldiContainer *pContainer)
{
	CairoDock *pDock = CAIRO_DOCK (pContainer);
	gboolean bContinue = FALSE;
	gboolean bUpdateSlowAnimation = FALSE;
	pContainer->iAnimationStep ++;
	if (pContainer->iAnimationStep * pContainer->iAnimationDeltaT >= CAIRO_DOCK_MIN_SLOW_DELTA_T)
	{
		bUpdateSlowAnimation = TRUE;
		pContainer->iAnimationStep = 0;
		pContainer->bKeepSlowAnimation = FALSE;
	}
	
	if (pDock->bIsShrinkingDown)
	{
		pDock->bIsShrinkingDown = _cairo_dock_shrink_down (pDock);
		cairo_dock_redraw_container (CAIRO_CONTAINER (pDock));
		bContinue |= pDock->bIsShrinkingDown;
	}
	if (pDock->bIsGrowingUp)
	{
		pDock->bIsGrowingUp = _cairo_dock_grow_up (pDock);
		cairo_dock_redraw_container (CAIRO_CONTAINER (pDock));
		bContinue |= pDock->bIsGrowingUp;
	}
	if (pDock->bIsHiding)
	{
		//g_print ("le dock se cache\n");
		pDock->bIsHiding = _cairo_dock_hide (pDock);
		gtk_widget_queue_draw (pContainer->pWidget);  // on n'utilise pas cairo_dock_redraw_container, sinon a la derniere iteration, le dock etant cache, la fonction ne le redessine pas.
		bContinue |= pDock->bIsHiding;
	}
	if (pDock->bIsShowing)
	{
		pDock->bIsShowing = _cairo_dock_show (pDock);
		cairo_dock_redraw_container (CAIRO_CONTAINER (pDock));
		bContinue |= pDock->bIsShowing;
	}
	//g_print (" => %d, %d\n", pDock->bIsShrinkingDown, pDock->bIsGrowingUp);
	
	double fDockMagnitude = cairo_dock_calculate_magnitude (pDock->iMagnitudeIndex);
	gboolean bIconIsAnimating;
	gboolean bNoMoreDemandingAttention = FALSE;
	Icon *icon;
	GList *ic;
	for (ic = pDock->icons; ic != NULL; ic = ic->next)
	{
		icon = ic->data;
		
		icon->fDeltaYReflection = 0;
		if (myIconsParam.fAlphaAtRest != 1)
			icon->fAlpha = fDockMagnitude + myIconsParam.fAlphaAtRest * (1 - fDockMagnitude);
		
		bIconIsAnimating = FALSE;
		if (bUpdateSlowAnimation)
		{
			gldi_object_notify (icon, NOTIFICATION_UPDATE_ICON_SLOW, icon, pDock, &bIconIsAnimating);
			pContainer->bKeepSlowAnimation |= bIconIsAnimating;
		}
		gldi_object_notify (icon, NOTIFICATION_UPDATE_ICON, icon, pDock, &bIconIsAnimating);
		
		if ((icon->bIsDemandingAttention || icon->bAlwaysVisible) && cairo_dock_is_hidden (pDock))  // animation d'une icone demandant l'attention dans un dock cache => on force le dessin qui normalement ne se fait pas.
		{
			gtk_widget_queue_draw (pContainer->pWidget);
		}
		
		bContinue |= bIconIsAnimating;
		if (! bIconIsAnimating)
		{
			icon->iAnimationState = CAIRO_DOCK_STATE_REST;
			if (icon->bIsDemandingAttention)
			{
				icon->bIsDemandingAttention = FALSE;  // the attention animation has finished by itself after the time it was planned for.
				bNoMoreDemandingAttention = TRUE;
			}
		}
	}
	bContinue |= pContainer->bKeepSlowAnimation;
	
	if (pDock->iVisibility == CAIRO_DOCK_VISI_KEEP_BELOW && bNoMoreDemandingAttention && ! pDock->bIsBelow && ! pContainer->bInside)
	{
		//g_print ("plus de raison d'etre devant\n");
		cairo_dock_pop_down (pDock);
	}
	
	if (! _cairo_dock_handle_inserting_removing_icons (pDock))
	{
		cd_debug ("ce dock n'a plus de raison d'etre");
		return FALSE;
	}
	
	if (bUpdateSlowAnimation)
	{
		gldi_object_notify (pDock, NOTIFICATION_UPDATE_SLOW, pDock, &pContainer->bKeepSlowAnimation);
	}
	gldi_object_notify (pDock, NOTIFICATION_UPDATE, pDock, &bContinue);
	
	if (! bContinue && ! pContainer->bKeepSlowAnimation)
	{
		pContainer->iSidGLAnimation = 0;
		return FALSE;
	}
	else
		return TRUE;
}

static gboolean _on_dock_destroyed (GtkWidget *menu, GldiContainer *pContainer);
static void _on_menu_deactivated (G_GNUC_UNUSED GtkMenuShell *menu, CairoDock *pDock)
{
	//g_print ("\n+++ %s ()\n\n", __func__);
	g_return_if_fail (CAIRO_DOCK_IS_DOCK (pDock));
	if (pDock->bHasModalWindow)  // don't send the signal if the menu was already deactivated.
	{
		pDock->bHasModalWindow = FALSE;
		cairo_dock_emit_leave_signal (CAIRO_CONTAINER (pDock));
	}
}
static void _on_menu_destroyed (GtkWidget *menu, CairoDock *pDock)
{
	//g_print ("\n+++ %s ()\n\n", __func__);
	gldi_object_remove_notification (pDock,
		NOTIFICATION_DESTROY,
		(GldiNotificationFunc) _on_dock_destroyed,
		menu);
}
static gboolean _on_dock_destroyed (GtkWidget *menu, GldiContainer *pContainer)
{
	//g_print ("\n+++ %s ()\n\n", __func__);
	g_signal_handlers_disconnect_matched
		(menu,
		G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA,
		0,
		0,
		NULL,
		_on_menu_deactivated,
		pContainer);
	g_signal_handlers_disconnect_matched
		(menu,
		G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA,
		0,
		0,
		NULL,
		_on_menu_destroyed,
		pContainer);
	return GLDI_NOTIFICATION_LET_PASS;
}
static void _setup_menu (GldiContainer *pContainer, G_GNUC_UNUSED Icon *pIcon, GtkWidget *pMenu)
{
	// keep the dock visible
	CAIRO_DOCK (pContainer)->bHasModalWindow = TRUE;
	
	// connect signals
	if (g_signal_handler_find (pMenu,
		G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA,
		0,
		0,
		NULL,
		_on_menu_deactivated,
		pContainer) == 0)  // on evite de connecter 2 fois ce signal, donc la fonction est appelable plusieurs fois sur un meme menu.
	{
		// when the menu is deactivated, hide the dock back if necessary.
		g_signal_connect (G_OBJECT (pMenu),
			"deactivate",
			G_CALLBACK (_on_menu_deactivated),
			pContainer);
		// when the menu is destroyed, remove the 'destroyed' notification on the dock.
		g_signal_connect (G_OBJECT (pMenu),
			"destroy",
			G_CALLBACK (_on_menu_destroyed),
			pContainer);
		// when the dock is destroyed, remove the 2 signals on the menu.
		gldi_object_register_notification (pContainer,
			NOTIFICATION_DESTROY,
			(GldiNotificationFunc) _on_dock_destroyed,
			GLDI_RUN_AFTER, pMenu);  // the menu can stay alive even if the container disappear, so we need to ensure we won't call the callbacks then.
	}
}

void gldi_dock_init_internals (CairoDock *pDock)
{
	pDock->container.iface.animation_loop = _cairo_dock_dock_animation_loop;
	pDock->container.iface.setup_menu = _setup_menu;
	
	//\__________________ set up its window
	GtkWidget *pWindow = pDock->container.pWidget;
	gtk_container_set_border_width (GTK_CONTAINER (pWindow), 0);
	gtk_window_set_gravity (GTK_WINDOW (pWindow), GDK_GRAVITY_STATIC);
	gtk_window_set_type_hint (GTK_WINDOW (pWindow), GDK_WINDOW_TYPE_HINT_DOCK);  // window must not be mapped
	
	//\__________________ connect to events.
	gtk_widget_add_events (pWindow,
		GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_SCROLL_MASK |
		GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK |
		GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK);
	
	g_signal_connect (G_OBJECT (pWindow),
		#if (GTK_MAJOR_VERSION < 3)
		"expose-event",
		#else
		"draw",
		#endif
		G_CALLBACK (_on_expose),
		pDock);
	g_signal_connect (G_OBJECT (pWindow),
		"configure-event",
		G_CALLBACK (_on_configure),
		pDock);
	g_signal_connect (G_OBJECT (pWindow),
		"key-release-event",
		G_CALLBACK (_on_key_release),
		pDock);
	g_signal_connect (G_OBJECT (pWindow),
		"key-press-event",
		G_CALLBACK (_on_key_release),
		pDock);
	g_signal_connect (G_OBJECT (pWindow),
		"button-press-event",
		G_CALLBACK (_on_button_press),
		pDock);
	g_signal_connect (G_OBJECT (pWindow),
		"button-release-event",
		G_CALLBACK (_on_button_press),
		pDock);
	g_signal_connect (G_OBJECT (pWindow),
		"scroll-event",
		G_CALLBACK (_on_scroll),
		pDock);
	g_signal_connect (G_OBJECT (pWindow),
		"motion-notify-event",
		G_CALLBACK (_on_motion_notify),
		pDock);
	g_signal_connect (G_OBJECT (pWindow),
		"enter-notify-event",
		G_CALLBACK (_on_enter_notify),
		pDock);
	g_signal_connect (G_OBJECT (pWindow),
		"leave-notify-event",
		G_CALLBACK (_on_leave_notify),
		pDock);
	gldi_container_enable_drop (CAIRO_CONTAINER (pDock),
		G_CALLBACK (_on_drag_data_received),
		pDock);
	g_signal_connect (G_OBJECT (pWindow),
		"drag-motion",
		G_CALLBACK (_on_drag_motion),
		pDock);
	g_signal_connect (G_OBJECT (pWindow),
		"drag-leave",
		G_CALLBACK (_on_drag_leave),
		pDock);
	/*g_signal_connect (G_OBJECT (pWindow),
		"drag-drop",
		G_CALLBACK (_on_drag_drop),
		pDock);*/
	
	gtk_widget_show_all (pDock->container.pWidget);
}
