#!/usr/bin/env node
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Genera manual.es.txt, manual.en.txt y manual.html (single-file
// interactivo bilingüe) a partir de manual.json.
//
// Uso:
//   node build_manual.mjs            # genera todo
//   node build_manual.mjs --txt-only
//   node build_manual.mjs --html-only

import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const data = JSON.parse(readFileSync(join(__dirname, 'manual.json'), 'utf8'));

// ============================================================
// TXT (80 cols, ASCII)
// ============================================================
function wrap(str, w=80) {
  const out = [];
  for (const para of str.split('\n')) {
    if (para.length <= w) { out.push(para); continue; }
    let line = '';
    for (const word of para.split(' ')) {
      if ((line + word).length + 1 > w) {
        out.push(line.trimEnd()); line = word + ' ';
      } else line += word + ' ';
    }
    if (line.trim()) out.push(line.trimEnd());
  }
  return out.join('\n');
}

function renderTxtSection(s, lang, depth=0) {
  const lines = [];
  const title = s.title[lang];
  if (depth === 0) {
    lines.push('');
    lines.push('='.repeat(80));
    lines.push(title);
    lines.push('='.repeat(80));
  } else {
    lines.push('');
    lines.push('-'.repeat(80));
    lines.push('  '.repeat(depth-1) + title);
    lines.push('-'.repeat(80));
  }
  lines.push('');
  lines.push(wrap(s.body[lang]));

  for (const c of (s.code || [])) {
    lines.push('');
    lines.push(c.label ? `[${c.label}]` : `[${c.lang}]`);
    const lines2 = c.src.split('\n');
    const prefix = c.terminal ? (c.prompt || '$ ') : '    ';
    for (const l of lines2) lines.push(prefix + l);
  }

  for (const sh of (s.screenshots || [])) {
    lines.push('');
    lines.push(`[fig] ${sh.src}`);
    lines.push('  ' + wrap(sh.caption[lang], 76).replace(/\n/g,'\n  '));
  }

  for (const t of (s.tips || [])) {
    lines.push('');
    lines.push(`! ${(t.kind||'tip').toUpperCase()}: ${wrap(t[lang],76).replace(/\n/g,'\n  ')}`);
  }

  for (const sub of (s.subsections || [])) {
    lines.push(...renderTxtSection(sub, lang, depth+1));
  }

  return lines;
}

function renderTxt(lang) {
  const m = data.meta;
  const out = [];
  out.push('');
  out.push('+'+'='.repeat(78)+'+');
  out.push('|' + ` ${m.app_name.toUpperCase()} — Manual de Usuario  v${m.version}`.padEnd(78) + '|');
  out.push('+'+'='.repeat(78)+'+');
  out.push('');
  out.push(m.tagline[lang]);
  out.push('');
  out.push(`Autor: ${m.author}`);
  out.push(`Licencia: ${m.license}  ·  Comercial: ${m.commercial_contact}`);
  out.push('');

  out.push('-'.repeat(80));
  out.push(lang==='es' ? 'ÍNDICE' : 'TABLE OF CONTENTS');
  out.push('-'.repeat(80));
  for (const s of data.sections) {
    out.push('  ' + s.title[lang]);
    for (const sub of (s.subsections || [])) {
      out.push('    ' + sub.title[lang]);
    }
  }

  for (const s of data.sections) {
    out.push(...renderTxtSection(s, lang));
  }

  // Glossary
  out.push('');
  out.push('='.repeat(80));
  out.push(lang==='es' ? 'GLOSARIO COMPLETO' : 'FULL GLOSSARY');
  out.push('='.repeat(80));
  for (const g of data.glossary) {
    out.push('');
    out.push(`  ${g.term[lang]}`);
    out.push('  ' + wrap(g.def[lang], 76).replace(/\n/g, '\n  '));
  }

  out.push('');
  out.push('-'.repeat(80));
  out.push(`Generado por ${m.generator_version} el ${m.generated_at}  ·  git ${m.git_sha}`);
  out.push('-'.repeat(80));

  return out.join('\n') + '\n';
}

// ============================================================
// HTML (single-file, dark+amber, sidebar, MiniSearch substring,
// glossary tooltips, frame strips, lang/theme toggle, copy code)
// ============================================================
function escHtml(s) {
  return String(s).replace(/[&<>"']/g, c => ({
    '&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'
  })[c]);
}

