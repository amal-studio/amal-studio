/******************************************************************************
    Amal Studio - état de la licence Pro, pour les modules Amal
    Copyright (C) 2026 Amal Ben Aly

    Interroge le module amal-licence. S'il est absent, les fonctions Pro sont
    considérées comme verrouillées.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include <obs.h>

namespace amal {

inline bool ProActive()
{
	calldata_t cd = {};
	bool pro = false;
	if (proc_handler_call(obs_get_proc_handler(), "amal_licence_etat", &cd)) {
		pro = calldata_bool(&cd, "pro");
	}
	calldata_free(&cd);
	return pro;
}

/* Ouvre la fenêtre de licence (achat, activation) */
inline void OpenLicence()
{
	calldata_t cd = {};
	proc_handler_call(obs_get_proc_handler(), "amal_licence_ouvrir", &cd);
	calldata_free(&cd);
}

} // namespace amal
