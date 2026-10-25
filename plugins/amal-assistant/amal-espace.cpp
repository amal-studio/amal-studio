/******************************************************************************
    Amal Studio - organisation de l'espace de travail
    Copyright (C) 2026 Amal Ben Aly

    Les panneaux Amal sont nombreux. Pour éviter le désordre :
    - menu « Amal » dans la barre de menus, qui regroupe tous les panneaux
      par usage et les assistants ;
    - rangement en deux groupes d'onglets à droite :
        « Pendant le direct »   : Coran, Bibliothèque, Relecture ;
        « Préparation »         : Studio graphique, Dispositions,
                                  Caméra téléphone, Multidiffusion, Projets ;
    - « Mode direct » n'affiche que les panneaux utiles en direct,
      « Mode préparation » les affiche tous.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "amal-assistant.hpp"

#include <obs-frontend-api.h>
#include <util/config-file.h>

#include <QAction>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QUrl>
#include <QDockWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QTimer>

#include <vector>

namespace amal {
namespace {

constexpr const char *CONFIG_SECTION = "AmalEspace";
constexpr int ARRANGE_VERSION = 1; /* à augmenter si la disposition par défaut change */

struct Panel {
	const char *id;
	const char *nameKey;
	bool live; /* utile pendant le direct */
};

const Panel PANELS[] = {
	{"amal-coran", "Panel.Quran", true},
	{"amal-bibliotheque", "Panel.Library", true},
	{"amal-relecture", "Panel.Replay", true},
	{"amal-habillage", "Panel.Studio", false},
	{"amal-dispositions", "Panel.Layouts", false},
	{"amal-telephone", "Panel.Phone", false},
	{"amal-multidiffusion", "Panel.Multi", false},
	{"amal-projets", "Panel.Projects", false},
};

QMainWindow *MainWindow()
{
	return static_cast<QMainWindow *>(obs_frontend_get_main_window());
}

QDockWidget *FindDock(const char *id)
{
	QMainWindow *main = MainWindow();
	return main ? main->findChild<QDockWidget *>(QString::fromUtf8(id)) : nullptr;
}

void ShowPanel(const char *id)
{
	QDockWidget *dock = FindDock(id);
	if (!dock) {
		return;
	}
	dock->show();
	dock->raise();
	dock->activateWindow();
}

/* Groupe d'onglets : le premier panneau sert d'ancre, les autres s'y ajoutent */
QDockWidget *Tabify(QMainWindow *main, bool live)
{
	QDockWidget *anchor = nullptr;
	for (const Panel &p : PANELS) {
		if (p.live != live) {
			continue;
		}
		QDockWidget *dock = FindDock(p.id);
		if (!dock) {
			continue;
		}
		dock->setFloating(false);
		dock->show();
		if (!anchor) {
			main->addDockWidget(Qt::RightDockWidgetArea, dock);
			anchor = dock;
		} else {
			main->tabifyDockWidget(anchor, dock);
		}
	}
	return anchor;
}

/* Guide PDF installé à la racine du dossier d'Amal Studio (bin/64bit/../../) */
void OpenGuide()
{
	const QString path = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../../Guide-Amal-Studio.pdf");
	if (QFileInfo::exists(path) && QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::cleanPath(path)))) {
		return;
	}
	QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/amal-studio/amal-studio/releases")));
	QMessageBox::information(MainWindow(), T("Menu.Guide"), T("Guide.Missing"));
}

void SetMode(bool liveOnly)
{
	for (const Panel &p : PANELS) {
		QDockWidget *dock = FindDock(p.id);
		if (dock) {
			dock->setVisible(p.live || !liveOnly);
		}
	}
	ShowPanel(PANELS[0].id);
}

} // namespace

void ArrangePanels()
{
	QMainWindow *main = MainWindow();
	if (!main) {
		return;
	}
	QDockWidget *liveGroup = Tabify(main, true);
	QDockWidget *prepGroup = Tabify(main, false);
	if (liveGroup && prepGroup) {
		main->splitDockWidget(liveGroup, prepGroup, Qt::Vertical);
		main->resizeDocks({liveGroup, prepGroup}, {55, 45}, Qt::Vertical);
		main->resizeDocks({liveGroup}, {420}, Qt::Horizontal);
	}
	ShowPanel("amal-coran");
	ShowPanel("amal-habillage");
}

/* Une seule fois (et à chaque nouvelle disposition par défaut) */
void ArrangePanelsOnce()
{
	config_t *config = obs_frontend_get_app_config();
	if (!config || config_get_int(config, CONFIG_SECTION, "ArrangeVersion") >= ARRANGE_VERSION) {
		return;
	}
	/* Après le placement individuel de chaque panneau au premier démarrage */
	QTimer::singleShot(800, MainWindow(), [] {
		ArrangePanels();
		config_t *cfg = obs_frontend_get_app_config();
		config_set_int(cfg, CONFIG_SECTION, "ArrangeVersion", ARRANGE_VERSION);
		config_save_safe(cfg, "tmp", nullptr);
	});
}

void SetupAmalMenu()
{
	QMainWindow *main = MainWindow();
	if (!main || main->findChild<QMenu *>("amalMenu")) {
		return;
	}
	QMenu *menu = new QMenu(T("Menu.Amal"), main);
	menu->setObjectName("amalMenu");

	menu->addSection(T("Menu.LivePanels"));
	for (const Panel &p : PANELS) {
		if (p.live) {
			const char *id = p.id;
			menu->addAction(T(p.nameKey), main, [id] { ShowPanel(id); });
		}
	}
	menu->addSection(T("Menu.PrepPanels"));
	for (const Panel &p : PANELS) {
		if (!p.live) {
			const char *id = p.id;
			menu->addAction(T(p.nameKey), main, [id] { ShowPanel(id); });
		}
	}
	menu->addSection(T("Menu.Workspace"));
	menu->addAction(T("Menu.LiveMode"), main, [] { SetMode(true); });
	menu->addAction(T("Menu.PrepMode"), main, [] { SetMode(false); });
	menu->addAction(T("Menu.Arrange"), main, [] { ArrangePanels(); });
	menu->addSection(T("Menu.Assistants"));
	menu->addAction(T("Menu.Startup"), main, [] { OpenAssistant(); });
	menu->addAction(T("Menu.Mic"), main, [] { OpenMicAssistant(); });
	menu->addSection(T("Menu.Help"));
	menu->addAction(T("Menu.Guide"), main, [] { OpenGuide(); });

	/* Avant le menu « Aide » (dernier menu) */
	QMenuBar *bar = main->menuBar();
	const QList<QAction *> actions = bar->actions();
	if (actions.isEmpty()) {
		bar->addMenu(menu);
	} else {
		bar->insertMenu(actions.last(), menu);
	}
}

} // namespace amal