// Tiny markdown subset: **bold**, *italic*, `code`, lists, paragraphs,
// pipe tables. Used inside body fields.
function md(src) {
  if (!src) return '';
  // Pipe tables: detect lines with | at start/end of line group
  const blocks = src.split(/\n\n+/);
  return blocks.map(block => {
    block = block.trim();
    if (!block) return '';
    // Table
    if (/^\|.*\|/.test(block) && /\n\|[-\s|:]+\|/.test(block)) {
      const lines = block.split('\n').filter(Boolean);
      const hdr = lines[0].split('|').slice(1,-1).map(s=>s.trim());
      const rows = lines.slice(2).map(l => l.split('|').slice(1,-1).map(s=>s.trim()));
      let h = '<table><thead><tr>' + hdr.map(c=>`<th>${inline(c)}</th>`).join('') + '</tr></thead><tbody>';
      for (const r of rows) h += '<tr>' + r.map(c=>`<td>${inline(c)}</td>`).join('') + '</tr>';
      h += '</tbody></table>';
      return h;
    }
    // List
    if (/^[-*]\s/.test(block)) {
      const items = block.split('\n').map(l => l.replace(/^[-*]\s+/,''));
      return '<ul>' + items.map(i=>`<li>${inline(i)}</li>`).join('') + '</ul>';
    }
    if (/^\d+\.\s/.test(block)) {
      const items = block.split('\n').map(l => l.replace(/^\d+\.\s+/,''));
      return '<ol>' + items.map(i=>`<li>${inline(i)}</li>`).join('') + '</ol>';
    }
    // Code block (triple backtick)
    if (/^```/.test(block)) {
      const lines = block.split('\n');
      const lang = lines[0].replace(/^```/,'');
      const code = lines.slice(1, -1).join('\n');
      return `<pre><code data-lang="${escHtml(lang)}">${escHtml(code)}</code></pre>`;
    }
    return `<p>${inline(block.replace(/\n/g,' '))}</p>`;
  }).join('\n');
}
function inline(s) {
  s = escHtml(s);
  s = s.replace(/\*\*([^*]+)\*\*/g,'<strong>$1</strong>');
  s = s.replace(/\*([^*]+)\*/g,'<em>$1</em>');
  s = s.replace(/`([^`]+)`/g,'<code>$1</code>');
  s = s.replace(/\[([^\]]+)\]\(([^)]+)\)/g,'<a href="$2" rel="noopener">$1</a>');
  return s;
}

function renderHtmlSection(s, lang, depth=0) {
  const tag = depth === 0 ? 'h2' : depth === 1 ? 'h3' : 'h4';
  let h = `<section id="${escHtml(s.id)}" data-section="${depth}">`;
  h += `<${tag}>${escHtml(s.title[lang])}</${tag}>`;
  h += `<div class="body">${md(s.body[lang])}</div>`;

  for (const c of (s.code || [])) {
    const cls = c.terminal ? 'terminal' : '';
    const label = c.label ? `<div class="code-label">${escHtml(c.label)}</div>` : '';
    const prompt = c.terminal ? (c.prompt || '$ ') : '';
    h += `<div class="code-block">${label}<pre class="${cls}"><code data-lang="${escHtml(c.lang)}">`;
    if (c.terminal) {
      for (const l of c.src.split('\n')) {
        h += `<span class="prompt">${escHtml(prompt)}</span><span class="cmd">${escHtml(l)}</span>\n`;
      }
    } else h += escHtml(c.src);
    h += `</code><button class="copy-btn" aria-label="Copy">Copiar</button></pre></div>`;
  }

  for (const sh of (s.screenshots || [])) {
    h += `<figure class="screenshot">`;
    h += `<img src="${escHtml(sh.src)}" alt="${escHtml(sh.alt[lang])}" loading="lazy">`;
    h += `<figcaption>${escHtml(sh.caption[lang])}</figcaption>`;
    h += `</figure>`;
  }

  for (const m of (s.media || [])) {
    if (m.type === 'frames') {
      h += `<figure class="frame-strip"><div class="strip-track">`;
      for (const fr of m.frames) {
        h += `<figure class="frame"><img src="${escHtml(fr.src)}" alt="${escHtml(fr.label[lang])}"><figcaption>${escHtml(fr.label[lang])}</figcaption></figure>`;
      }
      h += `</div><button class="strip-prev" aria-label="◀">◀</button><button class="strip-next" aria-label="▶">▶</button>`;
      if (m.caption) h += `<figcaption class="strip-caption">${escHtml(m.caption[lang])}</figcaption>`;
      h += `</figure>`;
    }
  }

  for (const t of (s.tips || [])) {
    h += `<aside class="tip tip-${escHtml(t.kind||'info')}"><strong>${(t.kind||'info').toUpperCase()}:</strong> ${escHtml(t[lang])}</aside>`;
  }

  for (const sub of (s.subsections || [])) {
    h += renderHtmlSection(sub, lang, depth+1);
  }
  h += `</section>`;
  return h;
}

function renderToc(lang) {
  let h = `<nav role="navigation" aria-label="${lang==='es'?'Tabla de contenidos':'Table of contents'}"><ol class="toc-root">`;
  for (const s of data.sections) {
    h += `<li><a href="#${escHtml(s.id)}" data-id="${escHtml(s.id)}">${escHtml(s.title[lang])}</a>`;
    if (s.subsections && s.subsections.length) {
      h += `<ol class="toc-sub">`;
      for (const sub of s.subsections) {
        h += `<li><a href="#${escHtml(sub.id)}" data-id="${escHtml(sub.id)}">${escHtml(sub.title[lang])}</a></li>`;
      }
      h += `</ol>`;
    }
    h += `</li>`;
  }
  h += `</ol></nav>`;
  return h;
}

function renderGlossary(lang) {
  let h = `<section id="glossary"><h2>${lang==='es'?'14. Glosario':'14. Glossary'}</h2><dl>`;
  for (const g of data.glossary) {
    h += `<dt id="g-${escHtml(g.term.en.toLowerCase().replace(/[^a-z0-9]+/g,'-'))}">${escHtml(g.term[lang])}</dt>`;
    h += `<dd>${md(g.def[lang])}</dd>`;
  }
  h += `</dl></section>`;
  return h;
}

function renderHtml() {
  const m = data.meta;
  const cssVars = Object.entries(m.theme_vars||{}).map(([k,v])=>`${k}:${v};`).join('');

  const css = `
