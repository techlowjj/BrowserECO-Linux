/*
 * Chargement d'images a la demande — partie « apparence et clic ».
 *
 * ATTENTION : ce script ne fait PAS l'économie de données. Il ne bloque rien.
 *
 * L'économie est faite au niveau RESEAU, par EcoInterceptor, qui bloque les
 * images tant que l'utilisateur ne les a pas demandees. C'est volontaire :
 *   - un script DOM arrive TROP TARD pour les images du HTML initial (le
 *     parseur a deja lance les requetes) ;
 *   - neutraliser QWebEngineSettings::AutoLoadImages est un impasse : avec ce
 *     reglage off, Chromium refuse tout chargement declenche par script
 *     (mesure : restaurer src puis appeler decode() ne charge rien) et un clic
 *     ne recharge pas une image dont la src a ete remplacee.
 *
 * Ce script fait donc le reste :
 *   - decocher les images en attente (bordure pointillee, curseur) ;
 *   - au clic, re-mettre la src AVEC le marqueur `_eco` que l'intercepteur
 *     reconnait, retire de l'URL, et laisse enfin passer la requete ;
 *   - tenir un compteur pour le panneau Eco.
 *
 * Monde applicatif : la page ne peut ni voir ce script ni le desactiver.
 */
(function() {
    'use strict';

    var ATTR = 'data-browsereco-src';      // src d'origine, pour le clic
    var STYLE_ATTR = 'data-browsereco-style';
    var MARKER = '_eco';
    var STYLES = ['cursor', 'border', 'minWidth', 'minHeight'];

    var etat = window.__browserecoLazy;
    if (etat && etat.installed) return;
    etat = window.__browserecoLazy = { count: 0, installed: true };

    function saveStyles(img) {
        var saved = {};
        for (var i = 0; i < STYLES.length; i++) saved[STYLES[i]] = img.style[STYLES[i]] || '';
        img.setAttribute(STYLE_ATTR, JSON.stringify(saved));
    }

    function restoreStyles(img) {
        var raw = img.getAttribute(STYLE_ATTR);
        if (raw) {
            try {
                var saved = JSON.parse(raw);
                for (var i = 0; i < STYLES.length; i++) img.style[STYLES[i]] = saved[STYLES[i]];
            } catch (e) { /* donnees corrompues : on ne touche a rien */ }
            img.removeAttribute(STYLE_ATTR);
        }
    }

    function mark(url) {
        return url + (url.indexOf('?') >= 0 ? '&' : '?') + MARKER + '=1';
    }

    function watchImage(img) {
        if (!img || img.nodeName !== 'IMG') return false;
        if (img.hasAttribute(ATTR)) return false;

        var src = img.getAttribute('src');
        // Rien a differer : pas d'URL reseau (data:/blob:/absent).
        if (!src || src.indexOf('http://') !== 0 && src.indexOf('https://') !== 0) return false;
        // Deja demandee par l'utilisateur (marqueur pose par un clic anterieur).
        if (src.indexOf(MARKER + '=') >= 0) return false;

        img.setAttribute(ATTR, src);
        saveStyles(img);
        img.style.cursor = 'pointer';
        img.style.border = '1px dashed #888';
        img.style.minWidth = '16px';
        img.style.minHeight = '16px';
        img.title = "Cliquez pour charger l'image (economie de donnees)";
        etat.count++;

        img.addEventListener('click', function onClick(ev) {
            ev.preventDefault();
            ev.stopPropagation();
            var url = img.getAttribute(ATTR);
            if (!url) return;
            // Le marqueur fait passer la requete cote intercepteur.
            img.setAttribute('src', mark(url));
            img.removeAttribute(ATTR);
            restoreStyles(img);
            img.title = '';
            etat.count = Math.max(0, etat.count - 1);
            img.removeEventListener('click', onClick);
        }, true);
        return true;
    }

    function watchAll(root) {
        var imgs = root.querySelectorAll('img');
        for (var i = 0; i < imgs.length; i++) watchImage(imgs[i]);
    }

    // Images deja presentes (le script est injecte apres coup ici : le blocage
    // reseau, lui, a deja joue pour celles du document initial).
    watchAll(document);

    // Images ajoutees ensuite (SPA, scroll infini).
    if (window.MutationObserver) {
        var observer = new MutationObserver(function(mutations) {
            for (var m = 0; m < mutations.length; m++) {
                var added = mutations[m].addedNodes;
                for (var n = 0; n < added.length; n++) {
                    var node = added[n];
                    if (!node || node.nodeType !== 1) continue;
                    if (node.nodeName === 'IMG') watchImage(node);
                    else if (node.querySelectorAll) watchAll(node);
                }
            }
        });
        observer.observe(document.documentElement || document,
                         { childList: true, subtree: true });
    }
})();