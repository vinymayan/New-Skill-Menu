import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'


export default defineConfig({
    base: './',
    plugins: [react()],
    build: {
        assetsDir: '',
        rollupOptions: {
            output: {
                entryFileNames: `[name].js`,
                chunkFileNames: `[name].js`,
                assetFileNames: `[name].[ext]`
            }
        }
    }
})