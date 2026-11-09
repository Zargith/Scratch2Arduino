import { readdirSync } from "node:fs";
import { join, relative } from "node:path";

import { describe, expect, it } from "vitest";

function files(dir: string): string[] {
	return readdirSync(dir, { withFileTypes: true }).flatMap((entry) =>
		entry.isDirectory() ? files(join(dir, entry.name)) : [join(dir, entry.name)]
	);
}

describe("source file names", () => {
	// Windows and macOS do not tell "Gallery.ts" from "gallery.ts": the build breaks there.
	it("never differ only by case or extension", () => {
		const seen = new Map<string, string>();
		const clashes: string[] = [];
		for (const file of files(__dirname)) {
			const key = relative(__dirname, file)
				.replace(/\.(test\.)?(ts|tsx)$/, ".$1")
				.toLowerCase();
			const other = seen.get(key);
			if (other) clashes.push(`${other} / ${relative(__dirname, file)}`);
			else seen.set(key, relative(__dirname, file));
		}
		expect(clashes).toEqual([]);
	});
});
