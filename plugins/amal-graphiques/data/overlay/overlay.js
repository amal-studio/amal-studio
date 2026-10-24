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
	/* Fond décoratif et cadres des dispositions                           */

	function buildBackground() {
		return el('div', 'fond');
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
		var style = ['volet', 'rideau', 'logo'].indexOf(d.style) >= 0 ? d.style : 'volet';
		var tr = el('div', 'tr style-' + style);
		var duration = Math.max(400, Math.min(6000, Number(d.dur) || 1400));
		tr.style.setProperty('--dur', duration + 'ms');

		if (style === 'rideau') {
			tr.appendChild(el('div', 'panneau haut'));
			tr.appendChild(el('div', 'panneau bas'));
		} else {
			tr.appendChild(el('div', 'panneau'));
		}

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
		else {
			pages = buildPanelPages(d, stage);
			card = pages[0];
		}

		/* Le fond et les cadres sont des éléments fixes : ni animation, ni durée */
		if (template === 'fond' || template === 'cadres' || template === 'transition') {
			stage.appendChild(card);
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
