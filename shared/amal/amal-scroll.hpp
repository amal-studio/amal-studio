/******************************************************************************
    Amal Studio - panneaux défilants
    Copyright (C) 2026 Amal Ben Aly - GNU GPL v2 ou ultérieure.

    Quand un panneau est plus petit que son contenu, une barre de défilement
    apparaît au lieu de couper le bas du panneau.
******************************************************************************/

#pragma once

#include <QDockWidget>
#include <QFrame>
#include <QScrollArea>

namespace amal {

/* Place « content » dans une zone défilante (le contenu garde sa taille minimale). */
inline QScrollArea *MakeScrollable(QWidget *content, QWidget *parent = nullptr)
{
	QScrollArea *scroll = new QScrollArea(parent);
	scroll->setObjectName(content->objectName().isEmpty() ? QStringLiteral("amalScroll")
							       : content->objectName() + QStringLiteral("Scroll"));
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidgetResizable(true);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	scroll->viewport()->setAutoFillBackground(false);
	scroll->setWidget(content);
	return scroll;
}

/* Rend défilant le contenu d'un panneau déjà construit. */
inline void MakeDockScrollable(QDockWidget *dock)
{
	if (!dock) {
		return;
	}
	QWidget *content = dock->widget();
	if (!content || qobject_cast<QScrollArea *>(content)) {
		return;
	}
	QScrollArea *scroll = MakeScrollable(content, dock);
	dock->setWidget(scroll);
}

} // namespace amal
