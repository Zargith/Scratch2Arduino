import { afterEach, describe, expect, it, vi } from "vitest";

import { bridge } from "@/bridge";
import { blocksThumbnail, codeThumbnail, fit, THUMBNAIL_HEIGHT, THUMBNAIL_WIDTH } from "@/thumbnail";

afterEach(() => vi.restoreAllMocks());

describe("thumbnail", () => {
	it("fits a picture in the thumbnail, centered and never enlarged", () => {
		const small = fit(100, 50);
		expect(small.scale).toBe(1);
		expect(small.x).toBe((THUMBNAIL_WIDTH - 100) / 2);
		expect(small.y).toBe((THUMBNAIL_HEIGHT - 50) / 2);
		const wide = fit(3040, 100);
		expect(wide.scale).toBeCloseTo((THUMBNAIL_WIDTH - 16) / 3040);
		expect(wide.x).toBeCloseTo(8);
		expect(fit(0, 10).scale).toBe(1);
	});

	it("draws the first lines of a text program", () => {
		const lines: string[] = [];
		const context = { fillRect() {}, fillText: (text: string) => lines.push(text) } as unknown as CanvasRenderingContext2D;
		vi.spyOn(HTMLCanvasElement.prototype, "getContext").mockReturnValue(context as never);
		vi.spyOn(HTMLCanvasElement.prototype, "toDataURL").mockReturnValue("data:image/png;base64,AAAA");
		const code = Array.from({ length: 30 }, (_, index) => `\tligne ${index}`).join("\n");
		expect(codeThumbnail(code)).toBe("data:image/png;base64,AAAA");
		expect(lines).toHaveLength(14);
		expect(lines[0]).toBe("  ligne 0");
	});

	it("gives no thumbnail rather than an error", async () => {
		vi.spyOn(HTMLCanvasElement.prototype, "getContext").mockReturnValue(null);
		expect(codeThumbnail("loop();")).toBeNull();
		const empty = { getBlocksBoundingBox: () => ({ left: 0, right: 0, top: 0, bottom: 0 }) };
		expect(await blocksThumbnail(empty as never)).toBeNull();
		const broken = {
			getBlocksBoundingBox: () => {
				throw new Error("no workspace");
			},
		};
		expect(await blocksThumbnail(broken as never)).toBeNull();
	});

	it("is sent to the host with the project on save", async () => {
		const save = vi.fn(async () => "/projets/a.s2a");
		(window as unknown as Record<string, unknown>).s2a_save = save;
		await bridge.save({} as never, "", "fr", "data:image/png;base64,AAAA");
		await bridge.save({} as never, "", "fr");
		expect(save.mock.calls).toEqual([
			[{}, "", "fr", "data:image/png;base64,AAAA"],
			[{}, "", "fr", null],
		]);
	});
});
