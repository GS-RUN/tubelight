#!/usr/bin/env node
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Genera manual.pdf usando Playwright headless Chromium.
import { chromium } from 'playwright';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const htmlPath = resolve(__dirname, 'manual.html');
const pdfPath = resolve(__dirname, 'manual.pdf');

const browser = await chromium.launch();
const page = await browser.newPage();
await page.goto(pathToFileURL(htmlPath).toString(), { waitUntil: 'networkidle' });
await page.emulateMedia({ media: 'print' });
await page.pdf({
  path: pdfPath,
  format: 'A4',
  printBackground: true,
  margin: { top: '18mm', bottom: '18mm', left: '16mm', right: '16mm' }
});
await browser.close();
console.log('  ✓ manual.pdf →', pdfPath);
