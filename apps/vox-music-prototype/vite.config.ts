import { fileURLToPath } from 'node:url';
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  server: { allowedHosts: ['.e2b.app'], fs: { allow: ['.', fileURLToPath(new URL('../../docs/ui/vox-music/UI_BINDING_MATRIX.csv', import.meta.url))] } },
  preview: { allowedHosts: ['.e2b.app'] },
});
