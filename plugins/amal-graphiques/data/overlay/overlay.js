/*
 * Amal Studio - affichage des habillages graphiques
 * Copyright (C) 2026 Amal Ben Aly - GNU GPL v2 ou ultérieure.
 *
 * Les données arrivent en JSON après le « # » de l'adresse. Tous les textes
 * sont insérés avec textContent : aucun code HTML saisi n'est interprété.
 */
(function () {
	'use strict';

	var LABELS = {
		verset: 'Coran',
		hadith: 'Hadith',
		invocation: 'Invocation',
		rappel: 'Rappel'
	};

	function readData() {
		var raw = window.location.hash.replace(/^#/, '');
		if (!raw) {
			return {};
		}
		try {
			return JSON.parse(decodeURIComponent(raw));
		} catch (e) {
			return {};
		}
	}

	function str(value) {
		return typeof value === 'string' ? value.trim() : '';
	}

	function el(tag, className, text, attrs) {
		var node = document.createElement(tag);
		if (className) {
			node.className = className;
		}
		if (text) {
			node.textContent = text;
		}
		if (attrs) {
			Object.keys(attrs).forEach(function (key) {
				node.setAttribute(key, attrs[key]);
			});
		}
		return node;
	}

	function arabic(text, quran) {
		return el('p', quran ? 'arabic quran' : 'arabic', text, { dir: 'rtl', lang: 'ar' });
	}

	/* Verset, hadith, invocation, rappel : même panneau, libellé différent */
	function buildPanel(d, page) {
		var card = el('div', 'card panel');
		var label = str(d.label) || LABELS[d.t] || '';
		var ar = page ? page.ar : str(d.ar);
		var fr = page ? page.fr : str(d.fr);
		var ref = str(d.ref);
		if (page && page.total > 1) {
			card.appendChild(el('div', 'pager', page.index + ' / ' + page.total));
		}

		if (label) {
			card.appendChild(el('div', 'label', label));
		}
		if (ar) {
			card.appendChild(arabic(ar, d.t === 'verset'));
		}
		if (fr) {
			card.appendChild(el('p', ar ? 'translation' : 'translation main', fr));
		}
		if (ref) {
			card.appendChild(el('div', 'divider'));
			card.appendChild(el('div', 'reference', ref));
		}
		return card;
	}

	function buildSpeaker(d) {
		var card = el('div', 'card speaker');
		card.appendChild(el('div', 'bar'));
		var body = el('div', 'body');
		body.appendChild(el('div', 'name', str(d.title) || 'Nom de l’intervenant'));
		if (str(d.ar)) {
			body.appendChild(el('div', 'name-ar', str(d.ar), { dir: 'rtl', lang: 'ar' }));
		}
		if (str(d.subtitle)) {
			body.appendChild(el('div', 'role', str(d.subtitle)));
		}
		card.appendChild(body);
		return card;
	}

	function buildTitle(d) {
		var card = el('div', 'card title-card');
		card.appendChild(el('div', 'star'));
		card.appendChild(el('div', 'title', str(d.title) || 'Titre de la conférence'));
		if (str(d.ar)) {
			card.appendChild(el('div', 'title-ar', str(d.ar), { dir: 'rtl', lang: 'ar' }));
		}
		if (str(d.subtitle)) {
			card.appendChild(el('div', 'subtitle', str(d.subtitle)));
		}
		if (str(d.ref)) {
			card.appendChild(el('div', 'info', str(d.ref)));
		}
		return card;
	}

	function buildTicker(d) {
		var card = el('div', 'card ticker');
		card.appendChild(el('div', 'tag', str(d.label) || 'Annonce'));
		var track = el('div', 'track');
		var scroll = el('div', 'scroll', str(d.fr) || 'Saisissez le texte du bandeau défilant');
		track.appendChild(scroll);
		card.appendChild(track);
		return card;
	}

	function applyTicker(card, d) {
		var scroll = card.querySelector('.scroll');
		if (!scroll) {
			return;
		}
		/* Vitesse en pixels par seconde, quelle que soit la longueur du texte */
		var speed = Math.max(40, Math.min(600, Number(d.speed) || 140));
		var distance = scroll.scrollWidth;
		scroll.style.animationDuration = (distance / speed).toFixed(2) + 's';
	}

	/* ------------------------------------------------------------------ */
	/* Bandeau TV                                                           */

	var clockTimer = null;

	function shiftedDate(days) {
		var now = new Date();
		now.setDate(now.getDate() + days);
		return now;
	}

	function formatHijri(offset) {
		try {
			return new Intl.DateTimeFormat('ar-u-ca-islamic-umalqura-nu-latn', {
				day: '2-digit', month: 'long', year: 'numeric'
			}).format(shiftedDate(offset));
		} catch (e) {
			return '';
		}
	}

	function formatGregorian() {
		return new Intl.DateTimeFormat('fr-FR', { weekday: 'long', day: 'numeric', month: 'long', year: 'numeric' })
			.format(new Date());
	}

	function formatClock() {
		return new Intl.DateTimeFormat('fr-FR', { hour: '2-digit', minute: '2-digit', hour12: false }).format(new Date());
	}

	/* Logo : uniquement un fichier local de l'utilisateur, transmis par Amal Studio */
	function safeLogo(url) {
		return typeof url === 'string' && url.indexOf('http://absolute/') === 0 ? url : '';
	}

	function buildBroadcast(d) {
		var tv = el('div', 'tv');
		var dateMode = d.date || 'les-deux';
		var offset = Math.max(-2, Math.min(2, Number(d.hijri) || 0));

		/* Haut droite : logo et badge EN DIRECT */
		var corner = el('div', 'haut-droite');
		var logo = safeLogo(d.logo);
		if (logo) {
			var img = el('img', 'logo', '', { src: logo, alt: '' });
			corner.appendChild(img);
		}
		if (d.live) {
			corner.appendChild(el('div', 'direct', str(d.label) || 'En direct'));
		}
		if (corner.childNodes.length) {
			tv.appendChild(corner);
		}

		var bottom = el('div', 'zone-bas');

		/* Gauche : date(s) et information libre */
		var left = el('div', 'coin gauche');
		if (dateMode === 'hegire' || dateMode === 'les-deux') {
			var hijri = formatHijri(offset);
			if (hijri) {
				left.appendChild(el('div', 'hegire', hijri, { dir: 'rtl', lang: 'ar' }));
			}
		}
		if (dateMode === 'gregorienne' || dateMode === 'les-deux') {
			left.appendChild(el('div', 'greg', formatGregorian()));
		}
		if (str(d.info)) {
			left.appendChild(el('div', 'info', str(d.info)));
		}
		bottom.appendChild(left.childNodes.length ? left : el('div'));

		/* Centre : bannière de titre */
		if (str(d.title) || str(d.subtitle)) {
			var banner = el('div', 'banniere');
			if (str(d.title)) banner.appendChild(el('div', 'ligne1', str(d.title), { dir: 'auto' }));
			if (str(d.subtitle)) banner.appendChild(el('div', 'ligne2', str(d.subtitle), { dir: 'auto' }));
			bottom.appendChild(banner);
		}

		/* Droite : horloge */
		if (d.clock) {
			var right = el('div', 'coin droite');
			var clock = el('div', 'horloge', formatClock());
			right.appendChild(clock);
			bottom.appendChild(right);
			clockTimer = window.setInterval(function () { clock.textContent = formatClock(); }, 1000);
		} else {
			bottom.appendChild(el('div'));
		}
		tv.appendChild(bottom);

		/* Bande défilante en option */
		if (d.band && str(d.fr)) {
			tv.classList.add('avec-bande');
			var ticker = buildTicker({ fr: d.fr, label: str(d.bandLabel) || 'Infos' });
			ticker.classList.remove('card');
			tv.appendChild(ticker);
		}
		return tv;
	}

	/* ------------------------------------------------------------------ */
	/* Cadres d'événement : un habillage plein écran avec une « fenêtre »   */
	/* transparente où l'on place la caméra.                               */
	/* Fenêtres (unités 1920 x 1080), identiques côté Studio graphique :    */
	/*   cadre   : x 132, y 92, l 1656, h 760                               */
	/*   lateral : x 600, y 0,  l 1320, h 870                               */

	var WINDOWS = {
		cadre: { x: 132, y: 92, w: 1656, h: 760 },
		lateral: { x: 600, y: 0, w: 1320, h: 870 }
	};

	function holePolygon(r) {
		var x2 = r.x + r.w, y2 = r.y + r.h;
		return 'polygon(evenodd, 0 0, 1920px 0, 1920px 1080px, 0 1080px, 0 0, ' +
			r.x + 'px ' + r.y + 'px, ' + r.x + 'px ' + y2 + 'px, ' + x2 + 'px ' + y2 + 'px, ' +
			x2 + 'px ' + r.y + 'px, ' + r.x + 'px ' + r.y + 'px)';
	}

	function badge(url, className) {
		var b = el('div', 'badge ' + className);
		b.appendChild(el('img', '', '', { src: url, alt: '' }));
		return b;
	}

	/* Lignes « Titre | détail » saisies par l'utilisateur (une carte par ligne) */
	function infoCards(text) {
		return str(text).split(/\r?\n/).map(function (line) {
			var parts = line.split('|');
			return { head: str(parts[0]), body: str(parts.slice(1).join('|')) };
		}).filter(function (c) { return c.head || c.body; }).slice(0, 6);
	}

	function buildEventFrame(d) {
		var layout = d.evt === 'lateral' ? 'lateral' : 'cadre';
		var win = WINDOWS[layout];
		var root = el('div', 'evt evt-' + layout);

		var bg = el('div', 'evt-fond');
		bg.style.clipPath = holePolygon(win);
		root.appendChild(bg);

		var border = el('div', 'evt-fenetre');
		border.style.left = win.x + 'px';
		border.style.top = win.y + 'px';
		border.style.width = win.w + 'px';
		border.style.height = win.h + 'px';
		root.appendChild(border);

		var logo = safeLogo(d.logo), logo2 = safeLogo(d.logo2), visual = safeLogo(d.visuel);
		var title = str(d.title), subtitle = str(d.subtitle), label = str(d.label);
		var date = str(d.edate), place = str(d.place);

		if (layout === 'cadre') {
			if (label) root.appendChild(el('div', 'evt-haut', label, { dir: 'auto' }));
			if (logo) root.appendChild(badge(logo, 'gauche'));
			if (logo2) root.appendChild(badge(logo2, 'droite'));
			if (date) {
				var side = el('div', 'evt-cote gauche');
				side.appendChild(el('span', '', date, { dir: 'auto' }));
				root.appendChild(side);
			}
			if (place) {
				var right = el('div', 'evt-cote droite');
				right.appendChild(el('span', 'pin'));
				right.appendChild(el('span', '', place, { dir: 'auto' }));
				root.appendChild(right);
			}
			var bottom = el('div', 'evt-bas' + (visual ? ' avec-visuel' : ''));
			if (visual) root.appendChild(el('img', 'evt-visuel', '', { src: visual, alt: '' }));
			var words = el('div', 'evt-textes');
			if (subtitle) words.appendChild(el('div', 'evt-sous-titre', subtitle, { dir: 'auto' }));
			words.appendChild(el('div', 'evt-titre', title || 'Titre de l’événement', { dir: 'auto' }));
			bottom.appendChild(words);
			root.appendChild(bottom);
			return root;
		}

		/* Panneau latéral + bande basse */
		var panel = el('div', 'evt-panneau');
		if (visual) panel.appendChild(el('img', 'evt-visuel', '', { src: visual, alt: '' }));
		if (label) panel.appendChild(el('div', 'evt-entete', label, { dir: 'auto' }));
		var cards = el('div', 'evt-cartes');
		infoCards(d.fr).forEach(function (c) {
			var card = el('div', 'evt-carte');
			if (c.head) card.appendChild(el('div', 'tete', c.head, { dir: 'auto' }));
			if (c.body) card.appendChild(el('div', 'corps', c.body, { dir: 'auto' }));
			cards.appendChild(card);
		});
		panel.appendChild(cards);
		root.appendChild(panel);

		var band = el('div', 'evt-bande');
		if (date || place) {
			var when = el('div', 'evt-quand');
			if (date) when.appendChild(el('div', 'date', date, { dir: 'auto' }));
			if (place) when.appendChild(el('div', 'lieu', place, { dir: 'auto' }));
			band.appendChild(when);
		}
		var texts = el('div', 'evt-textes');
		if (subtitle) texts.appendChild(el('div', 'evt-sous-titre', subtitle, { dir: 'auto' }));
		texts.appendChild(el('div', 'evt-titre', title || 'Titre de l’événement', { dir: 'auto' }));
		band.appendChild(texts);
		if (logo2 || logo) band.appendChild(badge(logo2 || logo, 'bande-logo'));
		root.appendChild(band);
		return root;
	}

	/* ------------------------------------------------------------------ */
	/* Intro vidéo : logo, titre et décor animés, aux couleurs du thème.    */
	/* Rejouée à chaque fois que la scène qui la contient s'affiche.        */

	function archPath(cx, base, w, h) {
		var x1 = cx - w / 2, x2 = cx + w / 2, spring = base - h * 0.55;
		return 'M' + x1 + ' ' + base + ' L' + x1 + ' ' + spring +
			' C' + x1 + ' ' + (spring - h * 0.3) + ' ' + (cx - w * 0.12) + ' ' + (base - h * 0.85) + ' ' + cx + ' ' + (base - h) +
			' C' + (cx + w * 0.12) + ' ' + (base - h * 0.85) + ' ' + x2 + ' ' + (spring - h * 0.3) + ' ' + x2 + ' ' + spring +
			' L' + x2 + ' ' + base;
	}

	function buildIntro(d) {
		var style = ['etoile', 'arches', 'lumiere'].indexOf(d.istyle) >= 0 ? d.istyle : 'etoile';
		var seconds = Math.max(3, Math.min(30, Number(d.idur) || 8));
		var intro = el('div', 'intro style-' + style);
		intro.style.setProperty('--d', seconds + 's');

		intro.appendChild(buildBackground({ fevt: d.fevt }));
		var art = svg('svg', { viewBox: '0 0 1920 1080', width: '1920', height: '1080', 'class': 'intro-art' });
		var g = svg('g', {}, art);
		if (style === 'etoile') {
			var star = svg('g', { 'class': 'intro-etoile' }, g);
			svg('rect', { x: 760, y: 340, width: 400, height: 400, pathLength: 100 }, star);
			svg('rect', { x: 760, y: 340, width: 400, height: 400, pathLength: 100, transform: 'rotate(45 960 540)' }, star);
			svg('circle', { cx: 960, cy: 540, r: 300, pathLength: 100 }, star);
			svg('circle', { cx: 960, cy: 540, r: 330, pathLength: 100, 'class': 'fin' }, star);
		} else if (style === 'arches') {
			for (var i = 0; i < 6; i++) {
				var arch = svg('path', { d: archPath(960, 900, 520, 640), 'class': 'intro-arche' }, g);
				arch.style.animationDelay = 'calc(var(--d) * ' + (i * 0.05).toFixed(2) + ')';
			}
		} else {
			var rnd = seeded(77);
			for (var p = 0; p < 60; p++) {
				var dot = svg('circle', { cx: (rnd() * 1920).toFixed(0), cy: (900 + rnd() * 300).toFixed(0), r: (1.5 + rnd() * 3.5).toFixed(1), 'class': 'intro-particule' }, g);
				dot.style.animationDelay = (rnd() * 4).toFixed(2) + 's';
				dot.style.animationDuration = (4 + rnd() * 4).toFixed(2) + 's';
			}
			svg('rect', { x: -400, y: 536, width: 400, height: 8, 'class': 'intro-faisceau' }, g);
		}
		intro.appendChild(art);

		var centre = el('div', 'intro-centre');
		var logo = safeLogo(d.logo);
		if (logo) centre.appendChild(el('img', 'intro-logo', '', { src: logo, alt: '' }));
		else {
			var emblem = el('div', 'intro-embleme');
			centre.appendChild(emblem);
		}
		centre.appendChild(el('div', 'intro-filet'));
		if (str(d.ar)) centre.appendChild(el('div', 'intro-ar', str(d.ar), { dir: 'rtl', lang: 'ar' }));
		centre.appendChild(el('div', 'intro-titre', str(d.title) || 'Nom de votre émission', { dir: 'auto' }));
		if (str(d.subtitle)) centre.appendChild(el('div', 'intro-sous-titre', str(d.subtitle), { dir: 'auto' }));
		intro.appendChild(centre);
		intro.appendChild(el('div', 'intro-reflet'));
		return intro;
	}

	/* Réduit un texte sur une ligne jusqu'à ce qu'il tienne en largeur */
	function shrinkToWidth(node, minSize) {
		for (var i = 0; i < 40 && node.scrollWidth > node.clientWidth + 1; i++) {
			var size = parseFloat(getComputedStyle(node).fontSize);
			if (size <= minSize) break;
			node.style.fontSize = (size * 0.93) + 'px';
		}
	}

	/* Réduit un bloc (plusieurs lignes possibles) jusqu'à ce qu'il tienne en hauteur */
	function shrinkToHeight(node, maxHeight, minSize) {
		for (var i = 0; i < 40 && node.scrollHeight > maxHeight + 1; i++) {
			var size = parseFloat(getComputedStyle(node).fontSize);
			if (size <= minSize) break;
			node.style.fontSize = (size * 0.93) + 'px';
		}
	}

	function fitEventFrame(root) {
		root.querySelectorAll('.evt-quand div, .evt-titre, .evt-sous-titre, .evt-haut, .evt-cote span, .evt-carte .corps, .evt-carte .tete').forEach(function (n) {
			shrinkToWidth(n, 14);
		});
		var heading = root.querySelector('.evt-entete');
		if (heading) shrinkToHeight(heading, 180, 24);
		var cards = root.querySelector('.evt-cartes');
		if (cards) shrinkToHeight(cards, cards.clientHeight, 10);
	}

	/* ------------------------------------------------------------------ */
	/* Fond décoratif et cadres des dispositions                           */

	/* ------------------------------------------------------------------ */
	/* Fonds d'événement : décors dessinés par le code (aucune image        */
	/* extérieure), aux couleurs du thème. Aucun texte religieux : seule    */
	/* une salutation saisie (ou proposée puis modifiable) par l'utilisateur. */

	var SVG = 'http://www.w3.org/2000/svg';
	var svgId = 0;

	function svg(tag, attrs, parent) {
		var node = document.createElementNS(SVG, tag);
		Object.keys(attrs || {}).forEach(function (k) { node.setAttribute(k, attrs[k]); });
		if (parent) parent.appendChild(node);
		return node;
	}

	/* Hasard reproductible : le décor est identique à chaque affichage */
	function seeded(seed) {
		var x = seed;
		return function () {
			x = (x * 16807) % 2147483647;
			return (x - 1) / 2147483646;
		};
	}

	function drawStars(g, count, maxY, seed) {
		var rnd = seeded(seed);
		for (var i = 0; i < count; i++) {
			var r = rnd() < 0.85 ? 1 + rnd() * 1.8 : 2.8 + rnd() * 2;
			var star = svg('circle', { cx: (rnd() * 1920).toFixed(1), cy: (rnd() * maxY).toFixed(1), r: r.toFixed(1), 'class': 'twinkle' }, g);
			star.style.animationDelay = (rnd() * 4).toFixed(2) + 's';
			star.style.animationDuration = (2.5 + rnd() * 3).toFixed(2) + 's';
		}
	}

	function drawCrescent(g, cx, cy, r) {
		var id = 'cr' + (svgId++);
		var mask = svg('mask', { id: id }, svg('defs', {}, g));
		svg('rect', { x: cx - r * 2, y: cy - r * 2, width: r * 4, height: r * 4, fill: 'white' }, mask);
		svg('circle', { cx: cx + r * 0.42, cy: cy - r * 0.22, r: r * 0.86, fill: 'black' }, mask);
		svg('circle', { cx: cx, cy: cy, r: r * 1.9, 'class': 'halo' }, g);
		svg('circle', { cx: cx, cy: cy, r: r, 'class': 'astre', mask: 'url(#' + id + ')' }, g);
	}

	function drawStar8(g, cx, cy, r, cls) {
		var pts = [];
		for (var i = 0; i < 16; i++) {
			var a = Math.PI * i / 8 - Math.PI / 2;
			var rr = i % 2 ? r * 0.45 : r;
			pts.push((cx + rr * Math.cos(a)).toFixed(1) + ',' + (cy + rr * Math.sin(a)).toFixed(1));
		}
		return svg('polygon', { points: pts.join(' '), 'class': cls || 'astre' }, g);
	}

	/* Lanterne (fanous) suspendue qui se balance */
	function drawLantern(g, x, rope, size, delay) {
		var l = svg('g', { 'class': 'swing' }, g);
		l.style.transformOrigin = x + 'px 0px';
		l.style.animationDelay = delay + 's';
		var y = rope;
		svg('line', { x1: x, y1: 0, x2: x, y2: y, 'class': 'corde' }, l);
		var w = size, h = size * 1.6;
		svg('circle', { cx: x, cy: y + h * 0.55, r: size * 1.3, 'class': 'lueur' }, l);
		svg('path', { d: 'M' + (x - w * 0.3) + ' ' + (y + h * 0.12) + ' L' + x + ' ' + y + ' L' + (x + w * 0.3) + ' ' + (y + h * 0.12) + ' Z', 'class': 'metal' }, l);
		svg('path', {
			d: 'M' + (x - w * 0.5) + ' ' + (y + h * 0.2) + ' L' + (x + w * 0.5) + ' ' + (y + h * 0.2) + ' L' + (x + w * 0.42) + ' ' + (y + h * 0.78) +
				' L' + (x - w * 0.42) + ' ' + (y + h * 0.78) + ' Z', 'class': 'verre-lanterne'
		}, l);
		svg('rect', { x: x - w * 0.5, y: y + h * 0.12, width: w, height: h * 0.09, rx: 3, 'class': 'metal' }, l);
		svg('rect', { x: x - w * 0.46, y: y + h * 0.76, width: w * 0.92, height: h * 0.08, rx: 3, 'class': 'metal' }, l);
		svg('path', { d: 'M' + (x - w * 0.25) + ' ' + (y + h * 0.84) + ' L' + x + ' ' + (y + h) + ' L' + (x + w * 0.25) + ' ' + (y + h * 0.84) + ' Z', 'class': 'metal' }, l);
		svg('line', { x1: x, y1: y + h * 0.2, x2: x, y2: y + h * 0.78, 'class': 'nervure' }, l);
	}

	/* Silhouette de mosquées : coupoles et minarets */
	function drawSkyline(g, baseY, seed, greenDome) {
		var rnd = seeded(seed);
		var x = -40;
		while (x < 1960) {
			var kind = rnd();
			if (kind < 0.28) {
				var mw = 26 + rnd() * 14, mh = 180 + rnd() * 160;
				svg('rect', { x: x, y: baseY - mh, width: mw, height: mh, 'class': 'ville' }, g);
				svg('rect', { x: x - 6, y: baseY - mh * 0.72, width: mw + 12, height: 10, 'class': 'ville' }, g);
				svg('path', { d: 'M' + (x - 4) + ' ' + (baseY - mh) + ' L' + (x + mw / 2) + ' ' + (baseY - mh - 60) + ' L' + (x + mw + 4) + ' ' + (baseY - mh) + ' Z', 'class': 'ville' }, g);
				x += mw + 30 + rnd() * 40;
			} else {
				var dw = 140 + rnd() * 160, bh = 70 + rnd() * 110;
				svg('rect', { x: x, y: baseY - bh, width: dw, height: bh + 2, 'class': 'ville' }, g);
				var r = dw * 0.38;
				var cx = x + dw / 2, top = baseY - bh;
				var dome = svg('path', {
					d: 'M' + (cx - r) + ' ' + top + ' C' + (cx - r) + ' ' + (top - r * 1.25) + ' ' + (cx - r * 0.15) + ' ' + (top - r * 1.1) + ' ' + cx + ' ' + (top - r * 1.55) +
						' C' + (cx + r * 0.15) + ' ' + (top - r * 1.1) + ' ' + (cx + r) + ' ' + (top - r * 1.25) + ' ' + (cx + r) + ' ' + top + ' Z',
					'class': greenDome && rnd() < 0.5 ? 'coupole-verte' : 'ville'
				}, g);
				dome.setAttribute('data-x', cx);
				x += dw + 10 + rnd() * 50;
			}
		}
		svg('rect', { x: 0, y: baseY, width: 1920, height: 1080 - baseY, 'class': 'ville' }, g);
	}

	function drawRays(g, cx) {
		for (var i = -6; i <= 6; i++) {
			var a = i * 9 * Math.PI / 180;
			var x2 = cx + Math.sin(a) * 1500, y2 = Math.cos(a) * 1500;
			svg('path', { d: 'M' + (cx - 14) + ' -20 L' + (x2 - 70) + ' ' + y2 + ' L' + (x2 + 70) + ' ' + y2 + ' L' + (cx + 14) + ' -20 Z', 'class': 'rayon' }, g)
				.style.animationDelay = (Math.abs(i) * 0.4) + 's';
		}
	}

	function drawBursts(g, seed) {
		var rnd = seeded(seed);
		for (var b = 0; b < 5; b++) {
			var cx = 200 + rnd() * 1520, cy = 120 + rnd() * 360, len = 60 + rnd() * 70;
			var burst = svg('g', { 'class': 'eclat-feu' }, g);
			burst.style.transformOrigin = cx + 'px ' + cy + 'px';
			burst.style.animationDelay = (rnd() * 3).toFixed(2) + 's';
			for (var i = 0; i < 16; i++) {
				var a = Math.PI * 2 * i / 16;
				svg('line', { x1: cx + Math.cos(a) * len * 0.3, y1: cy + Math.sin(a) * len * 0.3, x2: cx + Math.cos(a) * len, y2: cy + Math.sin(a) * len }, burst);
			}
		}
	}

	function drawWaves(g, baseY) {
		for (var i = 0; i < 3; i++) {
			var y = baseY + i * 55, d = 'M -200 ' + y;
			for (var x = -200; x < 2200; x += 160) d += ' q 40 -28 80 0 t 80 0';
			d += ' L 2200 1080 L -200 1080 Z';
			var wave = svg('path', { d: d, 'class': 'vague v' + i }, g);
			wave.style.animationDelay = (-i * 1.3) + 's';
		}
	}

	function drawHills(g, baseY) {
		svg('path', { d: 'M0 ' + baseY + ' C 300 ' + (baseY - 160) + ' 520 ' + (baseY - 60) + ' 760 ' + (baseY - 210) + ' C 980 ' + (baseY - 330) + ' 1150 ' + (baseY - 120) + ' 1400 ' + (baseY - 170) + ' C 1650 ' + (baseY - 220) + ' 1800 ' + (baseY - 90) + ' 1920 ' + (baseY - 120) + ' L1920 1080 L0 1080 Z', 'class': 'ville' }, g);
	}

	function drawKaaba(g, cx, baseY, size) {
		svg('ellipse', { cx: cx, cy: baseY + 6, rx: size * 1.6, ry: size * 0.22, 'class': 'lueur-sol' }, g);
		svg('path', { d: 'M' + (cx - size * 0.62) + ' ' + baseY + ' L' + (cx - size * 0.62) + ' ' + (baseY - size * 0.95) + ' L' + (cx + size * 0.05) + ' ' + (baseY - size * 1.08) + ' L' + (cx + size * 0.7) + ' ' + (baseY - size * 0.95) + ' L' + (cx + size * 0.7) + ' ' + baseY + ' Z', 'class': 'kaaba' }, g);
		svg('line', { x1: cx + size * 0.05, y1: baseY - size * 1.08, x2: cx + size * 0.05, y2: baseY, 'class': 'arete' }, g);
		svg('path', { d: 'M' + (cx - size * 0.62) + ' ' + (baseY - size * 0.72) + ' L' + (cx + size * 0.05) + ' ' + (baseY - size * 0.84) + ' L' + (cx + size * 0.7) + ' ' + (baseY - size * 0.72), 'class': 'bande-or' }, g);
	}

	var EVENT_SCENES = {
		ramadan: function (g) {
			drawStars(g, 140, 760, 11);
			drawCrescent(g, 380, 250, 105);
			[1480, 1660, 1830].forEach(function (x, i) { drawLantern(g, x, 110 + (i % 2) * 120, 70, i * 0.7); });
			drawLantern(g, 120, 160, 60, 1.1);
			drawSkyline(g, 1000, 3, false);
		},
		laylat: function (g) {
			drawRays(g, 960);
			drawStars(g, 260, 1000, 27);
			drawCrescent(g, 1600, 190, 80);
		},
		'aid-fitr': function (g) {
			drawStars(g, 90, 700, 5);
			drawBursts(g, 9);
			drawCrescent(g, 300, 220, 100);
			drawStar8(g, 470, 170, 30, 'astre scintille');
			drawSkyline(g, 1010, 8, false);
		},
		'aid-adha': function (g) {
			drawStars(g, 80, 600, 13);
			drawCrescent(g, 1620, 200, 90);
			drawBursts(g, 21);
			drawHills(g, 1080);
		},
		hajj: function (g) {
			drawStars(g, 120, 640, 17);
			drawRays(g, 960);
			drawKaaba(g, 960, 960, 260);
			svg('rect', { x: 0, y: 960, width: 1920, height: 120, 'class': 'ville' }, g);
		},
		hegire: function (g) {
			drawStars(g, 200, 1000, 31);
			drawStar8(g, 960, 640, 330, 'etoile-fond');
			drawCrescent(g, 960, 640, 150);
		},
		achoura: function (g) {
			drawStars(g, 120, 700, 41);
			drawCrescent(g, 1500, 220, 90);
			drawWaves(g, 860);
		},
		maouloud: function (g) {
			drawStars(g, 120, 700, 7);
			drawRays(g, 960);
			drawSkyline(g, 1000, 12, true);
		},
		isra: function (g) {
			drawStars(g, 300, 1000, 53);
			svg('path', { d: 'M -100 1000 C 500 820 1100 700 1500 260 S 1900 -40 2100 -100', 'class': 'chemin-lumiere' }, g);
			drawCrescent(g, 1640, 200, 70);
		},
		jumua: function (g) {
			drawRays(g, 1500);
			drawSkyline(g, 990, 19, false);
		}
	};

	function buildBackground(d) {
		var event = typeof d.fevt === 'string' && EVENT_SCENES[d.fevt] ? d.fevt : '';
		var wrap = el('div', 'fond' + (event ? ' fond-evt fond-' + event : ''));
		if (event) {
			var art = svg('svg', { viewBox: '0 0 1920 1080', width: '1920', height: '1080', 'class': 'decor' });
			EVENT_SCENES[event](svg('g', {}, art));
			wrap.appendChild(art);
		}
		/* Salutation facultative, saisie ou modifiée par l'utilisateur */
		if (d.greet && (str(d.title) || str(d.ar))) {
			var greet = el('div', 'salutation');
			if (str(d.ar)) greet.appendChild(el('div', 'salut-ar', str(d.ar), { dir: 'rtl', lang: 'ar' }));
			if (str(d.title)) greet.appendChild(el('div', 'salut-fr', str(d.title), { dir: 'auto' }));
			wrap.appendChild(greet);
		}
		return wrap;
	}

	function buildFrames(d) {
		var wrap = el('div', 'cadres');
		var style = ['aucune', 'fine', 'epaisse', 'ornee'].indexOf(d.frame) >= 0 ? d.frame : 'fine';
		var frames = Array.isArray(d.frames) ? d.frames.slice(0, 9) : [];
		frames.forEach(function (f) {
			var x = Number(f.x), y = Number(f.y), w = Number(f.w), h = Number(f.h);
			if (!(w > 0 && h > 0)) return;
			var box = el('div', 'cadre style-' + style);
			box.style.left = x + 'px';
			box.style.top = y + 'px';
			box.style.width = w + 'px';
			box.style.height = h + 'px';
			if (style === 'ornee') {
				['a1', 'a2', 'a3', 'a4'].forEach(function (a) { box.appendChild(el('div', 'angle ' + a)); });
			}
			if (str(f.label)) {
				box.appendChild(el('div', 'nom', str(f.label), { dir: 'auto' }));
			}
			wrap.appendChild(box);
		});
		return wrap;
	}

	/* ------------------------------------------------------------------ */
	/* Textes longs : le panneau ne doit jamais cacher les intervenants.     */
	/* 1) panneau élargi et texte un peu plus petit ;                       */
	/* 2) si c'est encore trop haut, le texte est découpé en pages qui      */
	/*    défilent (1/3, 2/3…). Le texte reste complet et inchangé.          */

	var LONG_TEXT = 220;
	var MAX_PAGES = 15;

	/* Découpe en n morceaux de longueurs proches, sans couper les mots */
	function splitWords(text, n) {
		var words = text.split(/\s+/).filter(Boolean);
		if (n <= 1 || words.length <= 1) return [text];
		var total = text.length;
		var chunks = [];
		var current = [];
		var length = 0;
		words.forEach(function (w) {
			var target = total * (chunks.length + 1) / n;
			if (current.length && chunks.length < n - 1 && length + w.length / 2 > target) {
				chunks.push(current.join(' '));
				current = [];
			}
			current.push(w);
			length += w.length + 1;
		});
		chunks.push(current.join(' '));
		while (chunks.length < n) chunks.push('');
		return chunks;
	}

	function currentScale() {
		return Number(getComputedStyle(document.documentElement).getPropertyValue('--scale')) || 1;
	}

	function buildPanelPages(d, stage) {
		var ar = str(d.ar);
		var fr = str(d.fr);
		var ratio = Math.max(25, Math.min(90, Number(d.maxh) || 40)) / 100;
		var limit = 1080 * ratio;
		var large = ar.length + fr.length > LONG_TEXT;

		function make(n, i, arParts, frParts) {
			var card = buildPanel(d, { ar: arParts[i], fr: frParts[i], index: i + 1, total: n });
			if (large) card.classList.add('large');
			return card;
		}

		for (var n = 1; n <= MAX_PAGES; n++) {
			var arParts = splitWords(ar, n);
			var frParts = splitWords(fr, n);
			var pages = [];
			var fits = true;
			for (var i = 0; i < n; i++) {
				var card = make(n, i, arParts, frParts);
				card.style.visibility = 'hidden';
				stage.appendChild(card);
				if (card.offsetHeight * currentScale() > limit) fits = false;
				stage.removeChild(card);
				card.style.visibility = '';
				pages.push(card);
				if (!fits) break;
			}
			if (fits) return pages;
		}

		/* Cas extrême (mot unique gigantesque) : réduire jusqu'à tenir */
		var last = make(1, 0, [ar], [fr]);
		stage.appendChild(last);
		var h = last.offsetHeight * currentScale();
		stage.removeChild(last);
		if (h > limit) last.style.scale = String(Math.max(0.3, limit / h) * currentScale());
		return [last];
	}

	var pageTimer = null;

	/* ------------------------------------------------------------------ */
	/* Transition Amal (jouée sur ordre d'Amal Studio)                      */

	function buildTransition(d) {
		var style = ['volet', 'rideau', 'logo', 'eclats', 'rubans', 'losange'].indexOf(d.style) >= 0 ? d.style : 'volet';
		var tr = el('div', 'tr style-' + style);
		var duration = Math.max(400, Math.min(6000, Number(d.dur) || 1400));
		tr.style.setProperty('--dur', duration + 'ms');

		/* L'écran est entièrement couvert de 41 % à 59 % de la durée : la scène change à 50 % */
		if (style === 'rideau') {
			tr.appendChild(el('div', 'panneau haut'));
			tr.appendChild(el('div', 'panneau bas'));
		} else if (style === 'eclats') {
			for (var i = 0; i < 5; i++) {
				var band = el('div', 'panneau eclat e' + i);
				band.style.setProperty('--i', String(i));
				tr.appendChild(band);
			}
		} else if (style === 'rubans') {
			for (var j = 0; j < 8; j++) {
				var ribbon = el('div', 'panneau ruban' + (j % 2 ? ' impair' : ''));
				ribbon.style.setProperty('--i', String(j));
				ribbon.style.left = (j * 12.5) + '%';
				tr.appendChild(ribbon);
			}
		} else {
			tr.appendChild(el('div', 'panneau'));
		}
		tr.appendChild(el('div', 'eclair'));

		var centre = el('div', 'centre');
		var logo = safeLogo(d.logo);
		if (logo) {
			centre.appendChild(el('img', '', '', { src: logo, alt: '' }));
		} else {
			centre.appendChild(el('div', 'etoile'));
		}
		if (str(d.fr)) {
			centre.appendChild(el('div', 'texte', str(d.fr), { dir: 'auto' }));
		}
		tr.appendChild(centre);
		return tr;
	}

	function playTransition() {
		var tr = document.querySelector('.tr');
		if (!tr) return;
		tr.classList.remove('play');
		void tr.offsetWidth; /* relance l'animation depuis le début */
		tr.classList.add('play');
	}

	/* Amal Studio envoie cet évènement au début de chaque transition */
	window.addEventListener('amalTransition', playTransition);

	/* La mise en page est prévue pour 1920 x 1080 : on l'adapte à la taille réelle */
	function fitToCanvas() {
		var zoom = window.innerWidth / 1920;
		document.documentElement.style.zoom = zoom > 0 ? String(zoom) : '1';
	}

	function render() {
		fitToCanvas();
		var d = readData();
		var stage = document.getElementById('stage');
		var root = document.documentElement.style;

		if (/^#[0-9a-fA-F]{6}$/.test(d.primary || '')) root.setProperty('--primary', d.primary);
		if (/^#[0-9a-fA-F]{6}$/.test(d.accent || '')) root.setProperty('--accent', d.accent);
		if (/^#[0-9a-fA-F]{6}$/.test(d.text || '')) root.setProperty('--ink', d.text);
		/* Style d'habillage et motif, choisis dans le Studio graphique */
		var look = ['classique', 'moderne', 'epure', 'verre'].indexOf(d.look) >= 0 ? d.look : 'classique';
		var html = document.documentElement;
		html.className = 'look-' + look + (d.motif === false ? ' sans-motif' : '');

		var scale = Number(d.scale);
		if (scale >= 0.3 && scale <= 2) root.setProperty('--scale', String(scale));

		var template = d.t || 'verset';
		var position = ['bas', 'haut', 'centre'].indexOf(d.pos) >= 0 ? d.pos : 'bas';
		if (template === 'titre' && !d.pos) position = 'centre';
		var animation = ['glisser', 'fondu', 'aucune'].indexOf(d.anim) >= 0 ? d.anim : 'glisser';

		stage.className = 'tpl-' + template + ' pos-' + position;
		stage.textContent = '';
		if (clockTimer) {
			window.clearInterval(clockTimer);
			clockTimer = null;
		}

		if (pageTimer) {
			window.clearInterval(pageTimer);
			pageTimer = null;
		}

		var card;
		var pages = null;
		if (template === 'intervenant') card = buildSpeaker(d);
		else if (template === 'titre') card = buildTitle(d);
		else if (template === 'bandeau') card = buildTicker(d);
		else if (template === 'direct') card = buildBroadcast(d);
		else if (template === 'fond') card = buildBackground(d);
		else if (template === 'cadres') card = buildFrames(d);
		else if (template === 'transition') card = buildTransition(d);
		else if (template === 'evenement') card = buildEventFrame(d);
		else if (template === 'intro') card = buildIntro(d);
		else {
			pages = buildPanelPages(d, stage);
			card = pages[0];
		}

		/* Le fond et les cadres sont des éléments fixes : ni animation, ni durée */
		if (template === 'fond' || template === 'cadres' || template === 'transition') {
			stage.appendChild(card);
			return;
		}
		if (template === 'intro') {
			stage.appendChild(card);
			card.querySelectorAll('.intro-titre, .intro-sous-titre').forEach(function (n) { shrinkToWidth(n, 20); });
			return;
		}
		if (template === 'evenement') {
			stage.appendChild(card);
			fitEventFrame(card);
			card.classList.add('evt-entree');
			return;
		}

		card.classList.add('anim-' + animation, 'enter');
		stage.appendChild(card);

		if (template === 'bandeau' || template === 'direct') {
			applyTicker(card, d);
		}

		/* Texte en plusieurs pages : elles défilent automatiquement */
		if (pages && pages.length > 1) {
			var pageIndex = 0;
			var pageSeconds = Math.max(3, Math.min(60, Number(d.page) || 8));
			pageTimer = window.setInterval(function () {
				pageIndex = (pageIndex + 1) % pages.length;
				var next = pages[pageIndex];
				next.classList.remove('enter', 'leave', 'page-in');
				next.classList.add('anim-' + animation);
				void next.offsetWidth;
				next.classList.add('page-in');
				stage.replaceChild(next, card);
				card = next;
			}, pageSeconds * 1000);
		}

		/* Durée d'affichage (secondes) : 0 = reste affiché */
		var duration = Number(d.dur) || 0;
		if (duration > 0) {
			window.setTimeout(function () {
				if (pageTimer) {
					window.clearInterval(pageTimer);
					pageTimer = null;
				}
				card.classList.remove('enter', 'page-in');
				card.classList.add('leave');
			}, duration * 1000);
		}
	}

	/* Attendre les polices pour éviter un affichage arabe provisoire */
	if (document.fonts && document.fonts.load) {
		Promise.all([
			document.fonts.load('66px "Amiri Quran"', 'بسم'),
			document.fonts.load('400 62px Amiri', 'بسم'),
			document.fonts.load('700 62px Amiri', 'بسم')
		]).then(render, render);
	} else {
		render();
	}
	window.addEventListener('hashchange', render);
	window.addEventListener('resize', fitToCanvas);
})();
