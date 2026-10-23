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

		var card;
		if (template === 'intervenant') card = buildSpeaker(d);
		else if (template === 'titre') card = buildTitle(d);
		else if (template === 'bandeau') card = buildTicker(d);
		else card = buildPanel(d);

		card.classList.add('anim-' + animation, 'enter');
		stage.appendChild(card);

		if (template === 'bandeau') {
			applyTicker(card, d);
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
