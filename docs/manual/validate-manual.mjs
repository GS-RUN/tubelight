#!/usr/bin/env node
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Validador CI para manual.json. Falla con exit 1 si:
//  - falta es o en en algún campo bilingüe
//  - algún screenshots[].src no existe en disco
//  - algún code[] sin lang
//  - falta meta.git_sha
//  - hay TODO_SCREENSHOT sin --allow-todo
//  - alt vacío en screenshots
//
// Uso:  node validate-manual.mjs [--strict] [--allow-todo]

import { readFileSync, existsSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const allowTodo = args.includes('--allow-todo');
const strict = args.includes('--strict');

const path = join(__dirname, 'manual.json');
let data;
try {
  data = JSON.parse(readFileSync(path, 'utf8'));
} catch (e) {
  console.error('FAIL: cannot parse manual.json:', e.message);
  process.exit(1);
}

const errs = [];
const warns = [];

function checkBilingual(obj, label) {
  if (!obj) { errs.push(`${label}: missing field`); return; }
  if (!obj.es || obj.es.trim() === '') errs.push(`${label}: empty es`);
  if (!obj.en || obj.en.trim() === '') errs.push(`${label}: empty en`);
}

if (!data.meta) errs.push('meta: missing');
else {
  if (!data.meta.git_sha) errs.push('meta.git_sha: missing');
  if (!data.meta.version) errs.push('meta.version: missing');
  if (!data.meta.app_name) errs.push('meta.app_name: missing');
  checkBilingual(data.meta.tagline, 'meta.tagline');
}

function walkSection(s, prefix='') {
  const id = `${prefix}${s.id}`;
  checkBilingual(s.title, `${id}.title`);
  checkBilingual(s.body, `${id}.body`);

  for (const sh of (s.screenshots || [])) {
    if (!sh.src) errs.push(`${id}.screenshots: missing src`);
    else {
      const full = join(__dirname, sh.src);
      if (!existsSync(full)) {
        if (sh.src.includes('TODO_SCREENSHOT')) {
          if (!allowTodo) errs.push(`${id}: TODO_SCREENSHOT present (${sh.src})`);
        } else {
          errs.push(`${id}.screenshots[].src not on disk: ${sh.src}`);
        }
      }
    }
    checkBilingual(sh.alt, `${id}.screenshots.alt`);
    checkBilingual(sh.caption, `${id}.screenshots.caption`);
  }

  for (const c of (s.code || [])) {
    if (!c.lang) errs.push(`${id}.code: missing lang`);
    if (!c.src) errs.push(`${id}.code: missing src`);
  }

  for (const t of (s.tips || [])) {
    if (!t.es) errs.push(`${id}.tips: missing es`);
    if (!t.en) errs.push(`${id}.tips: missing en`);
  }

  for (const sub of (s.subsections || [])) walkSection(sub, id + '.');
}

for (const s of (data.sections || [])) walkSection(s);

for (const g of (data.glossary || [])) {
  checkBilingual(g.term, `glossary.term`);
  checkBilingual(g.def, `glossary.def`);
}

if (errs.length === 0) {
  console.log(`OK — manual.json valid. sections=${data.sections.length} glossary=${data.glossary.length}`);
  if (warns.length) console.log(`WARN x${warns.length}`);
  process.exit(0);
} else {
  console.error(`FAIL — ${errs.length} error(s):`);
  for (const e of errs) console.error('  -', e);
  if (warns.length) console.error(`(plus ${warns.length} warnings)`);
  process.exit(1);
}
