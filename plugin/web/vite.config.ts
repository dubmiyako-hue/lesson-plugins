/// <reference types="vitest/config" />
import { fileURLToPath } from 'node:url'
import { defineConfig } from 'vite'

// The page the plug-ins embed (plugin/common/embed_web.cmake reads dist/). The names carry no hashes, so the same sources give
// the same files and the list of embedded files stays put; the paths are relative, as the plug-in serves the page from
// its resource provider's root. dev/ is a page for the browser alone, with a fake plug-in (npm run web:dev).
const root = fileURLToPath(new URL('.', import.meta.url))

export default defineConfig({
  root,
  base: './',
  esbuild: { jsx: 'automatic', jsxImportSource: 'preact' },
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    target: 'es2022',
    assetsInlineLimit: 0,
    modulePreload: { polyfill: false },
    rollupOptions: {
      input: { index: fileURLToPath(new URL('index.html', import.meta.url)) },
      output: {
        entryFileNames: 'assets/[name].js',
        chunkFileNames: 'assets/[name].js',
        assetFileNames: 'assets/[name][extname]',
      },
    },
  },
  test: {
    root,
    include: ['tests/**/*.test.ts'],
  },
})
