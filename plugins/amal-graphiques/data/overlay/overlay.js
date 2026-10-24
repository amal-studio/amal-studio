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
	function buildPanel(d) {
		var card = el('div', 'card panel');
		var label = str(d.label) || LABELS[d.t] || '';
		var ar = str(d.ar);
		var fr = str(d.fr);
		var ref = str(d.ref);

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

	function fitPanel(card) {
		var arabicText = card.querySelector('.arabic');
		var translation = card.querySelector('.translation');
		var limit = window.innerHeight * 0.9;
		for (var i = 0; i < 40 && card.getBoundingClientRect().height > limit; i++) {
			if (arabicText) {
				var size = parseFloat(getComputedStyle(arabicText).fontSize);
				if (size <= 26) break;
				arabicText.style.fontSize = (size * 0.92) + 'px';
			}
			if (translation) {
				var tsize = parseFloat(getComputedStyle(translation).fontSize);
				if (tsize > 20) translation.style.fontSize = (tsize * 0.94) + 'px';
			}
		}
		/* Dans tous les cas, ne jamais déborder de l'image */
		var rect = card.getBoundingClientRect();
		if (rect.height > limit) {
			card.style.scale = String(Math.max(0.3, limit / rect.height) * (Number(getComputedStyle(document.documentElement).getPropertyValue('--scale')) || 1));
		}
	}

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

		var card;
		if (template === 'intervenant') card = buildSpeaker(d);
		else if (template === 'titre') card = buildTitle(d);
		else if (template === 'bandeau') card = buildTicker(d);
		else if (template === 'direct') card = buildBroadcast(d);
		else if (template === 'fond') card = buildBackground(d);
		else if (template === 'cadres') card = buildFrames(d);
		else card = buildPanel(d);

		/* Le fond et les cadres sont des éléments fixes : ni animation, ni durée */
		if (template === 'fond' || template === 'cadres') {
			stage.appendChild(card);
			return;
		}

		card.classList.add('anim-' + animation, 'enter');
		stage.appendChild(card);

		if (template === 'bandeau' || template === 'direct') {
			applyTicker(card, d);
		}

		/* Longs versets : réduire le texte jusqu'à ce que le panneau tienne dans l'image */
		if (card.classList.contains('panel')) {
			fitPanel(card);
		}

		/* Durée d'affichage (secondes) : 0 = reste affiché */
		var duration = Number(d.dur) || 0;
		if (duration > 0) {
			window.setTimeout(function () {
				card.classList.remove('enter');
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
