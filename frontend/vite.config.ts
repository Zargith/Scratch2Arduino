import { cpSync } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";

import react from "@vitejs/plugin-react";
import { defineConfig, type Plugin } from "vitest/config";
import { viteSingleFile } from "vite-plugin-singlefile";

const root = fileURLToPath(new URL(".", import.meta.url));

/** scratch-blocks loads its icons from `pathToMedia`: ship them next to index.html. */
function scratchBlocksMedia(): Plugin {
	let outDir = `${root}dist`;
	return {
		name: "s2a-scratch-blocks-media",
		apply: "build",
		configResolved(config) {
			outDir = resolve(config.root, config.build.outDir);
		},
		closeBundle() {
			cpSync(`${root}node_modules/scratch-blocks/media`, `${outDir}/media`, { recursive: true });
		},
	};
}

// The native host loads dist/index.html from disk (file://), where ES module scripts are
// blocked by some engines: everything is inlined into a single HTML file.
export default defineConfig({
	base: "./",
	plugins: [react(), viteSingleFile(), scratchBlocksMedia()],
	resolve: { alias: { "@": `${root}src` } },
	server: { fs: { allow: [`${root}..`] } },
	publicDir: false,
	build: { outDir: "dist", emptyOutDir: true, chunkSizeWarningLimit: 8000 },
	test: {
		environment: "jsdom",
		include: ["src/**/*.test.ts", "src/**/*.test.tsx"],
	},
});