:root {${cssVars} --radius:10px; --font-sans:"Inter",system-ui,-apple-system,Segoe UI,sans-serif; --font-mono:"JetBrains Mono","Fira Code",Consolas,monospace; --font-serif:"Cambria",Georgia,serif; }
html[data-theme="light"] { --bg:#f6f3eb; --bg-2:#e8e2cc; --fg:#1a1814; --fg-2:#5a564a; --border:#c8c0aa; }
* { box-sizing:border-box; }
html,body { margin:0; padding:0; }
body { background:var(--bg); color:var(--fg); font-family:var(--font-sans); line-height:1.6; font-size:15.5px; -webkit-font-smoothing:antialiased; }
a { color:var(--accent); text-decoration:none; border-bottom:1px solid transparent; }
a:hover, a:focus-visible { border-bottom-color:var(--accent); }
:focus-visible { outline:2px solid var(--accent); outline-offset:2px; border-radius:4px; }
@media (prefers-reduced-motion: reduce) { * { animation:none !important; transition:none !important; scroll-behavior:auto !important; }}

.topbar { position:sticky; top:0; z-index:20; background:var(--bg-2); border-bottom:1px solid var(--border); padding:10px 16px; display:flex; align-items:center; gap:12px; backdrop-filter:blur(8px); }
.topbar .brand { font-family:var(--font-serif); font-size:22px; font-weight:700; color:var(--accent); }
.topbar .version { color:var(--fg-2); font-size:12px; font-family:var(--font-mono); }
.topbar .version .sha { opacity:0.7; }
.topbar .search { flex:1; max-width:480px; margin-left:auto; }
.topbar .search input { width:100%; background:var(--bg); border:1px solid var(--border); border-radius:8px; padding:8px 12px; color:var(--fg); font-family:inherit; font-size:14px; }
.topbar button { background:transparent; color:var(--fg); border:1px solid var(--border); border-radius:8px; padding:7px 12px; cursor:pointer; font-size:13px; font-family:inherit; }
.topbar button:hover { background:var(--bg); border-color:var(--accent); }

.app { display:grid; grid-template-columns:280px 1fr; max-width:1400px; margin:0 auto; }
.sidebar { position:sticky; top:56px; height:calc(100vh - 56px); overflow-y:auto; border-right:1px solid var(--border); padding:16px; background:var(--bg-2); }
.sidebar .toc-root { list-style:none; padding-left:0; margin:0; }
.sidebar .toc-root > li { margin-bottom:6px; }
.sidebar .toc-root > li > a { font-weight:600; color:var(--fg); }
.sidebar .toc-sub { list-style:none; padding-left:18px; margin-top:4px; }
.sidebar .toc-sub a { color:var(--fg-2); font-size:13px; }
.sidebar a[aria-current="location"] { color:var(--accent); }
.sidebar a { display:block; padding:4px 8px; border-radius:6px; border-bottom:0; }
.sidebar a:hover { background:var(--bg); }

main { padding:32px 48px; max-width:920px; }
main section { margin-bottom:48px; padding-top:8px; }
main section h2 { font-family:var(--font-serif); font-size:30px; color:var(--accent); border-bottom:2px solid var(--border); padding-bottom:8px; margin-top:0; }
main section h3 { font-size:22px; color:var(--accent-2); margin-top:32px; }
main section h4 { font-size:18px; color:var(--fg); margin-top:20px; }
main .body p { margin:0 0 14px; }
main .body strong { color:var(--fg); }
main code { background:var(--bg-2); padding:2px 6px; border-radius:4px; font-family:var(--font-mono); font-size:0.92em; }

table { border-collapse:collapse; margin:14px 0; width:100%; font-size:14px; }
th, td { border:1px solid var(--border); padding:8px 12px; text-align:left; }
th { background:var(--bg-2); color:var(--accent-2); }

.code-block { margin:14px 0; }
.code-label { color:var(--fg-2); font-size:12px; font-family:var(--font-mono); margin-bottom:4px; }
pre { background:#0a0e1a; color:#d6e1f5; border:1px solid #1a2236; border-radius:var(--radius); padding:14px 16px; font-family:var(--font-mono); font-size:13.5px; line-height:1.55; overflow-x:auto; position:relative; }
pre.terminal .prompt { color:#4cc9f0; font-weight:600; user-select:none; margin-right:6px; }
pre.terminal .cmd { color:#f5b400; }
pre .copy-btn { position:absolute; top:8px; right:8px; background:rgba(255,255,255,.08); color:#d6e1f5; border:1px solid #2a3550; padding:4px 8px; border-radius:6px; cursor:pointer; font-size:11px; opacity:0; transition:opacity .15s; }
pre:hover .copy-btn { opacity:1; }

figure.screenshot { margin:18px 0; }
figure.screenshot img { max-width:100%; border-radius:var(--radius); border:1px solid var(--border); cursor:zoom-in; }
figure.screenshot figcaption { color:var(--fg-2); font-size:13px; margin-top:6px; font-style:italic; }

.frame-strip { margin:18px 0; position:relative; }
.frame-strip .strip-track { display:flex; gap:12px; overflow-x:auto; scroll-snap-type:x mandatory; scroll-behavior:smooth; padding:4px; scrollbar-width:thin; }
.frame-strip .frame { flex:0 0 auto; width:min(80%, 520px); scroll-snap-align:center; margin:0; }
.frame-strip .frame img { width:100%; border-radius:var(--radius); border:1px solid var(--border); cursor:zoom-in; }
.frame-strip .frame figcaption { margin-top:6px; text-align:center; font-size:13px; color:var(--fg-2); font-weight:500; }
.frame-strip .strip-prev, .frame-strip .strip-next { position:absolute; top:50%; transform:translateY(-50%); background:rgba(0,0,0,.6); color:white; border:0; width:36px; height:36px; border-radius:50%; cursor:pointer; font-size:14px; z-index:2; }
.frame-strip .strip-prev { left:8px; } .frame-strip .strip-next { right:8px; }
.frame-strip .strip-caption { margin-top:10px; text-align:center; color:var(--fg-2); font-size:13px; font-style:italic; }

aside.tip { margin:14px 0; padding:12px 16px; border-left:3px solid var(--accent-2); background:var(--bg-2); border-radius:6px; font-size:14px; }
aside.tip.tip-warn { border-left-color:#e98a47; }
aside.tip.tip-info { border-left-color:var(--accent-2); }
aside.tip strong { color:var(--accent-2); margin-right:6px; }

dl#glossary { display:none; }
#glossary dt { color:var(--accent); font-weight:700; margin-top:12px; font-family:var(--font-mono); }
#glossary dd { margin:4px 0 0 0; color:var(--fg); }

.glossary-term { border-bottom:1px dotted var(--accent); cursor:help; position:relative; }
.glossary-term .glossary-tip { display:none; position:absolute; bottom:calc(100% + 6px); left:0; background:var(--bg-2); border:1px solid var(--border); padding:10px 12px; border-radius:8px; width:300px; z-index:50; font-size:13px; line-height:1.45; box-shadow:0 8px 24px rgba(0,0,0,.4); }
.glossary-term:hover .glossary-tip, .glossary-term:focus-within .glossary-tip { display:block; }
.glossary-term .glossary-tip strong { color:var(--accent); }

#lightbox { display:none; position:fixed; inset:0; background:rgba(0,0,0,.9); z-index:100; align-items:center; justify-content:center; cursor:zoom-out; padding:32px; }
#lightbox.on { display:flex; }
#lightbox img, #lightbox video { max-width:100%; max-height:100%; border-radius:8px; }

footer.page-footer { padding:24px 48px; color:var(--fg-2); font-size:12px; border-top:1px solid var(--border); margin-top:48px; font-family:var(--font-mono); }

@media (max-width:900px) {
  .app { grid-template-columns:1fr; }
  .sidebar { position:static; height:auto; border-right:0; border-bottom:1px solid var(--border); }
  main { padding:24px 18px; }
}

@media print {
  .topbar, .sidebar, .copy-btn, .strip-prev, .strip-next { display:none !important; }
  .app { grid-template-columns:1fr; }
  main { padding:0; max-width:100%; }
  body { background:white; color:black; font-size:11pt; }
  main section { page-break-inside:avoid; }
  main section h2 { page-break-before:always; }
  main section h2:first-of-type { page-break-before:auto; }
  a { color:black; }
  pre { background:#f0f0f0; color:black; border-color:#ccc; }
  pre.terminal .prompt { color:#0066cc; }
  pre.terminal .cmd { color:#a05000; }
}
`;

  const json = JSON.stringify(data);
  const js = `
const data = __MANUAL_JSON__;
const html = document.documentElement;
const $ = s => document.querySelector(s);
const $$ = s => Array.from(document.querySelectorAll(s));

function getLang() { return localStorage.getItem('lang') || data.meta.lang_default || 'es'; }
function getTheme() { return localStorage.getItem('theme') || 'dark'; }
function applyLang(l) {
  html.setAttribute('lang', l); html.setAttribute('data-lang', l);
  localStorage.setItem('lang', l);
  $$('[data-lang-content]').forEach(el => { el.style.display = el.dataset.langContent === l ? '' : 'none'; });
  injectGlossary();
}
function applyTheme(t) {
  html.setAttribute('data-theme', t); localStorage.setItem('theme', t);
  $('#btn-theme').textContent = t === 'dark' ? '☀' : '🌙';
}

document.addEventListener('DOMContentLoaded', () => {
  applyLang(getLang());
  applyTheme(getTheme());
  $('#btn-lang').addEventListener('click', () => applyLang(getLang() === 'es' ? 'en' : 'es'));
  $('#btn-theme').addEventListener('click', () => applyTheme(getTheme() === 'dark' ? 'light' : 'dark'));
  $('#btn-print').addEventListener('click', () => window.print());

  // Copy buttons
  $$('.copy-btn').forEach(btn => {
    btn.addEventListener('click', e => {
      e.preventDefault();
      const code = btn.closest('pre').querySelector('code');
      navigator.clipboard.writeText(code.innerText);
      const orig = btn.textContent; btn.textContent = 'Copiado ✓';
      setTimeout(() => btn.textContent = orig, 1200);
    });
  });

  // Lightbox
  const lb = document.createElement('div'); lb.id = 'lightbox';
  document.body.appendChild(lb);
  lb.addEventListener('click', () => { lb.classList.remove('on'); lb.innerHTML=''; });
  $$('figure.screenshot img, .frame-strip img').forEach(img => {
    img.addEventListener('click', () => {
      lb.innerHTML = '<img src="' + img.src + '" alt="' + (img.alt||'') + '">';
      lb.classList.add('on');
    });
  });

  // Frame strip nav
  $$('.frame-strip').forEach(strip => {
    const track = strip.querySelector('.strip-track');
    const step = () => track.querySelector('.frame').offsetWidth + 12;
    strip.querySelector('.strip-prev').addEventListener('click', () => track.scrollBy({left: -step(), behavior:'smooth'}));
    strip.querySelector('.strip-next').addEventListener('click', () => track.scrollBy({left:  step(), behavior:'smooth'}));
  });

  // Scroll-spy
  const sections = $$('main section[id]');
  const tocLinks = new Map($$('.sidebar a[data-id]').map(a => [a.dataset.id, a]));
  const io = new IntersectionObserver(entries => {
    for (const e of entries) {
      if (e.isIntersecting) {
        const a = tocLinks.get(e.target.id);
        if (a) {
          $$('.sidebar a[aria-current]').forEach(x => x.removeAttribute('aria-current'));
          a.setAttribute('aria-current','location');
        }
      }
    }
  }, { rootMargin:'-40% 0px -55% 0px' });
  sections.forEach(s => io.observe(s));

  // Search (substring)
  const search = $('#search');
  search.addEventListener('input', () => {
    const q = search.value.trim().toLowerCase();
    if (!q) {
      $$('.sidebar a').forEach(a => a.style.opacity = '');
      return;
    }
    $$('.sidebar a[data-id]').forEach(a => {
      const id = a.dataset.id;
      const sec = document.getElementById(id);
      const txt = (sec ? sec.innerText : a.innerText).toLowerCase();
      a.style.opacity = txt.includes(q) ? 1 : 0.25;
    });
  });
});

// Glossary tooltips: highlight first occurrence of each term per section
function injectGlossary() {
  const lang = getLang();
  document.querySelectorAll('.glossary-term').forEach(el => {
    const parent = el.parentNode;
    parent.replaceChild(document.createTextNode(el.textContent.replace(/[\\u200b]/g,'')), el);
  });
  const terms = (data.glossary || []).map(g => ({
    term: g.term[lang], def: g.def[lang], aliases: g.aliases || []
  })).filter(t => t.term);
  const sectionEls = document.querySelectorAll('main section[id]');
  sectionEls.forEach(sec => {
    const seen = new Set();
    sec.querySelectorAll('.body p, .body li, aside.tip').forEach(el => {
      terms.forEach(t => {
        if (seen.has(t.term)) return;
        const forms = [t.term, ...t.aliases].map(x => x.replace(/[.*+?^\${}()|[\\]\\\\]/g,'\\\\$&'));
        const re = new RegExp('\\\\b(' + forms.join('|') + ')\\\\b','i');
        const html2 = el.innerHTML;
        if (re.test(html2) && !html2.includes('glossary-term')) {
          el.innerHTML = html2.replace(re, '<span class="glossary-term" tabindex="0">$1<span class="glossary-tip" role="tooltip"><strong>' + t.term + '</strong><br>' + t.def + '</span></span>');
          seen.add(t.term);
        }
      });
    });
  });
}
`.replace('__MANUAL_JSON__', json);

  const tocEs = renderToc('es');
  const tocEn = renderToc('en');
  const bodyEs = data.sections.map(s => renderHtmlSection(s, 'es')).join('\n') + renderGlossary('es');
  const bodyEn = data.sections.map(s => renderHtmlSection(s, 'en')).join('\n') + renderGlossary('en');

  return `<!doctype html>
<html data-lang="${m.lang_default}" data-theme="dark" lang="${m.lang_default}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>${escHtml(m.app_name)} — Manual v${escHtml(m.version)}</title>
<style>${css}</style>
</head>
<body class="archetype-desktop-app">
<header class="topbar">
  <span class="brand">${escHtml(m.app_name)}</span>
  <span class="version">v${escHtml(m.version)} <span class="sha">·${escHtml(m.git_sha)}</span></span>
  <span class="search"><input id="search" type="search" placeholder="${m.lang_default==='es'?'Buscar…':'Search…'}" aria-label="${m.lang_default==='es'?'Buscar en el manual':'Search the manual'}"></span>
  <button id="btn-lang" aria-label="Toggle language">ES / EN</button>
  <button id="btn-theme" aria-label="Toggle theme">☀</button>
  <button id="btn-print" aria-label="Print">⎙</button>
</header>
<div class="app">
  <aside class="sidebar">
    <div data-lang-content="es">${tocEs}</div>
    <div data-lang-content="en">${tocEn}</div>
  </aside>
  <main>
    <div data-lang-content="es">${bodyEs}</div>
    <div data-lang-content="en">${bodyEn}</div>
  </main>
</div>
<footer class="page-footer">${escHtml(m.app_name)} v${escHtml(m.version)} · git ${escHtml(m.git_sha)} · ${escHtml(m.author)} · ${escHtml(m.license)} · ${escHtml(m.commercial_contact)}</footer>
<script>${js}</script>
</body>
</html>
`;
}

// ============================================================
// Main
// ============================================================
const wantTxt  = !args.includes('--html-only');
const wantHtml = !args.includes('--txt-only');

if (wantTxt) {
  writeFileSync(join(__dirname, 'manual.es.txt'), renderTxt('es'), 'utf8');
  writeFileSync(join(__dirname, 'manual.en.txt'), renderTxt('en'), 'utf8');
  console.log('  ✓ manual.es.txt + manual.en.txt');
}
if (wantHtml) {
  writeFileSync(join(__dirname, 'manual.html'), renderHtml(), 'utf8');
  console.log('  ✓ manual.html');
}
