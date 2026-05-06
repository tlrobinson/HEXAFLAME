import { defineConfig } from "vite";

export default defineConfig({
  base: "./",
  build: {
    rollupOptions: {
      input: {
        main: "index.html",
        layout: "layout.html",
      },
    },
  },
  server: {
    host: "127.0.0.1",
    port: 8000,
  },
  preview: {
    host: "127.0.0.1",
    port: 8000,
  },
});
