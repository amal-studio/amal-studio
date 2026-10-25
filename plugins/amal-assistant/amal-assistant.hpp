/******************************************************************************
    Amal Studio - module « Assistants et organisation » (déclarations communes)
    Copyright (C) 2026 Amal Ben Aly

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include <obs-module.h>
#include <QString>

namespace amal {

inline QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

/* amal-assistant.cpp */
void OpenAssistant();

/* amal-micro.cpp : assistant micro (tous types de micros) */
void OpenMicAssistant();

/* amal-espace.cpp : menu « Amal » et rangement des panneaux */
void SetupAmalMenu();
void ArrangePanelsOnce();
void ArrangePanels();

} // namespace amal
